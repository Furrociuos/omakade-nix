#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct PspParamSfo {
  QString title;
  QString discId;
  QString discVersion;
  QString region;

  [[nodiscard]] bool valid() const { return !title.isEmpty() || !discId.isEmpty(); }
};

struct PspGameRecord {
  QString gameId;
  QString title;
  QString path;
  QString discId;
  QString discVersion;
  QString region;
  QString coverPath;
  bool homebrew = false;
  bool flatpak = false;
};

struct PspScanResult {
  QVector<PspGameRecord> games;
  QStringList roots;
  QStringList warnings;
  bool incomplete = false;
};

// PPSSPP has no library index. It remembers recent and pinned paths in ppsspp.ini,
// while the games themselves live in the filesystem roots the user selected.
class PpssppScanner final {
public:
  [[nodiscard]] static QStringList discoverRoots();
  [[nodiscard]] static bool ppssppInstalled();
  [[nodiscard]] static PspParamSfo readParamSfo(const QString& path);
  [[nodiscard]] static PspParamSfo readParamSfoFromIso(const QString& path);
  [[nodiscard]] static PspParamSfo readParamSfoFromPbp(const QString& path);

  [[nodiscard]] static PspScanResult scan(const QStringList& configRoots,
                                           const QStringList& userRoots = {});
};
