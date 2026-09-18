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
  A process matched by the install-path rung that already sits inside a targeted
  prefix is recorded as skipped with that reason rather than listed twice. An
  owned pid inside a targeted prefix is still its own target, because that rung
  runs before the prefixes are collected, so the dedupe covers one rung and not
  the other; the execution re-checks liveness, which makes the later signal a
  no-op rather than a second close.
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
  true by construction rather than by discipline. The sink is a lever, not a
  safety boundary: the start-time guard and the guard lists live in `Stopper`, so
  a direct sink call bypasses them, which the header now says outright.
- The stop is two phases, `begin()` then `escalate()`. `begin()` sends the
  graceful step and `escalate()` forces whatever is still alive, so the wait
  between them belongs to the caller. The grace period is one stated constant
  (`Stopper::gracePeriodMs()`, 5000 ms, which answers the open question below),
  and a test drives both phases with no timing. The tool levers (`wineserver`,
  `flatpak`) are run synchronously and can hold the calling thread for up to 15
  seconds each, so they must not be called on the interface thread. That is a
  slice 4 requirement, not something this layer hides.
- A prefix with no live wineserver is nothing to do, not an error. Wine's own
  server confirms the mapping: `-k` reaches `kill_lock_owner` and `main` exits
  `!ret` (`server/main.c`), so exit 1 means no server was running for that
  prefix.
- Escalating a prefix is `wineserver -k9`, which sends SIGKILL where plain `-k`
  sends SIGINT and then SIGKILL (wineserver(1), `-k[n]`).
- The forced step re-reads the process start time before signalling, so a pid
  reused during the grace period is refused rather than killed. A target with no
  start time is refused outright, at the stopper as well as at attribution.
- `flatpak kill` stops a running instance by app id, and the sink reports a
  non-zero exit as a failure carrying flatpak's own message. flatpak exits
  non-zero for an app that is not running too, so the ordinary "already closed"
  case reads as a failure. The message to match cannot be verified on this
  machine (no flatpak), so it is left as a failure with flatpak's own words
  rather than guessed at.
- A shared directory is refused as an install folder, in addition to the whole
  system roots. This is a real case rather than a hypothetical: a Heroic sideload
  with no `folder_name` stores the *directory* of its executable as the install
  path, so an entry pointing at a launcher stores `/usr/bin`, and a manual
  library entry stores the program's own path, which is a file and not a folder
  at all. Either one would have attributed everything running from that directory
  to the game. `anInstallPathThatNamesAProgramOrSharedBinariesIsRefused` pins
  both.
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

### Slice 3: the Proton prefix

`SteamScanner::protonPrefixPath()` and `SteamScanner::protonPrefix()` derive
`<library>/steamapps/compatdata/<appId>/pfx` from the install path a Steam row
already holds, so the stop path needs no new role and no extra scan.

Decisions that moved while building it:

- The install-path rung itself landed in slice 1, because the acceptance fixtures
  needed it. What was left is the derivation, and it runs the other way: the
  library is whatever precedes `/steamapps/` in the install path, so nothing has
  to carry the Steam library path separately.
- `protonPrefix()` answers with the prefix only when Steam has created it. A game
  never run through Proton has no prefix, and naming one would be a target that
  closes nothing.
- The app id becomes a path segment in the derivation, so it is accepted only
  when it is all digits, anchored at both ends. Anything else derives nothing.
- The limit slice 1 recorded is what this closes: wine-side children that show
  only Windows paths stay unattributable by path, and the derived prefix covers
  them instead. `derivedProtonPrefixBecomesAPrefixTarget` asserts that
  composition end to end.

Not done: no UI, and the derivation has not been driven from the app against the
maintainer's own Steam library.

### Slice 4, the service half

`src/library/GameStopService.{h,cpp}`, covered by
`tests/GameStopServiceTests.cpp`. This is the piece the interface calls: a
library row in, a described target list out, and a stop that runs off the
calling thread.

Decisions that moved while building it:

- The row to identity mapping lives in one function, `identityFor`, and is
  asserted per source because it is the part that can be wrong. A Battle.net row
  keeps its prefix in the launch target, a Steam row derives it from the install
  path, and an emulator row's content path is its install path.
- Flatpak app ids are read from the runner role where a source stores one there
  (Ryujinx, shadPS4, Dolphin), and otherwise from the same ids `GameLauncher`
  starts, so a stop closes the app the launch opened. A source with no constant
  id offers no flatpak target rather than a guess.
- The stop runs in `QtConcurrent`, with the levers, the wait and the escalation
  inside the worker. The tests assert the levers ran on another thread, which is
  the property the reviews asked for and the reason the interface cannot freeze.
- Every process target is re-read after the stop, and the report says "closed" or
  "is still running" from that read rather than from the tool's exit code. A
  process that survives fails the stop, so a green message cannot mean an
  unfinished job.
- `liveGames()` carries the whole library row through with the lines it computed,
  so the caller can hand it straight back to `stop()` and get the same identity.
  Its first draft returned only display fields, which would have made the global
  action stop nothing.
- No step here depends on the recorder: a game is offered a stop when
  attribution finds something, not when a session row says it is running. That
  keeps the interface honest about what it can actually close.

Still to come in this slice: the QML control and confirmation, the global action,
and the render overlays in desktop and couch mode.

## Review findings, and what remains open

Two read-only reviews ran against these three slices (no build, no suite, every
claim a code-path claim). What they found that this branch does not fix, worst
first:

- The prefix lever needs a host `wineserver` on PATH. A Bottles or Proton prefix
  is launched through `bottles-cli` or `umu-run`, and neither puts `wineserver`
  there, so for exactly the launchers that hand off, the lever answers
  "wineserver is not installed" and the lingering processes survive. This is the
  reported complaint's only mechanism, so it needs a machine with wine before the
  feature can be called done.
- A flatpak-hosted prefix has its wine inside a sandbox with a private `/tmp`,
  which is where Wine keys the server socket. The host `wineserver` then exits 1,
  which this code reads as "no wineserver was reachable" and reports as nothing to
  do, with no escalation. A silent miss on a running game, and unverifiable here.
- A flatpak app id names the launcher or emulator, not the game: `flatpak kill
  com.valvesoftware.Steam` closes the client and any other game inside it. The
  target's own line says the sandbox and anything else in it closes, but the
  non-goal against a silent launcher shutdown deserves an explicit decision on
  whether this action is "stop the game" or a labelled "quit the launcher".
- Protected process names come from `argv[0]`, which the launcher chooses, so
  only `Guards.protectedPids` is authoritative. Slice 4 has to pass Omakade's own
  pid and the recorder's pid in.
- Nothing re-reads the process table after a lever, so a stop the tool reported as
  successful is not verified to have closed anything. A post-check belongs with
  the UI slice.
- A caller that runs `begin()` and never schedules `escalate()` gets a report
  reading "asked it to close". `pending()` is the only signal it forgot, and the
  escalation state is a single slot, so a second game's stop discards the first.
- An owned process is caller-asserted: the plan prints "Omakade started this
  process" for whatever pid the identity builder supplies, so that builder has to
  be right.

Also noted, lower: the levers resolve their tool through PATH rather than a fixed
path; an emulator game whose recorded content path is shared with another record
would claim that other session; and every path comparison is lexical, so a root
that is a symlink is judged by the link's own text rather than where it points
(and a symlinked install folder misses its own processes, because a process's
executable path is kernel-resolved). Nothing here follows the parent chain
either, so a handoff to another daemon with no path under the prefix or install
folder stays unattributable.

## Open questions

- Which launcher the reporter actually hits this with. It sets the order of
  slices 2 and 3, not the design.
- Whether "stop game" and "quit the launcher" are one action or two when they
  share a Wine prefix.
- Whether the global action should include sessions that were started outside
  Omakade but are attributable.
- How long to wait between the graceful signal and escalation. Answered for now:
  5000 ms, `GameStop::Stopper::gracePeriodMs()`.
