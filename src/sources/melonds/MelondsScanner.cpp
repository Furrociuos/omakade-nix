#include "sources/melonds/MelondsScanner.h"

#include "sources/FlatpakInstall.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QtEndian>

namespace {
constexpr qint64 kHeaderBytes = 0x300;
constexpr qint64 kMaximumHeaderRead = 512 * 1024;
// The walk is bounded by files visited rather than by depth, so a folder that contains a huge tree
// stops the scan with a warning instead of running for minutes.
constexpr int kMaximumFilesVisited = 20000;

constexpr int kTitleOffset = 0x000;
constexpr int kTitleLength = 12;
constexpr int kGameCodeOffset = 0x00C;
constexpr int kGameCodeLength = 4;
constexpr int kUnitCodeOffset = 0x012;
constexpr int kArm9RomOffset = 0x020;
constexpr int kDsiTitleIdHighOffset = 0x234;
// A licensed DS release puts its ARM9 code past the header. A smaller offset means the dump is
// homebrew, which is a game the user can play and is labelled rather than dropped.
constexpr quint32 kMinimumLicensedArm9Offset = 0x4000;
constexpr quint32 kDsiWareTitleIdHigh = 0x00030004;

QString cleanHeaderText(const QByteArray& bytes) {
  QString text = QString::fromLatin1(bytes);
  for (QChar& character : text) {
    if (character.unicode() < 0x20 || character.unicode() == 0x7f) {
      character = QLatin1Char(' ');
    }
  }
  return text.simplified();
}

bool looksLikeGameCode(const QString& code) {
  if (code.size() != kGameCodeLength) {
    return false;
  }
  for (const QChar character : code) {
    if (!character.isLetterOrNumber()) {
      return false;
    }
  }
  return true;
}

QString displayTitle(const QString& fileName) {
  static const QRegularExpression parenthesized(QStringLiteral("\\([^)]*\\)"));
  static const QRegularExpression bracketed(QStringLiteral("\\[[^\\]]*\\]"));
  QString title = QFileInfo(fileName).completeBaseName();
  title.remove(parenthesized);
  title.remove(bracketed);
  return title.simplified();
}

QString sidecarCover(const QString& romPath) {
  const QFileInfo rom(romPath);
  const QString stem = rom.absolutePath() + QLatin1Char('/') + rom.completeBaseName();
  for (const QString& extension :
       {QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg")}) {
    if (QFileInfo::exists(stem + extension)) {
      return stem + extension;
    }
  }
  return {};
}

bool hasGameExtension(const QString& fileName) {
  const QString suffix = QFileInfo(fileName).suffix();
  for (const QString& extension : MelondsScanner::gameExtensions()) {
    if (suffix.compare(extension, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

bool archiveExtension(const QString& fileName) {
  const QString suffix = QFileInfo(fileName).suffix().toLower();
  for (const QString& extension :
       {QStringLiteral("zip"), QStringLiteral("7z"), QStringLiteral("rar"), QStringLiteral("tar")}) {
    if (suffix == extension) {
      return true;
    }
  }
  return false;
}

QString flatpakAppId() { return QStringLiteral("net.kuribo64.melonDS"); }
} // namespace

QStringList MelondsScanner::gameExtensions() {
  return {QStringLiteral("nds"), QStringLiteral("srl"), QStringLiteral("dsi"),
          QStringLiteral("ids")};
}

QStringList MelondsScanner::discoverConfigRoots() {
  QStringList roots;
  // A portable directory next to the binary wins over the user configuration directory, which is
  // what melonDS itself checks first.
  for (const QString& binary : {QStringLiteral("melonDS"), QStringLiteral("melonds")}) {
    const QString executable = QStandardPaths::findExecutable(binary);
    if (executable.isEmpty()) {
      continue;
    }
    const QString portable = QFileInfo(executable).absolutePath() + QStringLiteral("/portable");
    if (QFileInfo(portable).isDir()) {
      roots.append(portable);
    }
  }
  const QString config =
      QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  if (!config.isEmpty()) {
    roots.append(config + QStringLiteral("/melonDS"));
  }
  roots.append(QDir::homePath() +
               QStringLiteral("/.var/app/net.kuribo64.melonDS/config/melonDS"));
  QStringList unique;
  for (const QString& root : roots) {
    if (!root.isEmpty() && !unique.contains(root)) {
      unique.append(root);
    }
  }
  return unique;
}

bool MelondsScanner::melondsInstalled() {
  return !QStandardPaths::findExecutable(QStringLiteral("melonDS")).isEmpty() ||
         !QStandardPaths::findExecutable(QStringLiteral("melonds")).isEmpty() ||
         flatpakAppInstalled(flatpakAppId());
}

MelondsScanner::Header MelondsScanner::readHeader(const QString& path) {
  Header header;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return header;
  }
  const QByteArray bytes = file.read(kHeaderBytes);
  file.close();
  if (bytes.size() < kHeaderBytes) {
    // A DS ROM carries the header melonDS reads in its first bytes. A file that stops short of the
    // fields below is a save, a state, or something else wearing the extension.
    return header;
  }
  header.title = cleanHeaderText(bytes.mid(kTitleOffset, kTitleLength));
  const QString code =
      QString::fromLatin1(bytes.mid(kGameCodeOffset, kGameCodeLength)).toUpper();
  const quint8 unitCode = static_cast<quint8>(bytes.at(kUnitCodeOffset));
  header.dsi = (unitCode & 0x02) != 0;
  header.gameCode = looksLikeGameCode(code) ? code : QString{};
  const quint32 arm9Offset =
      qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + kArm9RomOffset));
  header.homebrew = arm9Offset < kMinimumLicensedArm9Offset || code == QStringLiteral("####");
  if (header.dsi) {
    const quint32 titleIdHigh = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(bytes.constData() + kDsiTitleIdHighOffset));
    header.dsiWare = titleIdHigh == kDsiWareTitleIdHigh;
  }
  // A save file can be long enough to hold these offsets, so the header only counts when it names
  // a game: a title or a game code. Anything else is refused rather than imported as a blank row.
  if (header.title.isEmpty() && header.gameCode.isEmpty()) {
    return Header{};
  }
  return header;
}

MelondsScanResult MelondsScanner::scan(const QStringList& folders,
                                       const QStringList& configRoots) {
  Q_UNUSED(configRoots);
  MelondsScanResult result;
  const bool flatpak =
      QStandardPaths::findExecutable(QStringLiteral("melonDS")).isEmpty() &&
      QStandardPaths::findExecutable(QStringLiteral("melonds")).isEmpty() &&
      flatpakAppInstalled(flatpakAppId());
  QSet<QString> seenPaths;
  int visited = 0;
  for (const QString& folder : folders) {
    const QFileInfo info(folder);
    if (!info.isDir() || !info.isReadable()) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("ROM folder is unavailable: %1").arg(folder));
      continue;
    }
    result.folders.append(folder);
    QDirIterator iterator(folder, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
      const QString filePath = iterator.next();
      if (++visited > kMaximumFilesVisited) {
        result.incomplete = true;
        result.warnings.append(
            QStringLiteral("Stopped after %1 files in %2").arg(kMaximumFilesVisited).arg(folder));
        return result;
      }
      if (archiveExtension(filePath)) {
        result.warnings.append(QStringLiteral("Archive is not scanned yet: %1").arg(filePath));
        continue;
      }
      if (!hasGameExtension(filePath)) {
        continue;
      }
      const QString canonical = QFileInfo(filePath).canonicalFilePath().isEmpty()
                                    ? QDir::cleanPath(filePath)
                                    : QFileInfo(filePath).canonicalFilePath();
      if (seenPaths.contains(canonical) || QFileInfo(canonical).size() > kMaximumHeaderRead) {
        continue;
      }
      const Header header = readHeader(canonical);
      if (!header.valid()) {
        // A save, a save state, or a file that only looks like a ROM. The extension is not
        // evidence that melonDS can open it.
        result.warnings.append(QStringLiteral("Not a DS ROM: %1").arg(canonical));
        continue;
      }
      QString title = header.title;
      if (title.isEmpty()) {
        title = displayTitle(canonical);
      }
      result.games.append(MelondsGameRecord{.gameId = header.gameCode.isEmpty()
                                                         ? QStringLiteral("path:%1").arg(canonical)
                                                         : header.gameCode,
                                            .gameCode = header.gameCode,
                                            .title = title,
                                            .path = canonical,
                                            .platform = header.dsi ? QStringLiteral("DSi")
                                                                   : QStringLiteral("DS"),
                                            .coverPath = sidecarCover(canonical),
                                            .dsi = header.dsi,
                                            .dsiWare = header.dsiWare,
                                            .homebrew = header.homebrew,
                                            .flatpak = flatpak,
                                            .flatpakAppId = flatpak ? flatpakAppId() : QString{}});
      seenPaths.insert(canonical);
    }
  }
  return result;
}
