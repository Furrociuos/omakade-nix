#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// One DS game melonDS can open.
struct MelondsGameRecord {
  // The ROM's own four-character game code, which is what melonDS keys its own records by, or
  // "path:<file>" for a dump that has no usable code. The two forms never collide.
  QString gameId;
  QString gameCode;
  QString title;
  QString path;
  // "DS" or "DSi": the unit code in the header says which console the dump is for.
  QString platform;
  QString coverPath;
  bool dsi = false;
  bool dsiWare = false;
  // A dump whose ARM9 offset is inside the header is homebrew rather than a licensed release. It
  // is still a game the user can play, so it is imported and labelled instead of rejected.
  bool homebrew = false;
  bool flatpak = false;
  QString flatpakAppId;
};

struct MelondsScanResult {
  QVector<MelondsGameRecord> games;
  QStringList folders;
  QStringList warnings;
  bool incomplete = false;
};

// melonDS keeps no game library. It remembers recent files and the folder it last used, and its
// save and save state are files beside the ROM, so a DS game is discovered from the folders the
// user keeps ROMs in and named from the ROM header melonDS itself reads.
//
// Discovery is bounded: only the folders the caller passes are walked, and the walk stops at a
// file budget instead of running for as long as the tree is deep. Nothing here reads a home
// directory or a filesystem root on its own.
class MelondsScanner final {
public:
  // What melonDS will open as a game. Archives are deliberately absent: melonDS can read one
  // member out of an archive, but that needs the archive's own member list, which this source does
  // not read yet. An archive is reported rather than guessed at.
  [[nodiscard]] static QStringList gameExtensions();

  // The directories melonDS keeps its configuration in, in the order they take precedence: a
  // portable directory beside the binary wins, then the native configuration directory, then the
  // Flatpak application's own. The configuration file inside is melonDS.toml, or the older
  // melonDS.ini where the toml has not been written yet.
  [[nodiscard]] static QStringList discoverConfigRoots();

  [[nodiscard]] static bool melondsInstalled();

  // The fields of the ROM header this source needs. An invalid header means the file is not a DS
  // ROM: a save, a save state, or something else wearing the extension.
  struct Header {
    QString gameCode;
    QString title;
    bool dsi = false;
    bool dsiWare = false;
    bool homebrew = false;
    [[nodiscard]] bool valid() const { return !gameCode.isEmpty() || !title.isEmpty(); }
  };
  [[nodiscard]] static Header readHeader(const QString& path);

  [[nodiscard]] static MelondsScanResult scan(const QStringList& folders,
                                             const QStringList& configRoots = {});
};
