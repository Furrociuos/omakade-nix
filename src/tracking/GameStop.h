#pragma once

#include "tracking/ProcFs.h"
#include "tracking/ProcessMatcher.h"

#include <QString>
#include <QStringList>
#include <QVector>

// Attribution for stopping a running game (issue #53). This layer answers "what
// would we signal, and why" from a process snapshot, and signals nothing at all:
// the target list it returns is the input to the kill ladder, which keeps the
// attribution testable without a real process being touched.
namespace GameStop {

// The scope a target closes. The two scope kinds hand the job to the tool that
// owns the scope; the rest are one process each.
enum class TargetKind {
  TerminateProcess,
  WinePrefix,
  FlatpakApp,
};

enum class Confidence {
  // Omakade started it, or the scope names the game exactly.
  Exact,
  // Attributed by path or prefix. Can miss a process a launcher handed off.
  Good,
};

// Why a process that looked like a candidate is not in the target list.
enum class SkipReason {
  // pid 1, a shell, the recorder, Omakade itself, anything in the guard list.
  Protected,
  // The pid is alive but its procfs start time differs: a different process.
  RecycledPid,
  // Already closed by a prefix target earlier in the same plan.
  CoveredByPrefix,
  // The process matched the game's scope but is not the game (an emulator
  // running a different ROM), or the identity gives nothing to match on.
  MissingScope,
};

// A process Omakade itself started, paired with its procfs start time so a
// reused pid cannot be signalled by mistake.
struct OwnedProcess {
  qint64 pid = 0;
  qint64 procStart = -1;
};

// Everything the library already knows about one game. Every field is optional;
// what is set decides which mechanisms can attribute anything.
struct GameIdentity {
  QString title;
  QString appId;
  QString source;
  QString runner;
  QString installPath;
  bool flatpak = false;
  // The flatpak application the game runs inside, e.g. org.libretro.RetroArch.
  QString flatpakAppId;
  // Wine prefixes this game lives in, system path form.
  QStringList winePrefixes;
  // Launches Omakade started itself and still tracks.
  QVector<OwnedProcess> ownedProcesses;
  // Content paths recorded for this game, used to claim an emulator session.
  QStringList gamePaths;
  // Emulator profile name, when the source is an emulator.
  QString emulator;
};

// Processes that must never be signalled. An empty list still carries the
// defaults, so a caller that forgets cannot weaken the guard.
struct Guards {
  QVector<qint64> protectedPids;
  QStringList protectedBinaries;
};

struct Target {
  TargetKind kind = TargetKind::TerminateProcess;
  Confidence confidence = Confidence::Exact;
  // Set for TerminateProcess.
  qint64 pid = 0;
  qint64 procStart = -1;
  QString comm;
  // Set for WinePrefix.
  QString prefix;
  // Set for FlatpakApp.
  QString appId;
  // Short user-facing name for what closes, e.g. "pcsx2-qt (pid 8112)".
  QString label;
  // Why this target is in the list, in a sentence a user can read.
  QString reason;
  // Exact scope members observed before confirmation; a replacement is never escalated.
  QVector<OwnedProcess> witnesses;
};

struct Skipped {
  qint64 pid = 0;
  qint64 procStart = -1;
  QString comm;
  SkipReason reason = SkipReason::MissingScope;
  QString detail;
};

struct Plan {
  // Ordered the way the ladder applies them: owned processes, Wine prefixes,
  // flatpak apps, emulator sessions, then install-path matches. A target that
  // has already died by the time it is reached is checked again before it is
  // signalled, so a later duplicate is a no-op rather than a second kill.
  QVector<Target> targets;
  QVector<Skipped> skipped;
  // Plain sentences for the confirmation: what this plan does not cover.
  QStringList notes;

  [[nodiscard]] bool isEmpty() const { return targets.isEmpty(); }
  // Targets that close one process.
  [[nodiscard]] int processTargets() const;
  // Targets that close a whole prefix or sandbox.
  [[nodiscard]] int scopeTargets() const;
};

// The built-in guard list. Callers add their own pid and the recorder's.
[[nodiscard]] Guards defaultGuards();

// Attributes game's processes inside processes. Signals nothing.
[[nodiscard]] Plan plan(const GameIdentity& game, const QVector<ProcessSnapshot>& processes,
                        const ProcessProfileSet& profiles, const Guards& guards = {});

// One line naming a target, e.g. "Close every process in the Wine prefix /p".
[[nodiscard]] QString describe(const Target& target);

// One line per target, for the confirmation that lists what will be signalled.
[[nodiscard]] QStringList describe(const Plan& plan);

[[nodiscard]] QString skipReasonText(SkipReason reason);

} // namespace GameStop
