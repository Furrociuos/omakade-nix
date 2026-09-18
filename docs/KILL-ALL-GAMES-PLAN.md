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

## Open questions

- Which launcher the reporter actually hits this with. It sets the order of
  slices 2 and 3, not the design.
- Whether "stop game" and "quit the launcher" are one action or two when they
  share a Wine prefix.
- Whether the global action should include sessions that were started outside
  Omakade but are attributable.
- How long to wait between the graceful signal and escalation.
