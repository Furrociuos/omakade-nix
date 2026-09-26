#include "sources/rpcs3/Rpcs3Scanner.h"

#include "sources/FlatpakInstall.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QtEndian>
#include <limits>

namespace {
constexpr qint64 kMaximumSfoBytes = 4 * 1024 * 1024;
constexpr qint64 kMaximumYamlBytes = 4 * 1024 * 1024;
constexpr int kMaximumSfoEntries = 4096;
constexpr quint16 kPsfUtf8 = 0x0204;
constexpr quint16 kPsfUtf16 = 0x0202;
constexpr quint16 kPsfUtf8Legacy = 0x0004;
constexpr int kIsoSectorSize = 2048;

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

QString yamlScalar(QString value) {
  value = value.trimmed();
  if (value.isEmpty()) {
    return {};
  }
  if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')) &&
      value.size() >= 2) {
    const QByteArray array = QByteArrayLiteral("[") + value.toUtf8() + QByteArrayLiteral("]");
    const QJsonArray parsed = QJsonDocument::fromJson(array).array();
    return parsed.size() == 1 ? parsed.first().toString() : QString{};
  }
  if (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')) &&
      value.size() >= 2) {
    value = value.mid(1, value.size() - 2);
    value.replace(QStringLiteral("''"), QStringLiteral("'"));
    return value;
  }
  // An unquoted YAML comment starts after whitespace. A path containing '#' is therefore
  // still preserved; RPCS3's emitter quotes values when it needs to.
  const int comment = value.indexOf(QRegularExpression(QStringLiteral("\\s+#")));
  if (comment >= 0) {
    value.truncate(comment);
  }
  return value.trimmed();
}

QHash<QString, QString> readYamlMap(const QString& path, bool* okay) {
  *okay = false;
  const QByteArray bytes = boundedRead(path, kMaximumYamlBytes, okay);
  if (!*okay) {
    return {};
  }
  QHash<QString, QString> values;
  static const QRegularExpression separator(QStringLiteral("^\\s*([^:#][^:]*)\\s*:\\s*(.*)$"));
  for (QString line : QString::fromUtf8(bytes).split(QLatin1Char('\n'))) {
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('#')) ||
        line == QStringLiteral("---") || line == QStringLiteral("...")) {
      continue;
    }
    const QRegularExpressionMatch match = separator.match(line);
    if (!match.hasMatch()) {
      continue;
    }
    const QString key = yamlScalar(match.captured(1));
    if (!key.isEmpty()) {
      values.insert(key, yamlScalar(match.captured(2)));
    }
  }
  *okay = true;
  return values;
}

QString expandVariables(QString value, const QString& emulatorDir) {
  value.replace(QStringLiteral("$(EmulatorDir)"), emulatorDir);
  value.replace(QStringLiteral("$HOME"), QDir::homePath());
  value.replace(QStringLiteral("${HOME}"), QDir::homePath());
  value.replace(QStringLiteral("$XDG_CONFIG_HOME"),
                QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
  value.replace(QStringLiteral("${XDG_CONFIG_HOME}"),
                QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
  if (value.startsWith(QStringLiteral("~/"))) {
    value.replace(0, 1, QDir::homePath());
  }
  return value;
}

QString expandedPath(QString value, const QString& emulatorDir, const QString& base = {}) {
  value = expandVariables(value.trimmed(), emulatorDir);
  if (value.isEmpty()) {
    return {};
  }
  if (!QFileInfo(value).isAbsolute() && !base.isEmpty()) {
    value = base + QLatin1Char('/') + value;
  }
  return QDir::cleanPath(value);
}

bool insideDirectory(const QString& path, const QString& directory) {
  if (path.isEmpty() || directory.isEmpty()) {
    return false;
  }
  const QString cleanPath = QDir::cleanPath(path);
  const QString cleanDirectory = QDir::cleanPath(directory);
  return cleanPath == cleanDirectory ||
         cleanPath.startsWith(cleanDirectory + QLatin1Char('/'));
}

bool acceptedCategory(const QString& category) {
  static const QSet<QString> accepted{
      QStringLiteral("DG"), QStringLiteral("HG"), QStringLiteral("1P"), QStringLiteral("2P"),
      QStringLiteral("2G"), QStringLiteral("PP"), QStringLiteral("MN"), QStringLiteral("PE")};
  return accepted.contains(category.toUpper());
}

QString firstBootPath(const QString& directory) {
  for (const QString& relative : {QStringLiteral("/PS3_GAME/USRDIR/EBOOT.BIN"),
                                  QStringLiteral("/USRDIR/EBOOT.BIN"),
                                  QStringLiteral("/EBOOT.BIN"),
                                  QStringLiteral("/USRDIR/ISO.BIN.EDAT")}) {
    const QString candidate = directory + relative;
    if (QFileInfo(candidate).isFile()) {
      return QDir::cleanPath(candidate);
    }
  }
  const QDir dir(directory);
  const QFileInfoList variants = dir.entryInfoList({QStringLiteral("PS3_GM*")}, QDir::Dirs,
                                                   QDir::Name);
  for (const QFileInfo& variant : variants) {
    const QString candidate = variant.absoluteFilePath() + QStringLiteral("/USRDIR/EBOOT.BIN");
    if (QFileInfo(candidate).isFile()) {
      return QDir::cleanPath(candidate);
    }
  }
  return {};
}

QString sfoPathForDirectory(const QString& directory) {
  for (const QString& candidate :
       {directory + QStringLiteral("/PARAM.SFO"),
        directory + QStringLiteral("/PS3_GAME/PARAM.SFO")}) {
    if (QFileInfo(candidate).isFile()) {
      return candidate;
    }
  }
  return {};
}

QString coverFor(const QString& configRoot, const QString& titleId, const QString& gamePath) {
  if (!titleId.isEmpty()) {
    const QDir icons(configRoot + QStringLiteral("/Icons/game_icons/") + titleId);
    const QStringList matches =
        icons.entryList({QStringLiteral("ICON0.PNG"), QStringLiteral("ICON0.png"),
                         QStringLiteral("ICON0.jpg"), QStringLiteral("ICON0.jpeg")},
                        QDir::Files, QDir::Name);
    if (!matches.isEmpty()) {
      return icons.filePath(matches.constFirst());
    }
  }
  const QFileInfo game(gamePath);
  const QString stem = game.isDir() ? game.absoluteFilePath() : game.absolutePath() + '/' +
                                                                  game.completeBaseName();
  for (const QString& suffix :
       {QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg")}) {
    if (QFileInfo::exists(stem + suffix)) {
      return stem + suffix;
    }
  }
  return {};
}

Rpcs3ParamSfo parseParamSfo(const QByteArray& bytes) {
  Rpcs3ParamSfo result;
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
    if (keyEnd < 0) {
      return {};
    }
    const QString key = QString::fromUtf8(
        bytes.mid(static_cast<int>(keyStart), static_cast<int>(keyEnd - keyStart)));
    const qint64 valueStart = static_cast<qint64>(dataTable) + dataOffset;
    if (valueStart < dataTable || length > static_cast<quint32>(bytes.size()) ||
        valueStart + length > bytes.size()) {
      return {};
    }
    QString value;
    if (format == kPsfUtf8 || format == kPsfUtf8Legacy) {
      value = QString::fromUtf8(
          bytes.mid(static_cast<int>(valueStart), static_cast<int>(length)));
    } else if (format == kPsfUtf16) {
      QString decoded;
      for (quint32 offset = 0; offset + 1 < length; offset += 2) {
        const quint16 code = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar*>(bytes.constData() + valueStart + offset));
        if (code == 0) {
          break;
        }
        decoded.append(QChar(code));
      }
      value = decoded;
    } else {
      continue;
    }
    const int nul = value.indexOf(QChar::Null);
    value = nul < 0 ? value : value.left(nul);
    if (key == QStringLiteral("TITLE")) {
      result.title = value;
    } else if (key == QStringLiteral("TITLE_ID")) {
      result.titleId = value.toUpper();
    } else if (key == QStringLiteral("CATEGORY")) {
      result.category = value.toUpper();
    } else if (key == QStringLiteral("APP_VER") || key == QStringLiteral("VERSION")) {
      result.appVersion = value;
    } else if (key == QStringLiteral("SAVEDATA_DIRECTORY")) {
      result.savedataDirectory = value;
    }
  }
  result.title = result.title.trimmed();
  result.titleId = result.titleId.trimmed().toUpper();
  result.category = result.category.trimmed().toUpper();
  result.savedataDirectory = result.savedataDirectory.trimmed();
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

void addGame(const QString& path, const QString& yamlTitleId, bool installed,
             const QString& configRoot, const QString& hdd0, bool flatpak,
             QSet<QString>* seenPaths, QHash<QString, int>* byId, Rpcs3ScanResult* result) {
  const QFileInfo info(path);
  if (!info.exists()) {
    return;
  }
  QString titleId = yamlTitleId.trimmed().toUpper();
  QString title;
  QString category;
  QString launchTarget = QDir::cleanPath(info.absoluteFilePath());

  if (info.isDir()) {
    const QString sfoPath = sfoPathForDirectory(info.absoluteFilePath());
    const Rpcs3ParamSfo sfo = Rpcs3Scanner::readParamSfo(sfoPath);
    if (!sfo.titleId.isEmpty()) {
      titleId = sfo.titleId.toUpper();
    }
    title = sfo.title.trimmed();
    category = sfo.category.trimmed().toUpper();
    const QString boot = firstBootPath(info.absoluteFilePath());
    if (!boot.isEmpty()) {
      launchTarget = boot;
    }
  } else if (info.suffix().compare(QStringLiteral("iso"), Qt::CaseInsensitive) == 0) {
    const Rpcs3ParamSfo sfo = Rpcs3Scanner::readParamSfoFromIso(info.absoluteFilePath());
    if (!sfo.titleId.isEmpty()) {
      titleId = sfo.titleId.toUpper();
    }
    title = sfo.title.trimmed();
    category = sfo.category.trimmed().toUpper();
  } else {
    title = info.completeBaseName();
  }
  if (titleId.isEmpty() && yamlTitleId.isEmpty()) {
    return;
  }

  if (titleId.isEmpty()) {
    result->warnings.append(QStringLiteral("PARAM.SFO has no TITLE_ID: %1").arg(path));
    return;
  }
  if (!category.isEmpty() && !acceptedCategory(category)) {
    result->warnings.append(
        QStringLiteral("RPCS3 category %1 is not a game: %2").arg(category, path));
    return;
  }
  if (insideDirectory(path, hdd0) && !insideDirectory(path, hdd0 + QStringLiteral("/game")) &&
      !installed) {
    result->warnings.append(QStringLiteral("Refused dev_hdd0 path outside game/: %1").arg(path));
    return;
  }
  if (info.isDir() && launchTarget == QDir::cleanPath(info.absoluteFilePath()) &&
      firstBootPath(info.absoluteFilePath()).isEmpty() && !installed) {
    result->warnings.append(QStringLiteral("RPCS3 game has no bootable file: %1").arg(path));
    return;
  }

  const QString cleanedPath = QDir::cleanPath(info.isDir() ? launchTarget : info.absoluteFilePath());
  if (seenPaths->contains(cleanedPath)) {
    return;
  }
  if (title.isEmpty()) {
    title = info.completeBaseName().isEmpty() ? titleId : info.completeBaseName();
  }
  Rpcs3GameRecord record{.gameId = titleId,
                         .titleId = titleId,
                         .title = title,
                         .path = cleanedPath,
                         .launchTarget = launchTarget,
                         .category = category,
                         .coverPath = coverFor(configRoot, titleId, cleanedPath),
                         .installed = installed,
                         .flatpak = flatpak};
  const auto found = byId->constFind(titleId);
  if (found != byId->cend()) {
    if (installed && !result->games.at(*found).installed) {
      result->games[*found] = record;
    }
    return;
  }
  byId->insert(titleId, result->games.size());
  result->games.append(record);
  seenPaths->insert(cleanedPath);
}

void scanExternalRoot(const QString& root, bool markerRequired, const QString& configRoot,
                      const QString& hdd0, bool flatpak, QSet<QString>* seenPaths,
                      QHash<QString, int>* byId, Rpcs3ScanResult* result) {
  const QFileInfo info(root);
  if (!info.isDir()) {
    return;
  }
  if (markerRequired &&
      !QFileInfo(root + QStringLiteral("/Disc Games Can Be Put Here For Automatic Detection.txt"))
           .isFile()) {
    return;
  }
  result->roots.append(root);
  addGame(root, {}, false, configRoot, hdd0, flatpak, seenPaths, byId, result);
  const QFileInfoList children =
      QDir(root).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
  for (const QFileInfo& child : children) {
    addGame(child.absoluteFilePath(), {}, false, configRoot, hdd0, flatpak, seenPaths, byId,
            result);
  }
}
} // namespace

Rpcs3ParamSfo Rpcs3Scanner::readParamSfo(const QString& path) {
  if (path.isEmpty()) {
    return {};
  }
  bool okay = false;
  const QByteArray bytes = boundedRead(path, kMaximumSfoBytes, &okay);
  return okay ? parseParamSfo(bytes) : Rpcs3ParamSfo{};
}

Rpcs3ParamSfo Rpcs3Scanner::readParamSfoFromIso(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QByteArray descriptor = isoRead(file, 16, kIsoSectorSize);
  if (descriptor.size() != kIsoSectorSize || descriptor.mid(1, 5) != QByteArrayLiteral("CD001")) {
    return {};
  }
  const QByteArray rootRecord = descriptor.mid(156, 34);
  const QByteArray gameDirectory = isoFindEntry(file, rootRecord, QStringLiteral("PS3_GAME"));
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
  return bytes.size() == static_cast<int>(length) ? parseParamSfo(bytes) : Rpcs3ParamSfo{};
}

QStringList Rpcs3Scanner::discoverConfigRoots() {
  const QString home = QDir::homePath();
  QStringList roots;
  bool portable = false;
  for (const QString& binary :
       {QStringLiteral("rpcs3"), QStringLiteral("RPCS3"),
        QStringLiteral("rpcs3.AppImage")}) {
    const QString executable = QStandardPaths::findExecutable(binary);
    if (executable.isEmpty()) {
      continue;
    }
    const QString candidate = QFileInfo(executable).absolutePath() + QStringLiteral("/portable");
    if (QFileInfo(candidate).isDir()) {
      roots.append(candidate);
      portable = true;
      break;
    }
  }
  if (!portable) {
    const QString config =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
        QStringLiteral("/rpcs3");
    roots.append(config);
  }
  roots.append(home + QStringLiteral("/.var/app/net.rpcs3.RPCS3/config/rpcs3"));
  roots.removeDuplicates();
  return roots;
}

Rpcs3VfsPaths Rpcs3Scanner::resolvePaths(const QString& rawRoot) {
  Rpcs3VfsPaths paths;
  paths.configRoot = QDir::cleanPath(rawRoot);
  if (paths.configRoot.isEmpty() || !QFileInfo(paths.configRoot).isDir()) {
    return paths;
  }
  paths.flatpak = insideDirectory(paths.configRoot,
                                  QDir::homePath() +
                                      QStringLiteral("/.var/app/net.rpcs3.RPCS3/"));
  QHash<QString, QString> vfs;
  if (QFileInfo::exists(paths.configRoot + QStringLiteral("/vfs.yml"))) {
    bool okay = false;
    vfs = readYamlMap(paths.configRoot + QStringLiteral("/vfs.yml"), &okay);
    if (!okay) {
      return paths;
    }
  }
  paths.emulatorDir =
      expandedPath(vfs.value(QStringLiteral("$(EmulatorDir)")), paths.configRoot,
                   paths.configRoot);
  if (paths.emulatorDir.isEmpty()) {
    paths.emulatorDir = paths.configRoot;
  }
  paths.hdd0 = expandedPath(vfs.value(QStringLiteral("/dev_hdd0")), paths.emulatorDir,
                            paths.emulatorDir);
  if (paths.hdd0.isEmpty()) {
    paths.hdd0 = paths.emulatorDir + QStringLiteral("/dev_hdd0");
  }
  paths.flash = expandedPath(vfs.value(QStringLiteral("/dev_flash")), paths.emulatorDir,
                             paths.emulatorDir);
  const QString gamesValue = vfs.value(QStringLiteral("games_dir"));
  paths.gamesDirExplicit = !gamesValue.trimmed().isEmpty();
  paths.gamesDir = expandedPath(
      paths.gamesDirExplicit ? gamesValue : QStringLiteral("$(EmulatorDir)games"),
      paths.emulatorDir, paths.emulatorDir);
  return paths;
}

bool Rpcs3Scanner::rpcs3Installed() {
  return !QStandardPaths::findExecutable(QStringLiteral("rpcs3")).isEmpty() ||
         !QStandardPaths::findExecutable(QStringLiteral("RPCS3")).isEmpty() ||
         !QStandardPaths::findExecutable(QStringLiteral("rpcs3.AppImage")).isEmpty() ||
         flatpakAppInstalled(QStringLiteral("net.rpcs3.RPCS3"));
}

Rpcs3ScanResult Rpcs3Scanner::scan(const QStringList& configRoots,
                                   const QStringList& userRoots) {
  Rpcs3ScanResult result;
  QSet<QString> seenPaths;
  QHash<QString, int> byId;
  for (const QString& rawRoot : configRoots) {
    const Rpcs3VfsPaths paths = resolvePaths(rawRoot);
    const QString root = paths.configRoot;
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
      continue;
    }
    const bool flatpak = paths.flatpak;
    const QString effectiveEmulatorDir = paths.emulatorDir;
    const QString hdd0 = paths.hdd0;
    const QString flash = paths.flash;

    bool gamesOkay = false;
    const QHash<QString, QString> games =
        readYamlMap(root + QStringLiteral("/games.yml"), &gamesOkay);
    if (!gamesOkay && QFileInfo::exists(root + QStringLiteral("/games.yml"))) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Could not read %1/games.yml").arg(root));
    } else {
      for (auto it = games.cbegin(); it != games.cend(); ++it) {
        const QString path = expandedPath(it.value(), effectiveEmulatorDir, effectiveEmulatorDir);
        if (!path.isEmpty() && (flash.isEmpty() || !insideDirectory(path, flash))) {
          addGame(path, it.key(), false, root, hdd0, flatpak, &seenPaths, &byId, &result);
        }
      }
    }

    const QDir installed(hdd0 + QStringLiteral("/game"));
    if (installed.exists()) {
      result.roots.append(root);
      for (const QFileInfo& entry : installed.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                            QDir::Name)) {
        addGame(entry.absoluteFilePath(), {}, true, root, hdd0, flatpak, &seenPaths, &byId,
                &result);
      }
    }

    const QString gamesDir = paths.gamesDir;
    if (!gamesDir.isEmpty()) {
      scanExternalRoot(gamesDir, !paths.gamesDirExplicit, root, hdd0, flatpak, &seenPaths,
                       &byId, &result);
    }
  }

  for (const QString& rawRoot : userRoots) {
    const QString root = QDir::cleanPath(rawRoot);
    if (!QFileInfo(root).isDir()) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("PS3 folder is unavailable: %1").arg(rawRoot));
      continue;
    }
    const QString configRoot = QDir::homePath() + QStringLiteral("/.config/rpcs3");
    const QString hdd0 = QDir::homePath() + QStringLiteral("/.config/rpcs3/dev_hdd0");
    scanExternalRoot(root, false, configRoot, hdd0, false, &seenPaths, &byId, &result);
  }
  result.roots.removeDuplicates();
  return result;
}
