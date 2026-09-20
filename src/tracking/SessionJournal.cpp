#include "tracking/SessionJournal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {
// The header identifies both the file and the format version. A file without it is not a
// journal this build can trust, so it is set aside rather than guessed at.
constexpr char kHeader[] = "OMKJRNL1\n";
constexpr qsizetype kHeaderLength = 9;
// Per record and whole file caps. They bound disk use and replay work; the journal refuses
// new records at the cap and never evicts an accepted one.
constexpr qsizetype kMaxRecordBytes = 64 * 1024;
constexpr qsizetype kMaxFileBytes = 256 * 1024;
constexpr int kMaxLiveRecords = 512;
constexpr int kFormatVersion = 1;

quint32 crc32(const QByteArray& data) {
  static const std::array<quint32, 256> table = [] {
    std::array<quint32, 256> entries{};
    for (quint32 index = 0; index < 256; ++index) {
      quint32 value = index;
      for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
      }
      entries[index] = value;
    }
    return entries;
  }();
  quint32 crc = 0xFFFFFFFFu;
  for (char byte : data) {
    crc = table[(crc ^ static_cast<quint8>(byte)) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

void appendU32(QByteArray& buffer, quint32 value) {
  buffer.append(static_cast<char>((value >> 24) & 0xFF));
  buffer.append(static_cast<char>((value >> 16) & 0xFF));
  buffer.append(static_cast<char>((value >> 8) & 0xFF));
  buffer.append(static_cast<char>(value & 0xFF));
}

quint32 readU32(const QByteArray& buffer, qsizetype offset) {
  return (static_cast<quint32>(static_cast<quint8>(buffer.at(offset))) << 24) |
         (static_cast<quint32>(static_cast<quint8>(buffer.at(offset + 1))) << 16) |
         (static_cast<quint32>(static_cast<quint8>(buffer.at(offset + 2))) << 8) |
         static_cast<quint32>(static_cast<quint8>(buffer.at(offset + 3)));
}

bool syncFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || ::fsync(file.handle()) != 0)
    return false;
  return true;
}

bool syncDirectoryPath(const QString& directory) {
  const QByteArray encoded = directory.toLocal8Bit();
  const int descriptor = ::open(encoded.constData(), O_RDONLY | O_DIRECTORY);
  if (descriptor < 0)
    return false;
  const bool durable = ::fsync(descriptor) == 0;
  const int closed = ::close(descriptor);
  return closed == 0 && durable;
}

bool syncDirectory(const QString& path) {
  return syncDirectoryPath(QFileInfo(path).absolutePath());
}

bool writeBytesDurable(const QString& path, const QByteArray& bytes,
                       QFileDevice::Permissions permissions) {
  QByteArray temporary = path.toLocal8Bit() + ".tmp.XXXXXX";
  const int descriptor = ::mkstemp(temporary.data());
  if (descriptor < 0)
    return false;
  const auto fail = [&descriptor, &temporary]() {
    ::close(descriptor);
    ::unlink(temporary.constData());
    return false;
  };
  mode_t mode = (permissions & QFileDevice::ReadOwner) ? S_IRUSR : 0;
  if (permissions & QFileDevice::WriteOwner)
    mode |= S_IWUSR;
  if (::fchmod(descriptor, mode) != 0)
    return fail();
  qsizetype written = 0;
  while (written < bytes.size()) {
    const ssize_t count = ::write(descriptor, bytes.constData() + written,
                                  static_cast<size_t>(bytes.size() - written));
    if (count < 0) {
      if (errno == EINTR)
        continue;
      return fail();
    }
    written += count;
  }
  if (::fdatasync(descriptor) != 0 || ::fsync(descriptor) != 0)
    return fail();
  if (::close(descriptor) != 0) {
    ::unlink(temporary.constData());
    return false;
  }
  // POSIX rename atomically replaces the complete target on Linux. A crash before rename
  // leaves the old journal; after rename it leaves the complete replacement.
  if (::rename(temporary.constData(), path.toLocal8Bit().constData()) != 0) {
    ::unlink(temporary.constData());
    return false;
  }
  return syncFile(path) && syncDirectory(path);
}

QByteArray encode(const SessionJournal::Operation& operation, bool ack) {
  QJsonObject object;
  object.insert(QStringLiteral("v"), kFormatVersion);
  if (ack) {
    object.insert(QStringLiteral("t"), QStringLiteral("ack"));
    object.insert(QStringLiteral("key"), operation.key);
  } else {
    object.insert(QStringLiteral("t"), QStringLiteral("op"));
    object.insert(QStringLiteral("key"), operation.key);
    object.insert(QStringLiteral("game"), operation.gamePath);
    object.insert(QStringLiteral("source"), operation.source);
    object.insert(QStringLiteral("start"), operation.startedAt);
    object.insert(QStringLiteral("end"), operation.endedAt);
    object.insert(QStringLiteral("sec"), operation.seconds);
    object.insert(QStringLiteral("pid"), operation.pid);
    object.insert(QStringLiteral("proc"), operation.procStart);
    object.insert(QStringLiteral("obs"), operation.observedAt);
    object.insert(QStringLiteral("inc"), operation.incarnation);
    object.insert(QStringLiteral("gep"), operation.clearEpoch);
    object.insert(QStringLiteral("open"), operation.open);
  }
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool decode(const QByteArray& payload, SessionJournal::Operation& operation, bool& ack,
            bool& valid) {
  valid = false;
  const QJsonDocument document = QJsonDocument::fromJson(payload);
  if (!document.isObject()) {
    return false;
  }
  const QJsonObject object = document.object();
  if (object.value(QStringLiteral("v")).toInt() != kFormatVersion) {
    // A future version is skipped rather than misread.
    return true;
  }
  const QString key = object.value(QStringLiteral("key")).toString();
  if (key.isEmpty() || key.size() > 128) {
    return false;
  }
  const QString type = object.value(QStringLiteral("t")).toString();
  if (type == QStringLiteral("ack")) {
    operation.key = key;
    ack = true;
    valid = true;
    return true;
  }
  if (type != QStringLiteral("op")) {
    // An unknown record kind is not a reason to treat the file as corrupt, but it carries
    // nothing replayable.
    return true;
  }
  const QString gamePath = object.value(QStringLiteral("game")).toString();
  if (gamePath.isEmpty() || gamePath.size() > 4096) {
    return false;
  }
  const qint64 startedAt = object.value(QStringLiteral("start")).toVariant().toLongLong();
  const qint64 seconds = object.value(QStringLiteral("sec")).toVariant().toLongLong();
  if (startedAt <= 0 || seconds < 0) {
    return false;
  }
  operation.key = key;
  operation.gamePath = gamePath;
  operation.source = object.value(QStringLiteral("source")).toString().left(256);
  operation.startedAt = startedAt;
  operation.endedAt = object.value(QStringLiteral("end")).toVariant().toLongLong();
  operation.seconds = seconds;
  operation.pid = object.value(QStringLiteral("pid")).toVariant().toLongLong();
  operation.procStart = object.value(QStringLiteral("proc")).toVariant().toLongLong();
  operation.observedAt = object.value(QStringLiteral("obs")).toVariant().toLongLong();
  operation.incarnation = object.value(QStringLiteral("inc")).toString().left(128);
  operation.clearEpoch = object.value(QStringLiteral("gep")).toVariant().toLongLong();
  operation.open = object.value(QStringLiteral("open")).toBool();
  ack = false;
  valid = true;
  return true;
}
} // namespace

SessionJournal::SessionJournal(QString path) : m_path(std::move(path)) {}

SessionJournal::~SessionJournal() = default;

SessionJournal::Status SessionJournal::status(const QString& path) {
  Status result;
  QFileInfo info(path);
  if (!info.exists()) {
    return result;
  }
  // Mirror the recorder's capacity rule exactly, including the byte cap, so a journal that is
  // full by size rather than by entry count still raises the warning.
  if (info.size() >= kMaxFileBytes) {
    result.full = true;
  }
  SessionJournal reader(path);
  QVector<Record> records;
  bool torn = false;
  if (!reader.readAll(records, torn)) {
    result.corrupt = true;
    return result;
  }
  QHash<QString, int> latest;
  QSet<QString> acked;
  for (const Record& record : records) {
    if (record.isAck) {
      acked.insert(record.ackKey);
    } else {
      latest.insert(record.operation.key, 1);
    }
  }
  for (auto it = acked.cbegin(); it != acked.cend(); ++it) {
    latest.remove(*it);
  }
  result.pending = latest.size();
  if (result.pending >= kMaxLiveRecords) {
    result.full = true;
  }
  return result;
}

bool SessionJournal::ensureDirectory() const {
  const QFileInfo info(m_path);
  const QString directory = info.absolutePath();
  if (directory.isEmpty()) {
    return false;
  }
  if (!QDir().mkpath(directory))
    return false;
  // A newly created directory is not durable merely because it is open. Persist each new
  // entry in its parent, from the deepest created directory back through existing parents.
  QDir current(directory);
  while (true) {
    const QString path = current.absolutePath();
    if (!syncDirectoryPath(path))
      return false;
    QDir parent = current;
    if (!parent.cdUp() || parent.absolutePath() == path)
      return true;
    current = parent;
  }
}

bool SessionJournal::readOrCreateOwner() {
  const QString path = m_path + QStringLiteral(".owner");
  QFile file(path);
  if (file.exists()) {
    if (!file.open(QIODevice::ReadOnly))
      return false;
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 16 || bytes.size() > 129)
      return false;
    m_owner = QString::fromLocal8Bit(bytes).trimmed();
    return !m_owner.isEmpty();
  }
  m_owner = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QByteArray bytes = m_owner.toUtf8() + '\n';
  return writeBytesDurable(path, bytes, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool SessionJournal::readAll(QVector<Record>& records, bool& tornTail) const {
  tornTail = false;
  QFile file(m_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const qint64 size = file.size();
  if (size > kMaxFileBytes * 4) {
    // A file far beyond any cap this format can produce is not a journal we wrote.
    return false;
  }
  const QByteArray contents = file.readAll();
  if (contents.size() < kHeaderLength) {
    // Too short to be a journal. The caller reinitializes the file; it cannot hold a record.
    return false;
  }
  if (contents.left(kHeaderLength) != QByteArray(kHeader, kHeaderLength)) {
    return false;
  }
  qsizetype offset = kHeaderLength;
  while (offset < contents.size()) {
    if (offset + 4 > contents.size()) {
      // The length field of the final record was not fully written: a torn append.
      tornTail = true;
      break;
    }
    const quint32 length = readU32(contents, offset);
    if (length > static_cast<quint32>(kMaxRecordBytes)) {
      return false;
    }
    if (offset + 4 + static_cast<qsizetype>(length) + 4 > contents.size()) {
      // The payload or its checksum was not fully written: a torn append.
      tornTail = true;
      break;
    }
    const QByteArray payload = contents.mid(offset + 4, static_cast<qsizetype>(length));
    const quint32 expected = readU32(contents, offset + 4 + static_cast<qsizetype>(length));
    if (crc32(payload) != expected) {
      // A complete frame with a bad checksum is interior corruption, not a torn tail.
      return false;
    }
    Operation operation;
    bool ack = false;
    bool valid = false;
    if (!decode(payload, operation, ack, valid)) {
      return false;
    }
    offset += 4 + static_cast<qsizetype>(length) + 4;
    if (!valid) {
      continue;
    }
    Record record;
    record.operation = operation;
    record.isAck = ack;
    record.ackKey = operation.key;
    records.append(record);
  }
  return true;
}

bool SessionJournal::writeRecords(const QVector<Record>& records) {
  QByteArray contents;
  contents.append(kHeader, kHeaderLength);
  for (const Record& record : records) {
    const QByteArray payload = encode(record.operation, record.isAck);
    if (payload.size() > kMaxRecordBytes) {
      continue;
    }
    QByteArray framed;
    framed.reserve(payload.size() + 8);
    appendU32(framed, static_cast<quint32>(payload.size()));
    framed.append(payload);
    appendU32(framed, crc32(payload));
    contents.append(framed);
  }
  return writeBytesDurable(m_path, contents, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool SessionJournal::open() {
  m_available = false;
  m_full = false;
  m_recoveredCorrupt = false;
  m_recoveredTornTail = false;
  m_pendingKeys.clear();
  if (!ensureDirectory()) {
    return false;
  }
  if (!readOrCreateOwner())
    return false;
  QFileInfo info(m_path);
  if (info.exists() && info.size() < kHeaderLength) {
    // A torn creation left fewer bytes than a header, which cannot hold a record. Establish a
    // valid header rather than quarantining, so the next append lands after a complete header
    // instead of after a partial one that would later look like corruption.
    m_recoveredTornTail = info.size() > 0;
    if (!writeRecords({})) {
      return false;
    }
  } else if (info.exists()) {
    QVector<Record> records;
    bool torn = false;
    if (!readAll(records, torn)) {
      // Interior damage: preserve exactly one copy for diagnostics and start fresh so the
      // damage cannot poison every later startup. This is the only place the journal
      // discards data, and the copy is kept.
      const QString corruptPath = m_path + QStringLiteral(".corrupt");
      QFile::remove(corruptPath);
      QFile::rename(m_path, corruptPath);
      m_recoveredCorrupt = true;
    } else {
      if (torn) {
        // A crash tore the final append. Rewrite the verified prefix so already accepted
        // records still replay and the partial frame is not mistaken for damage later.
        if (!writeRecords(records)) {
          return false;
        }
        m_recoveredTornTail = true;
      }
      QHash<QString, int> latest;
      QSet<QString> acked;
      for (const Record& record : records) {
        if (record.isAck) {
          acked.insert(record.ackKey);
        } else {
          latest.insert(record.operation.key, 1);
        }
      }
      for (auto it = acked.cbegin(); it != acked.cend(); ++it) {
        latest.remove(*it);
      }
      for (auto it = latest.cbegin(); it != latest.cend(); ++it) {
        m_pendingKeys.insert(it.key());
      }
    }
  } else {
    if (!writeRecords({})) {
      return false;
    }
  }
  if (!QFileInfo::exists(m_path) && !writeRecords({})) {
    return false;
  }
  m_available = true;
  return true;
}

void SessionJournal::close() { m_available = false; }

int SessionJournal::recordCount() const { return m_pendingKeys.size(); }

bool SessionJournal::append(const Operation& operation) {
  if (!m_available || operation.key.isEmpty()) {
    return false;
  }
  // Replacing an existing pending checkpoint does not require a new slot. A genuinely new
  // session is refused at the cap; a replacement is still bounded by the byte cap below.
  if (!m_pendingKeys.contains(operation.key) &&
      (m_pendingKeys.size() >= kMaxLiveRecords || QFileInfo(m_path).size() >= kMaxFileBytes)) {
    m_full = true;
    return false;
  }
  const QByteArray payload = encode(operation, false);
  if (payload.size() > kMaxRecordBytes) {
    m_full = true;
    return false;
  }
  // A newer operation for a key that already has a pending record replaces it, so repeated
  // checkpoints for one long session neither grow the file nor count toward the entry cap.
  if (m_pendingKeys.contains(operation.key)) {
    QVector<Record> records;
    bool torn = false;
    if (!readAll(records, torn)) {
      m_available = false;
      return false;
    }
    QVector<Record> kept;
    kept.reserve(records.size() + 1);
    for (const Record& record : records) {
      if (record.isAck || record.operation.key != operation.key) {
        kept.append(record);
      }
    }
    Record fresh;
    fresh.operation = operation;
    kept.append(fresh);
    qsizetype projectedSize = kHeaderLength;
    for (const Record& record : kept) {
      const QByteArray recordPayload = encode(record.operation, record.isAck);
      projectedSize += 8 + recordPayload.size();
    }
    if (projectedSize >= kMaxFileBytes) {
      m_full = true;
      return false;
    }
    if (!writeRecords(kept)) {
      m_available = false;
      return false;
    }
    return true;
  }
  QByteArray framed;
  framed.reserve(payload.size() + 8);
  appendU32(framed, static_cast<quint32>(payload.size()));
  framed.append(payload);
  appendU32(framed, crc32(payload));
  QFile file(m_path);
  const bool fileExisted = QFileInfo(m_path).exists();
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
    m_available = false;
    return false;
  }
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  if (file.write(framed) != framed.size() || !file.flush()) {
    m_available = false;
    return false;
  }
  // The record is only acknowledged once it is on disk, so a crash cannot lose an operation
  // the recorder already reported as pending.
  if (::fdatasync(file.handle()) != 0 || ::fsync(file.handle()) != 0 ||
      (!fileExisted && !syncDirectory(m_path))) {
    m_available = false;
    return false;
  }
  m_pendingKeys.insert(operation.key);
  return true;
}

QVector<SessionJournal::Operation> SessionJournal::pending(int maxRecords) const {
  QVector<Operation> result;
  QVector<Record> records;
  bool torn = false;
  if (!m_available || !readAll(records, torn)) {
    return result;
  }
  QHash<QString, int> latest;
  QSet<QString> acked;
  for (int index = 0; index < records.size(); ++index) {
    const Record& record = records.at(index);
    if (record.isAck) {
      acked.insert(record.ackKey);
      continue;
    }
    latest.insert(record.operation.key, index);
  }
  QVector<int> order;
  order.reserve(latest.size());
  for (auto it = latest.cbegin(); it != latest.cend(); ++it) {
    order.append(it.value());
  }
  std::sort(order.begin(), order.end());
  for (int index : order) {
    if (result.size() >= maxRecords) {
      break;
    }
    if (acked.contains(records.at(index).operation.key)) {
      continue;
    }
    result.append(records.at(index).operation);
  }
  return result;
}

bool SessionJournal::compact(const QStringList& dropKeys) {
  if (!m_available) {
    return false;
  }
  QVector<Record> records;
  bool torn = false;
  if (!readAll(records, torn)) {
    return false;
  }
  QSet<QString> dropped;
  for (const QString& key : dropKeys) {
    dropped.insert(key);
  }
  QHash<QString, int> latest;
  QSet<QString> acked;
  for (int index = 0; index < records.size(); ++index) {
    const Record& record = records.at(index);
    if (record.isAck) {
      acked.insert(record.ackKey);
      continue;
    }
    latest.insert(record.operation.key, index);
  }
  QVector<int> order;
  order.reserve(latest.size());
  for (auto it = latest.cbegin(); it != latest.cend(); ++it) {
    order.append(it.value());
  }
  std::sort(order.begin(), order.end());
  QVector<Record> kept;
  for (int index : order) {
    const Operation& operation = records.at(index).operation;
    if (acked.contains(operation.key) || dropped.contains(operation.key)) {
      continue;
    }
    kept.append(records.at(index));
  }
  if (!writeRecords(kept)) {
    return false;
  }
  m_pendingKeys.clear();
  for (const Record& record : kept) {
    m_pendingKeys.insert(record.operation.key);
  }
  if (m_pendingKeys.size() < kMaxLiveRecords && QFileInfo(m_path).size() < kMaxFileBytes) {
    m_full = false;
  }
  return true;
}
