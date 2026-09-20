#pragma once

#include <QString>

#include <functional>
#include <memory>

// Reads an emulator's own records to answer which game it has loaded right now.
//
// Command-line matching proves a game only when the emulator was handed the game as an
// argument. Several emulators also load a game from their own file picker, and that process
// then names nothing: a window title is one weak hint, and this is the stronger one, because
// it reads a file the emulator rewrites while the game actually runs.
//
// The contract every adapter here keeps:
//
//  - A recents list is not evidence. It says a game was launched at some point, not that it
//    is loaded now, and it is never read here.
//  - Attribution needs a live process identity plus evidence written during this session, so
//    a stale file, another user's file, or a record left by a previous run attributes nothing.
//  - Weak evidence never replaces strong evidence. A command line that names the game wins,
//    and an adapter is only ever asked about a process that has no such match.
//  - A paused emulator stops writing. Losing freshness is not the game closing, so the last
//    confirmed game is kept and marked stale rather than used to end a session. The caller is
//    expected to check a stopped heartbeat against weaker evidence before it keeps billing:
//    see the daemon's attribution resolver.
//  - A confirmation is withdrawn when the game has plainly moved on and the new one cannot be
//    named, so play is never credited to the game before it.
//  - Nothing here grants authority over a process. Attribution names a game; the stop path
//    keeps its own verified identity rules and must not read this.
namespace AttributionAdapter {

// One attribution attempt. An empty gamePath means no attribution, and a caller must keep
// whatever attribution it already had instead of clearing it.
struct Result {
  QString gamePath;
  // This is the last confirmed game rather than fresh evidence, which is what a paused
  // emulator looks like. The game is still the one running.
  bool stale = false;
  // Fresh evidence existed and was refused. An ambiguous pair (two games advancing in one poll)
  // keeps the previous attribution for that poll only. A single game that advanced and cannot be
  // named withdraws it instead: the play has moved on, and crediting it to the game before would
  // be worse than crediting it to nothing.
  bool refused = false;
  [[nodiscard]] bool attributed() const { return !gamePath.isEmpty(); }
};

// Maps an emulator's own identity for a game (a disc id, a title id) to the content path the
// library records sessions against. An empty answer means unknown or ambiguous, and the
// attribution is refused rather than guessed.
using IdentityResolver =
    std::function<QString(const QString& identity, const QString& emulator)>;

class Adapter {
public:
  virtual ~Adapter() = default;
  // The emulator profile name this adapter answers for. A call for another emulator, another
  // process, or a process whose start time is unknown attributes nothing.
  [[nodiscard]] virtual QString emulator() const = 0;
  // The game this live process is running, or an empty result. nowWall is the wall clock the
  // caller observed the process at, which is what file freshness is measured against. A call
  // naming another emulator, another process, or a process whose start time is unknown
  // attributes nothing, so a caller may offer every emulator process to every adapter.
  [[nodiscard]] virtual Result attribute(const QString& emulator, qint64 pid, qint64 procStart,
                                         qint64 nowWall, const IdentityResolver& resolve) = 0;
};

// Dolphin rewrites its TimePlayed.ini from a thread that runs while emulation is Running, once
// every 30 seconds and immediately on a state change, through a temporary file and an atomic
// rename. That makes the file a heartbeat for the game currently loaded, and the strongest
// evidence available for any of the supported emulators: everything else writes its
// recent-games list at exit or at content close.
//
// The value is a cumulative total per disc id, so it is not evidence on its own. The first
// sight of a Dolphin process records the file's contents as a baseline and attributes nothing,
// and a later poll only attributes a game whose total grew since that baseline. configRoot
// overrides the searched Dolphin configuration folders, which is how the tests point it at a
// fixture instead of the real tree.
[[nodiscard]] std::unique_ptr<Adapter> dolphinTimePlayed(const QString& configRoot = {});

// 30 seconds between Dolphin's writes, plus room for one missed tick.
inline constexpr qint64 kHeartbeatFreshnessSeconds = 60;
// Concurrent Dolphin processes are rare, and the state is per process, so it is bounded rather
// than left to grow for the life of the daemon.
inline constexpr int kMaximumTrackedProcesses = 64;

} // namespace AttributionAdapter
