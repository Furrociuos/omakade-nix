#pragma once

#include "tracking/ProcFs.h"

#include <QSet>
#include <QString>
#include <QVector>

// Emulator launch profiles drive session attribution. A profile names the
// binaries an emulator runs under; the matcher then looks for a command line
// argument that looks like a game image. That covers every launch path that
// names the game on the command line: Omakade launches, terminal launches, and
// wrapper scripts. Loading a game from inside the emulator's own file picker
// shows no path on the command line and stays untracked for now.
struct SessionProcessProfile {
  QString name;
  QStringList binaries;
  // Omakade source to ask for a rescan when a session of this emulator ends,
  // for emulators whose own playtime is only written on exit. Empty when the
  // source keeps itself current.
  QString rescanSource;
};

struct SessionMatch {
  qint64 pid = 0;
  qint64 procStart = -1;
  QString emulator;
  QString rescanSource;
  QString gamePath;
};

struct ProcessProfileSet {
  QVector<SessionProcessProfile> emulators;
  QSet<QString> romExtensions;
};

namespace ProcessMatcher {

// Reads a profiles JSON document: {"romExtensions": [...],
// "emulators": [{"name": "...", "binaries": [...], "rescanSource": "..."}]}.
// Returns an empty set and a non-empty error on malformed input.
[[nodiscard]] ProcessProfileSet load(const QString& path, QString* error = nullptr);

[[nodiscard]] QVector<SessionMatch> match(const QVector<ProcessSnapshot>& processes,
                                          const ProcessProfileSet& profiles);

} // namespace ProcessMatcher
