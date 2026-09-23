#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// The fields Omakade reads from a PSF/PARAM.SFO file. RPCS3 uses the same file for
// installed games and for each savedata directory, so the parser is shared by discovery
// and the per-game save resolver.
struct Rpcs3ParamSfo {
  QString title;
  QString titleId;
  QString category;
  QString appVersion;
  QString savedataDirectory;

  [[nodiscard]] bool valid() const {
    return !title.isEmpty() || !titleId.isEmpty() || !savedataDirectory.isEmpty();
  }
};

struct Rpcs3GameRecord {
  QString gameId;  // TITLE_ID, the stable identity used by RPCS3.
  QString titleId;
  QString title;
  QString path;         // Bootable path used for session attribution.
  QString launchTarget; // Path or %RPCS3_GAMEID% token passed to rpcs3.
  QString category;
  QString coverPath;
  bool installed = false;
  bool flatpak = false;
};

struct Rpcs3ScanResult {
  QVector<Rpcs3GameRecord> games;
  QStringList roots;
  QStringList warnings;
  bool incomplete = false;
};

struct Rpcs3VfsPaths {
  QString configRoot;
  QString emulatorDir;
  QString hdd0;
  QString flash;
  QString gamesDir;
  bool gamesDirExplicit = false;
  bool flatpak = false;
};

// RPCS3 persists a bounded index of external games in games.yml, installed games below
// dev_hdd0/game, and the auto-detection root in vfs.yml. This scanner reads only those
// locations and the explicit PS3 ROM folders supplied by the caller.
class Rpcs3Scanner final {
public:
  [[nodiscard]] static QStringList discoverConfigRoots();
  [[nodiscard]] static bool rpcs3Installed();
  [[nodiscard]] static Rpcs3ParamSfo readParamSfo(const QString& path);
  [[nodiscard]] static Rpcs3ParamSfo readParamSfoFromIso(const QString& path);
  [[nodiscard]] static Rpcs3VfsPaths resolvePaths(const QString& configRoot);

  [[nodiscard]] static Rpcs3ScanResult scan(const QStringList& configRoots,
                                             const QStringList& userRoots = {});
};
