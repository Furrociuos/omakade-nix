#include "tracking/AttributionAdapter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>
#include <QStringList>

#include <utility>

#include <sys/stat.h>

namespace {

// Dolphin's file holds one line per distinct game ever played, so it stays small. The caps
// exist so a damaged file cannot make the recorder allocate without bound.
constexpr qint64 kMaximumFileBytes = 1024 * 1024;
constexpr int kMaximumEntries = 8192;
constexpr int kMaximumKeyLength = 64;

struct FileStat {
  quint64 device = 0;
  quint64 inode = 0;
  qint64 size = -1;
  qint64 mtimeMs = 0;
};

// What the file held when it was last read. size stays -1 until the first read, which is
// also what tells the first sight of a process apart from a later poll.
struct FileState {
  FileStat info;
  QHash<QString, quint64> totals;
};

bool statHeartbeat(const QString& path, FileStat* stat) {
  struct stat info {};
  if (::stat(QFile::encodeName(path).constData(), &info) != 0) {
    return false;
  }
  if (!S_ISREG(info.st_mode) || info.st_size < 0 || info.st_size > kMaximumFileBytes) {
    return false;
  }
  stat->device = static_cast<quint64>(info.st_dev);
  stat->inode = static_cast<quint64>(info.st_ino);
  stat->size = static_cast<qint64>(info.st_size);
  stat->mtimeMs =
      static_cast<qint64>(info.st_mtim.tv_sec) * 1000 + info.st_mtim.tv_nsec / 1000000;
  return true;
}

// TimePlayed.ini is an ordinary INI written by Dolphin's own writer: a [TimePlayed] section of
// "disc id = 0x<hex milliseconds>" lines. Only that section is read, because the same folder
// holds unrelated state, and an unparsable value is skipped rather than guessed at.
QHash<QString, quint64> parseTotals(const QByteArray& bytes) {
  QHash<QString, quint64> totals;
  bool inTimePlayed = false;
  int entries = 0;
  const QList<QByteArray> lines = bytes.split('\n');
  for (const QByteArray& rawLine : lines) {
    const QByteArray line = rawLine.trimmed();
    if (line.isEmpty() || line.startsWith(';') || line.startsWith('#')) {
      continue;
    }
    if (line.startsWith('[')) {
      inTimePlayed = line.compare(QByteArrayLiteral("[TimePlayed]"), Qt::CaseInsensitive) == 0;
      continue;
    }
    if (!inTimePlayed) {
      continue;
    }
    const qsizetype equals = line.indexOf('=');
    if (equals <= 0) {
      continue;
    }
    const QString key = QString::fromUtf8(line.left(equals)).trimmed();
    QString value = QString::fromUtf8(line.mid(equals + 1)).trimmed();
    if (key.isEmpty() || key.size() > kMaximumKeyLength || value.isEmpty()) {
      continue;
    }
    if (value.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
      value.remove(0, 2);
    }
    if (value.isEmpty() || value.size() > 16) {
      continue;
    }
    bool parsed = false;
    const quint64 total = value.toULongLong(&parsed, 16);
    if (!parsed) {
      continue;
    }
    if (++entries > kMaximumEntries) {
      break;
    }
    totals.insert(key, total);
  }
  return totals;
}

bool readHeartbeat(const QString& path, FileState* state) {
  FileStat info;
  if (!statHeartbeat(path, &info)) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  // Bounded: a short read of a small file, once per poll that the file actually moved.
  const QByteArray bytes = file.read(kMaximumFileBytes);
  file.close();
  state->info = info;
  state->totals = parseTotals(bytes);
  return true;
}

bool sameFile(const FileStat& left, const FileStat& right) {
  return left.device == right.device && left.inode == right.inode && left.size == right.size &&
         left.mtimeMs == right.mtimeMs;
}

struct ProcessState {
  FileState baseline;
  FileState lastRead;
  // The last game this process was attributed to. It stands while evidence is missing, stale
  // or refused, because none of those means the game changed.
  QString confirmed;
  qint64 lastSeen = 0;
};

class DolphinTimePlayed final : public AttributionAdapter::Adapter {
public:
  explicit DolphinTimePlayed(QString configRoot) : m_configRoot(std::move(configRoot)) {}

  [[nodiscard]] QString emulator() const override { return QStringLiteral("Dolphin"); }

  [[nodiscard]] AttributionAdapter::Result
  attribute(const QString& emulator, qint64 pid, qint64 procStart, qint64 nowWall,
            const AttributionAdapter::IdentityResolver& resolve) override {
    if (emulator != this->emulator() || pid <= 0 || procStart < 0 || !resolve) {
      return {};
    }
    const QString processKey = QStringLiteral("%1:%2").arg(pid).arg(procStart);
    auto state = m_processes.find(processKey);
    if (state == m_processes.end()) {
      if (m_processes.size() >= AttributionAdapter::kMaximumTrackedProcesses) {
        evictOldest();
      }
      state = m_processes.insert(processKey, ProcessState{});
    }
    state->lastSeen = nowWall;

    const QString path = heartbeatPath();
    FileStat info;
    if (path.isEmpty() || !statHeartbeat(path, &info)) {
      // No file to read: the emulator has never tracked a game, or the file is between
      // Dolphin's temporary write and its rename. Neither says the game changed.
      return confirmed(*state, true);
    }
    if (state->baseline.info.size < 0) {
      // First sight of this process. A cumulative total proves nothing on its own, so the
      // file's current contents become the baseline and this poll attributes nothing. The
      // game is attributed by the write that follows, which is Dolphin's own first heartbeat.
      if (!readHeartbeat(path, &state->lastRead)) {
        return {};
      }
      state->baseline = state->lastRead;
      return {};
    }
    if (nowWall - info.mtimeMs / 1000 > AttributionAdapter::kHeartbeatFreshnessSeconds) {
      // Dolphin has stopped writing: the game is paused, sitting in a menu, or the process
      // outlived it. The last confirmed game still stands, marked as not being refreshed,
      // because losing freshness is not the same as the game closing.
      return confirmed(*state, true);
    }
    if (!sameFile(info, state->lastRead.info) && !readHeartbeat(path, &state->lastRead)) {
      return confirmed(*state, true);
    }

    // One stat per poll, and the file is only parsed when it moved, so a running game costs a
    // stat and a small parse every 30 seconds rather than a parse per poll.
    //
    // Replacement is judged by content, not by the file's identity: Dolphin writes through a
    // temporary file and a rename, so the inode changes on every ordinary heartbeat too. Only a
    // file that lost data (a smaller key, or a shorter file) is a different record rather than
    // this one advancing.
    const FileState& current = state->lastRead;
    if (current.info.size < state->baseline.info.size ||
        anyTotalDecreased(current, state->baseline)) {
      // A shorter file, or one whose totals went backwards: a different record rather than this
      // one advancing, so it rebases silently and attributes nothing from it.
      state->baseline = current;
      return confirmed(*state, false);
    }

    QStringList grew;
    for (auto total = current.totals.cbegin(); total != current.totals.cend(); ++total) {
      if (total.value() > state->baseline.totals.value(total.key(), 0)) {
        grew.append(total.key());
      }
    }
    if (grew.isEmpty()) {
      // Fresh, but nothing advanced yet. Dolphin is still tracking the game it was already
      // attributed to.
      return confirmed(*state, false);
    }
    state->baseline = current;
    if (grew.size() > 1) {
      // Changing the game inside one process can flush the old game and start the new one
      // inside the same window. Guessing between them would credit play to the wrong game, so
      // this poll refuses and the next one, which sees only the new game advancing, resolves it.
      AttributionAdapter::Result result = confirmed(*state, false);
      result.refused = true;
      return result;
    }
    const QString gamePath = resolve(grew.first(), emulator);
    if (gamePath.isEmpty()) {
      // The library does not know this disc id, or knows it twice. That one advancing game is the
      // only evidence of what is loaded, so the previous confirmation is withdrawn rather than
      // kept: play that has moved on to a game this record cannot name must not be billed to the
      // game before it. Returning nothing also leaves the process to weaker evidence, which the
      // window title may still resolve.
      state->confirmed.clear();
      return {.gamePath = QString{}, .stale = false, .refused = true};
    }
    state->confirmed = gamePath;
    return {.gamePath = gamePath, .stale = false, .refused = false};
  }

private:
  [[nodiscard]] static bool anyTotalDecreased(const FileState& current, const FileState& baseline) {
    for (auto total = current.totals.cbegin(); total != current.totals.cend(); ++total) {
      const auto before = baseline.totals.constFind(total.key());
      if (before != baseline.totals.cend() && total.value() < before.value()) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] static AttributionAdapter::Result confirmed(const ProcessState& state, bool stale) {
    if (state.confirmed.isEmpty()) {
      // Nothing was ever confirmed for this process, so there is nothing to remember. An empty
      // result keeps the process unattributed rather than inventing a game for it.
      return {};
    }
    return {.gamePath = state.confirmed, .stale = stale, .refused = false};
  }

  // The tree that is running Dolphin is the one writing the record, so the newest record wins.
  // A leftover file in another tree must not shadow the live one: a native configuration left
  // behind by a Flatpak install (or the reverse) has an old modification time, and choosing it
  // would leave that install never attributed. Resolved on every poll, because which tree is
  // running can change while the daemon runs, and a tree with no record yet is watched so the
  // file is picked up as soon as the first game writes it.
  //
  // One adapter watches one record, so two Dolphin trees running at the same time are not
  // separated here: whichever record is newest supplies the evidence for both processes.
  [[nodiscard]] QString heartbeatPath() {
    QStringList roots;
    if (!m_configRoot.isEmpty()) {
      roots.append(m_configRoot);
    } else {
      const QString config =
          QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
      if (!config.isEmpty()) {
        roots.append(config + QStringLiteral("/dolphin-emu"));
      }
      roots.append(QDir::homePath() +
                   QStringLiteral("/.var/app/org.DolphinEmu.dolphin-emu/config/dolphin-emu"));
    }
    QString newest;
    qint64 newestMs = -1;
    QString existing;
    for (const QString& root : roots) {
      const QString candidate = root + QStringLiteral("/TimePlayed.ini");
      const QFileInfo info(candidate);
      if (info.isFile()) {
        const qint64 updated = info.lastModified().toMSecsSinceEpoch();
        if (updated > newestMs) {
          newestMs = updated;
          newest = candidate;
        }
      } else if (existing.isEmpty() && QFileInfo(root).isDir()) {
        existing = candidate;
      }
    }
    return newest.isEmpty() ? existing : newest;
  }

  // Bounded state: the least recently seen process gives way. Reaching the cap needs more
  // concurrent emulator processes than an ordinary session has, but the table cannot grow past
  // it, and a dropped entry costs one poll plus one heartbeat before attribution returns.
  void evictOldest() {
    auto oldest = m_processes.end();
    for (auto entry = m_processes.begin(); entry != m_processes.end(); ++entry) {
      if (oldest == m_processes.end() || entry->lastSeen < oldest->lastSeen) {
        oldest = entry;
      }
    }
    if (oldest != m_processes.end()) {
      m_processes.erase(oldest);
    }
  }

  QString m_configRoot;
  QHash<QString, ProcessState> m_processes;
};

} // namespace

namespace AttributionAdapter {

std::unique_ptr<Adapter> dolphinTimePlayed(const QString& configRoot) {
  return std::make_unique<DolphinTimePlayed>(configRoot);
}

Result resolveStoppedHeartbeat(const Result& result, const QString& titledGamePath) {
  if (!result.stale) {
    return result;
  }
  if (!titledGamePath.isEmpty() && titledGamePath == result.gamePath) {
    return result;
  }
  return {};
}

} // namespace AttributionAdapter
