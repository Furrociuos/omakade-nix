#include "sources/ppsspp/PpssppScanner.h"

#include "sources/FlatpakInstall.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QtEndian>
#include <limits>

namespace {
constexpr qint64 kMaximumSfoBytes = 4 * 1024 * 1024;
constexpr qint64 kMaximumIniBytes = 4 * 1024 * 1024;
constexpr int kIsoSectorSize = 2048;
constexpr quint16 kPsfUtf8 = 0x0204;
constexpr quint16 kPsfUtf8Legacy = 0x0004;
constexpr quint16 kPsfUtf16 = 0x0202;
constexpr int kMaximumSfoEntries = 4096;

bool readU32(const QByteArray& bytes, int offset, quint32* value) {
  if (offset < 0 || offset + 4 > bytes.size()) {
    return false;
  }
  *value = qFromLittleEndian<quint32>(
      reinterpret_cast<const uchar*>(bytes.constData() + offset));
  return true;
}

QByteArray boundedRead(const QString& path, qint64 limit, bool* okay) {
  *okay = false;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > limit) {
    return {};
  }
  const QByteArray bytes = file.readAll();
  *okay = bytes.size() <= limit;
  return bytes;
}

QString regionForId(const QString& id) {
  if (id.size() < 3) {
    return {};
  }
  if (id.startsWith(QStringLiteral("NPH"), Qt::CaseInsensitive)) {
    return QStringLiteral("Hong Kong");
  }
  if (id.startsWith(QStringLiteral("NPI"), Qt::CaseInsensitive)) {
    return QStringLiteral("Internal");
  }
  switch (id.at(2).toUpper().unicode()) {
  case 'E':
    return QStringLiteral("Europe");
  case 'U':
    return QStringLiteral("USA");
  case 'J':
    return QStringLiteral("Japan");
  case 'K':
    return QStringLiteral("Korea");
  case 'A':
    return QStringLiteral("Asia");
  default:
    return {};
  }
}

PspParamSfo parseParamSfo(const QByteArray& bytes) {
  PspParamSfo result;
  if (bytes.size() < 20 || bytes.left(4) != QByteArrayLiteral("\x00PSF")) {
    return result;
  }
  quint32 keyTable = 0;
  quint32 dataTable = 0;
  quint32 count = 0;
  if (!readU32(bytes, 8, &keyTable) || !readU32(bytes, 12, &dataTable) ||
      !readU32(bytes, 16, &count) || count > kMaximumSfoEntries ||
      keyTable > static_cast<quint32>(bytes.size()) ||
      dataTable > static_cast<quint32>(bytes.size()) ||
      count > static_cast<quint32>((bytes.size() - 20) / 16)) {
    return result;
  }
  for (quint32 index = 0; index < count; ++index) {
    const int entry = 20 + static_cast<int>(index) * 16;
    const quint16 keyOffset =
        qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(bytes.constData() + entry));
    const quint16 format = qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(bytes.constData() + entry + 2));
    quint32 length = 0;
    quint32 dataOffset = 0;
    if (!readU32(bytes, entry + 4, &length) || !readU32(bytes, entry + 12, &dataOffset)) {
      return {};
    }
    const qint64 keyStart = static_cast<qint64>(keyTable) + keyOffset;
    if (keyStart < keyTable || keyStart >= bytes.size()) {
      return {};
    }
    const qint64 keyEnd = bytes.indexOf('\0', static_cast<int>(keyStart));
    const qint64 valueStart = static_cast<qint64>(dataTable) + dataOffset;
    if (keyEnd < 0 || valueStart < dataTable ||
        length > static_cast<quint32>(bytes.size()) || valueStart + length > bytes.size()) {
      return {};
    }
    const QString key = QString::fromUtf8(
        bytes.mid(static_cast<int>(keyStart), static_cast<int>(keyEnd - keyStart)));
    QString value;
    if (format == kPsfUtf8 || format == kPsfUtf8Legacy) {
      value = QString::fromUtf8(
          bytes.mid(static_cast<int>(valueStart), static_cast<int>(length)));
    } else if (format == kPsfUtf16) {
      for (quint32 offset = 0; offset + 1 < length; offset += 2) {
        const quint16 code = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar*>(bytes.constData() + valueStart + offset));
        if (code == 0) {
          break;
        }
        value.append(QChar(code));
      }
    } else {
      continue;
    }
    const int nul = value.indexOf(QChar::Null);
    value = nul < 0 ? value : value.left(nul);
    if (key == QStringLiteral("TITLE")) {
      result.title = value;
    } else if (key == QStringLiteral("DISC_ID")) {
      result.discId = value.toUpper();
    } else if (key == QStringLiteral("DISC_VERSION")) {
      result.discVersion = value;
    }
  }
  result.title = result.title.trimmed();
  result.discId = result.discId.trimmed().toUpper();
  result.discVersion = result.discVersion.trimmed();
  result.region = regionForId(result.discId);
  return result;
}

QByteArray isoRead(QFile& file, quint32 lba, qint64 size) {
  if (lba > std::numeric_limits<quint32>::max() / kIsoSectorSize || size < 0 ||
      size > kMaximumSfoBytes * 16) {
    return {};
  }
  const qint64 offset = static_cast<qint64>(lba) * kIsoSectorSize;
  if (offset < 0 || offset + size > file.size() || !file.seek(offset)) {
    return {};
  }
  return file.read(size);
}

QByteArray isoFindEntry(QFile& file, const QByteArray& directoryRecord,
                        const QString& wantedName) {
  if (directoryRecord.size() < 34) {
    return {};
  }
  const quint32 lba = qFromLittleEndian<quint32>(
      reinterpret_cast<const uchar*>(directoryRecord.constData() + 2));
  const quint32 length = qFromLittleEndian<quint32>(
      reinterpret_cast<const uchar*>(directoryRecord.constData() + 10));
  if (length == 0 || length > 16 * 1024 * 1024) {
    return {};
  }
  const QByteArray directory = isoRead(file, lba, length);
  if (directory.size() != static_cast<int>(length)) {
    return {};
  }
  int offset = 0;
  while (offset < directory.size()) {
    const quint8 recordLength = static_cast<quint8>(directory.at(offset));
    if (recordLength == 0) {
      offset = ((offset / kIsoSectorSize) + 1) * kIsoSectorSize;
      continue;
    }
    if (offset + recordLength > directory.size() || recordLength < 34) {
      return {};
    }
    const QByteArray record = directory.mid(offset, recordLength);
    const int nameLength = static_cast<quint8>(record.at(32));
    if (nameLength <= 0 || 33 + nameLength > record.size()) {
      return {};
    }
    QString name = QString::fromLatin1(record.mid(33, nameLength));
    const int version = name.indexOf(QLatin1Char(';'));
    if (version >= 0) {
      name.truncate(version);
    }
    if (name.compare(wantedName, Qt::CaseInsensitive) == 0) {
      return record;
    }
    offset += recordLength;
  }
  return {};
}

QString gameIdForPath(const QString& path) {
  const QByteArray hash = QCryptographicHash::hash(QFileInfo(path).canonicalFilePath().toUtf8(),
                                                   QCryptographicHash::Sha256);
  return QStringLiteral("path:") + QString::fromLatin1(hash.toHex().left(24));
}

void addGame(const QString& path, bool flatpak, QSet<QString>* seenPaths, QSet<QString>* seenIds,
             PspScanResult* result) {
  const QFileInfo info(path);
  if (!info.isFile()) {
    return;
  }
  const QString extension = info.suffix().toLower();
  if (extension != QStringLiteral("iso") && extension != QStringLiteral("cso") &&
      extension != QStringLiteral("chd") && extension != QStringLiteral("pbp") &&
      extension != QStringLiteral("elf")) {
    return;
  }
  PspParamSfo sfo;
  if (extension == QStringLiteral("iso")) {
    sfo = PpssppScanner::readParamSfoFromIso(info.absoluteFilePath());
  } else if (extension == QStringLiteral("pbp")) {
    sfo = PpssppScanner::readParamSfoFromPbp(info.absoluteFilePath());
  }
  if ((extension == QStringLiteral("cso") || extension == QStringLiteral("chd")) &&
      !sfo.valid()) {
    result->warnings.append(
        QStringLiteral("Compressed PSP image has no readable PARAM.SFO: %1")
            .arg(info.absoluteFilePath()));
    return;
  }
  QString gameId = sfo.discId;
  if (gameId.isEmpty() && (sfo.valid() || extension == QStringLiteral("elf"))) {
    gameId = gameIdForPath(info.absoluteFilePath());
  } else if (gameId.isEmpty()) {
    result->warnings.append(
        QStringLiteral("PSP image has no DISC_ID and is not an ELF: %1")
            .arg(info.absoluteFilePath()));
    return;
  }
  const QString cleaned = QDir::cleanPath(info.absoluteFilePath());
  if (seenPaths->contains(cleaned) || seenIds->contains(gameId)) {
    return;
  }
  QString title = sfo.title;
  if (title.isEmpty()) {
    title = info.completeBaseName();
  }
  result->games.append(PspGameRecord{.gameId = gameId,
                                     .title = title,
                                     .path = cleaned,
                                     .discId = sfo.discId,
                                     .discVersion = sfo.discVersion,
                                     .region = sfo.region,
                                     .coverPath = {},
                                     .homebrew = sfo.discId.isEmpty(),
                                     .flatpak = flatpak});
  seenPaths->insert(cleaned);
  seenIds->insert(gameId);
}

QStringList iniPaths(const QString& configRoot) {
  bool okay = false;
  const QByteArray bytes =
      boundedRead(configRoot + QStringLiteral("/PSP/SYSTEM/ppsspp.ini"), kMaximumIniBytes, &okay);
  if (!okay) {
    return {};
  }
  QStringList paths;
  static const QRegularExpression recent(
      QStringLiteral("(?m)^\\s*FileName\\d+\\s*=\\s*(.+?)\\s*$"));
  static const QRegularExpression pinned(
      QStringLiteral("(?m)^\\s*Path\\d+\\s*=\\s*(.+?)\\s*$"));
  for (const QRegularExpression& expression : {recent, pinned}) {
    auto matches = expression.globalMatch(QString::fromUtf8(bytes));
    while (matches.hasNext()) {
      QString path = matches.next().captured(1).trimmed();
      if (path.size() >= 2 && path.startsWith(QLatin1Char('"')) &&
          path.endsWith(QLatin1Char('"'))) {
        path = path.mid(1, path.size() - 2);
      }
      if (QFileInfo::exists(path)) {
        paths.append(QDir::cleanPath(path));
      }
    }
  }
  paths.removeDuplicates();
  return paths;
}

void scanTree(const QString& root, bool flatpak, int depth, int* visited,
              QSet<QString>* seenPaths, QSet<QString>* seenIds, PspScanResult* result) {
  if (depth > 4 || *visited > 20000) {
    if (*visited > 20000) {
      result->incomplete = true;
    }
    return;
  }
  for (const QFileInfo& child :
       QDir(root).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    ++*visited;
    if (child.isFile()) {
      addGame(child.absoluteFilePath(), flatpak, seenPaths, seenIds, result);
    } else if (child.isDir()) {
      scanTree(child.absoluteFilePath(), flatpak, depth + 1, visited, seenPaths, seenIds, result);
    }
  }
}

void scanRoot(const QString& root, bool flatpak, QSet<QString>* seenPaths,
              QSet<QString>* seenIds, PspScanResult* result) {
  const QFileInfo info(root);
  if (!info.isDir()) {
    return;
  }
  result->roots.append(root);
  addGame(root, flatpak, seenPaths, seenIds, result);
  int visited = 0;
  scanTree(root, flatpak, 0, &visited, seenPaths, seenIds, result);
}
} // namespace

PspParamSfo PpssppScanner::readParamSfo(const QString& path) {
  if (path.isEmpty()) {
    return {};
  }
  bool okay = false;
  const QByteArray bytes = boundedRead(path, kMaximumSfoBytes, &okay);
  return okay ? parseParamSfo(bytes) : PspParamSfo{};
}

PspParamSfo PpssppScanner::readParamSfoFromIso(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QByteArray descriptor = isoRead(file, 16, kIsoSectorSize);
  if (descriptor.size() != kIsoSectorSize || descriptor.mid(1, 5) != QByteArrayLiteral("CD001")) {
    return {};
  }
  const QByteArray rootRecord = descriptor.mid(156, 34);
  const QByteArray gameDirectory = isoFindEntry(file, rootRecord, QStringLiteral("PSP_GAME"));
  if (gameDirectory.isEmpty()) {
    return {};
  }
  const QByteArray sfoRecord = isoFindEntry(file, gameDirectory, QStringLiteral("PARAM.SFO"));
  if (sfoRecord.size() < 34) {
    return {};
  }
  const quint32 lba = qFromLittleEndian<quint32>(
      reinterpret_cast<const uchar*>(sfoRecord.constData() + 2));
  const quint32 length = qFromLittleEndian<quint32>(
      reinterpret_cast<const uchar*>(sfoRecord.constData() + 10));
  if (length == 0 || length > kMaximumSfoBytes) {
    return {};
  }
  const QByteArray bytes = isoRead(file, lba, length);
  return bytes.size() == static_cast<int>(length) ? parseParamSfo(bytes) : PspParamSfo{};
}

PspParamSfo PpssppScanner::readParamSfoFromPbp(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() < 40) {
    return {};
  }
  const QByteArray header = file.read(40);
  if (!header.startsWith(QByteArrayLiteral("\x00PBP"))) {
    return {};
  }
  QVector<quint32> offsets;
  for (int index = 0; index < 8; ++index) {
    offsets.append(qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(header.constData() + 8 + index * 4)));
  }
  if (offsets.size() < 2 || offsets.at(0) < 40 || offsets.at(1) <= offsets.at(0) ||
      offsets.at(1) > file.size() || offsets.at(1) - offsets.at(0) > kMaximumSfoBytes ||
      !file.seek(offsets.at(0))) {
    return {};
  }
  const QByteArray bytes = file.read(offsets.at(1) - offsets.at(0));
  return bytes.size() == static_cast<int>(offsets.at(1) - offsets.at(0))
             ? parseParamSfo(bytes)
             : PspParamSfo{};
}

QStringList PpssppScanner::discoverRoots() {
  const QString config =
      QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
      QStringLiteral("/ppsspp");
  return {config, QDir::homePath() +
                      QStringLiteral("/.var/app/org.ppsspp.PPSSPP/config/ppsspp")};
}

bool PpssppScanner::ppssppInstalled() {
  for (const QString& binary :
       {QStringLiteral("PPSSPPSDL"), QStringLiteral("ppsspp"), QStringLiteral("PPSSPP"),
        QStringLiteral("ppsspp-qt")}) {
    if (!QStandardPaths::findExecutable(binary).isEmpty()) {
      return true;
    }
  }
  return flatpakAppInstalled(QStringLiteral("org.ppsspp.PPSSPP"));
}

PspScanResult PpssppScanner::scan(const QStringList& configRoots,
                                  const QStringList& userRoots) {
  PspScanResult result;
  QSet<QString> seenPaths;
  QSet<QString> seenIds;
  for (const QString& rawRoot : configRoots) {
    const QString root = QDir::cleanPath(rawRoot);
    if (!QFileInfo(root).isDir()) {
      continue;
    }
    const bool flatpak = root.contains(QStringLiteral("/.var/app/org.ppsspp.PPSSPP/"));
    const QStringList remembered = iniPaths(root);
    for (const QString& path : remembered) {
      if (QFileInfo(path).isDir()) {
        scanRoot(path, flatpak, &seenPaths, &seenIds, &result);
      } else {
        addGame(path, flatpak, &seenPaths, &seenIds, &result);
      }
    }
    if (!remembered.isEmpty()) {
      result.roots.append(root);
    }
  }
  for (const QString& rawRoot : userRoots) {
    const QString root = QDir::cleanPath(rawRoot);
    if (!QFileInfo(root).isDir()) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("PSP folder is unavailable: %1").arg(rawRoot));
      continue;
    }
    scanRoot(root, false, &seenPaths, &seenIds, &result);
  }
  result.roots.removeDuplicates();
  return result;
}
