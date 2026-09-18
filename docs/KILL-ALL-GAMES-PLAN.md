# Stopping a running game

Design note for issue #53 ("Kill All Games"). No code yet. The question is how
Omakade can stop a game that is still running, including the ones it never
started itself, across every supported source.

## The problem

Games leave processes behind after they appear to close. Battle.net in
particular looks closed while its processes are still alive. The reporter asks
for a button like Faugus Launcher's skull that kills what the app launched, and
correctly suspects it may not be possible because Omakade hands the launch to
another launcher and then gets out of the way.

## What the app can already attribute

Most of what this feature needs exists, because play-session tracking already
had to answer "which process is this game".

| Piece | What it provides |
| --- | --- |
| `src/tracking/ProcFs.{h,cpp}` | A process snapshot of `pid`, `procStart` (procfs stat field 22, the pid-reuse guard), `comm` and full `arguments`, plus `processAlive(pid, procStart)` |
| `src/tracking/ProcessMatcher.{h,cpp}` | Attributes a live process to a game. An emulator profile names the binaries and the matcher finds a ROM path on the command line using `romExtensions`. Returns `{pid, procStart, emulator, gamePath}` |
| `src/tracking/SessionRecorder.cpp` | Already "adopts survivors still running the same game" across restarts, so it can list live processes per session |
| `src/launch/GameLauncher.cpp` | Tracks the pid of anything launched directly, paired with the process start time |
| `src/library/GameRoles.h` | Per-game `installPath`, `flatpak`, `appId`, `source`, `runner` |
| `src/sources/FlatpakInstall.h` | Resolves flatpak app ids against the system and user install roots |

What does not exist anywhere in the tree today: any kill, terminate,
`wineserver` or `flatpak kill` call. This is new work, not a rework.

## The gap

`GameLauncher::trackProcess` records nothing when the process it started has
already exited, with the comment "Already gone: a launcher stub that handed off
and exited". That is the whole handoff case: for `steam://` and for Battle.net,
the pid we would own is a stub, and the real game is a child of another daemon.
Attribution therefore cannot come from ownership alone.

## The kill ladder

For a given game, targets are collected in order, narrowest first. Every rung
records why the target was included, and unattributable leftovers are reported
rather than guessed at.

| Source | Identity we hold | Mechanism | Confidence |
| --- | --- | --- | --- |
| Direct launches (native, manual games) | tracked pid | terminate the tracked pid | exact |
| Any Wine or Proton game: Heroic, Lutris, Faugus, Battle.net, Steam Proton | `installPath`, prefix list | `WINEPREFIX=<that prefix> wineserver -k` | exact for the prefix |
| Flatpak | app id | `flatpak kill <appId>` | first class |
| Emulators: Cemu, Dolphin, PCSX2, RetroArch, retro, Ryujinx, shadPS4 | profile binary plus ROM path, already matched | terminate the matched emulator process, which is the session | exact |
| Steam through `steam://` | `appId`, `installPath` | match processes whose exe or command line sits under the game's install directory, then terminate | good, needs the Proton `compatdata` prefix derived |
| Battle.net handoffs | `installPath`, prefix list | prefix kill, plus install-path match | good |
| Anti-cheat and services started outside both the prefix and the install directory | nothing | none. Report as unattributable | known gap |

Two things this table makes obvious:

1. The emulator half is nearly free. The profile data that drives session
   tracking is the same data a kill needs.
2. The prefix lever is the one that answers the reported complaint. Killing a
   Wine prefix takes that prefix's whole process tree, which is exactly the
   lingering Battle.net processes.

## Mechanisms in detail

**Owned launches.** Terminate the tracked pid after re-checking
`ProcFs::processAlive(pid, procStart)` so a recycled pid can never be signalled.

**Wine prefixes.** `WINEPREFIX=<game prefix> wineserver -k`. The caveat is
shared fate: Battle.net and its games normally share one prefix, so this closes
Battle.net as well. That should be labelled as such in the UI rather than
hidden, and it may deserve to be a separate action ("stop the game" versus
"quit the launcher") rather than a side effect.

**Install-path matching.** `ProcFs::listProcesses()` plus a check that the
resolved executable or command line lives under the game's `installPath`. This
covers Steam Proton and native handoffs, Heroic, Lutris and native games without
owning a pid, and it is the only mechanism that spans a launcher handoff.

**Flatpak.** `flatpak kill <appId>`, scoped by construction.

**Emulators.** The matched emulator process is the session. Killing it closes
the game, which can lose unsaved progress, so it needs a clear warning and
probably a confirmation.

**Optional, for owned launches only.** Launch inside a transient user scope
(`systemd-run --user --scope --unit=omakade-<appid>`) and later kill the scope.
That captures every descendant the game spawns without pid guessing, but it
changes the launch path and is a separate slice.

## Safety rules

- The start-time guard is mandatory before signalling anything.
- Terminate gracefully first, then escalate to SIGKILL after a timeout. For
  prefixes, `wineserver -k` is the graceful step.
- Never target pid 1, the shell, sessiond, Omakade itself, or the launcher,
  unless the user explicitly asked to quit the launcher.
- Show the exact targets, by name and count, before acting, and log what was
  signalled and why.
- The kill layer takes an explicit target list, so attribution can be tested
  against snapshots without killing anything.

## Non-goals

- No blanket `pkill`, and no name-based kill lists. Both are brittle and can
  take out the wrong process.
- No "kill everything that looks like a game" as the primary action. A global
  action should run the same ladder over known sessions, and show the list
  first.
- No silent launcher shutdown as a side effect of stopping a game.

## Proposed slicing

1. Attribution and kill service in `src/tracking`, with tests over process
   snapshots. No UI.
2. The emulator, flatpak and prefix levers.
3. Steam install-path matching, including deriving the Proton `compatdata`
   prefix.
4. UI: per-game stop, plus the global stop-all action with a confirmation that
   lists targets.
5. Optional: scope-based ownership for owned launches.

## Status

Slice 1, attribution, is implemented on `codex/stop-games` and covered by
`tests/GameStopTests.cpp`. The ladder runs over a process snapshot and returns an
ordered target list with a reason per target, the processes it refused with the
reason it refused them, and the gaps it cannot cover. No signal is sent and no
surface has changed.

Decisions that moved while building it:

- The slice delivers attribution for the whole ladder rather than one lever. The
  four fixtures the slice is accepted on (a Steam Proton game, a Battle.net
  prefix, a flatpak app, an emulator) each need their own rung to answer, so all
  of them are in the service. Slice 2 is what sends the signals.
- Targets are ordered the way they will be applied: an owned process, then a Wine
  prefix, then a flatpak app, then an emulator session, then install-path matches.
  A process a prefix target already closes is recorded as skipped with that
  reason instead of being listed twice.
- `ProcFs` now resolves `/proc/<pid>/exe` into the snapshot. Install-path matching
  needs the executed file as well as the command line, and the resolver also
  strips the kernel's " (deleted)" suffix.
- The prefix rung takes no evidence from the snapshot. The prefix is identity the
  library already holds, and `wineserver -k` against a prefix with nothing
  running is a no-op, so "is this game running" is deliberately not answered
  here. The caller decides that before it offers a stop, which makes it the UI
  slice's job.
- The shared-prefix consequence is stated on the target itself: every process in
  the prefix closes, including any launcher that shares it.
- A root that is a whole system folder (/, /home, the home directory itself,
  /usr, /tmp and the like) is refused as an install folder or a prefix, with a
  note explaining why. A scan that fills in one of those would otherwise turn a
  per-game stop into a session-wide kill list.

Limits this slice proved rather than assumed:

- A Wine process reports the wine loader as its executable and a Windows path on
  its command line, so install-path matching catches the wrapper chain that
  carries the unix path (Proton, native games) and not the wine-side children.
  Where the game has a prefix, those are covered by the prefix rung instead.
  `battleNetGameClosesItsSharedPrefix` asserts the honest outcome: one prefix
  target and no per-process targets for processes showing only Windows paths.
- Nothing walks the parent chain. A process a launcher handed off to another
  daemon, with no path under the prefix or the install folder, stays
  unattributable, and the plan says so in a note. Parent attribution is the
  obvious next mechanism and needs a field the snapshot does not carry today.
- Flatpak is fixture-only so far. This machine has no `flatpak` binary and no
  install roots, so `flatpak kill` cannot be exercised here at all.

### Slice 2: the levers

`src/tracking/GameStopper.{h,cpp}`, covered by `tests/GameStopperTests.cpp`.
Signals the targets a plan lists: a tracked pid, a Wine prefix, a flatpak app.

Decisions that moved while building it:

- Every signal goes through a `SignalSink`, and the tests substitute a fake. That
  is what keeps the rule "no test may signal a real process outside a fixture"
  true by construction rather than by discipline.
- The stop is two phases, `begin()` then `escalate()`. `begin()` sends the
  graceful step and `escalate()` forces whatever is still alive, so the wait
  between them belongs to the caller. Nothing in the layer blocks a thread, the
  grace period is one stated constant (`Stopper::gracePeriodMs()`, 5000 ms, which
  answers the open question below), and a test drives both phases with no timing.
- A prefix with no live wineserver is nothing to do, not an error. Wine's own
  server confirms the mapping: `-k` reaches `kill_lock_owner` and `main` exits
  `!ret` (`server/main.c`), so exit 1 means no server was running for that
  prefix.
- Escalating a prefix is `wineserver -k9`, which sends SIGKILL where plain `-k`
  sends SIGINT and then SIGKILL (wineserver(1), `-k[n]`).
- The forced step re-reads the process start time before signalling, so a pid
  reused during the grace period is refused rather than killed. A target with no
  start time is refused outright, at the stopper as well as at attribution.
- `flatpak kill` stops a running instance by app id. What flatpak does when the
  app is not running is not documented, so its own message is passed through to
  the user instead of being classified.
- The log and the on-screen lines come from one report: what was signalled, why it
  was attributed, and the tool's own words, with a failure logged as a warning.
  The two cannot drift apart because they are the same strings.

Limits this slice proved rather than assumed:

- No lever here has run against a real target. This machine has no `wine`,
  `wineserver` or `flatpak` at all, so the wine and flatpak arms are exercised
  against the fake only, and the exit-code mapping for `wineserver` rests on
  Wine's source rather than on a local run. That is the first thing to retest on
  a machine with wine installed.
- The pid-1 refusal in the real sink is verified by reading, not by breaking it:
  sabotaging that guard would make the suite call `kill(1, ...)` for real.

Still open: no UI surface, no per-game or global control, and the levers have not
been run against a real running game.

## Open questions

- Which launcher the reporter actually hits this with. It sets the order of
  slices 2 and 3, not the design.
- Whether "stop game" and "quit the launcher" are one action or two when they
  share a Wine prefix.
- Whether the global action should include sessions that were started outside
  Omakade but are attributable.
- How long to wait between the graceful signal and escalation. Answered for now:
  5000 ms, `GameStop::Stopper::gracePeriodMs()`.
