# Recording coverage

Profiles recognize emulator processes and game paths in command-line arguments, and
on Hyprland also match an emulator's window title against titles Omakade already
knows. A listed profile is not evidence that every launcher, wrapper, or internal
game change works. Recording does not inject code, control emulators, or detect pauses.

| Area | Automated evidence | Remaining acceptance |
| --- | --- | --- |
| Ryujinx and Eden | Direct ROM arguments, paths with spaces, missing-path rejection | Exact candidate launch, return, and short-session accounting |
| melonDS | Direct NDS/SRL/DSI/IDS argument, scanner identity, cache-to-unified-library filtering, and a native melonDS 1.1 launch of the MIT-licensed DS-Craft beta 1.7.1 homebrew ROM that closed as one 129-second session over a 130-second wall span | Flatpak launch and a game change made inside melonDS's own file picker |
| Other shipped profiles | Shared matcher and profile parsing | Real launch arguments for RetroArch, PCSX2, Dolphin, Cemu, shadPS4 and other listed binaries |
| Window-title attribution | Cache reading, exact and whole-name matching, ambiguity and short-name refusal, decorated titles, malformed compositor answers, and a live Hyprland run that recorded and closed a session for a process naming no game path | The same run against a real emulator and a real ROM on each supported compositor |
| Dolphin attribution record | Reading a heartbeat the running emulator rewrites, refusing a record that has not advanced, following a game change inside one process, refusing an unknown disc id and an ambiguous pair, and a stopped heartbeat keeping the game already confirmed | A real Dolphin session that loads a second game from Dolphin's own file picker, on a native and on a Flatpak install |
| Pause on unfocus | Focus parsing, the never-treat-unknown-as-unfocused rule, billing and pausing across polls with a controlled clock, and a live Hyprland run where an unfocused game billed 0s against a control's 40s | The same run with a game that loses and regains focus mid-session |
| Session accounting | Monotonic duration, game changes in supplied snapshots, restart recovery, baseline overlap, write failure | Compare displayed time with a real short session |
| Durable recovery | Bounded journal framing, owner-bound unresolved identity, first-read outage recovery, replace isolation, replay after restart and commit without acknowledgment, dead/changed/same-process reconciliation, retryable final close, deletion before replay, replacement sync-failure probes, capacity, corruption and unavailable storage | A real crash during a refused write on a supported host |
| Recorder ownership | Private daemon start, duplicate rejection, termination and restart | Installed service behavior on supported hosts |
| Imported totals | Source parser and baseline fixtures | Updated emulator formats and unusual library layouts |
| Discord Rich Presence | Frame encoding, handshake and command payloads, socket search including sandboxed clients, and a real round trip over a live socket covering the handshake, a command, connection reuse and clearing | A real Discord or Vesktop client showing the presence for a real game |

The earlier local Ryujinx launch/return produced a closed session. That is
historical evidence, not acceptance of the current candidate. Game tests remain
paused at the maintainer's request. The Z-A freezes have not been attributed to
Omakade, LSFG, or focus changes.

New configurations default to recording off. Existing explicit choices persist;
legacy configurations without the key retain the previous enabled default.
The preference controls recording and whether recorded totals contribute to the
display. Turning it off keeps the stored history. Daemon status is checked against
the owner of this library's recorder lock and the live process executable. It
reports a running process, not a guarantee that a particular game is recognized.

A write the database refuses is recorded in a bounded journal beside the database and
replayed after a recorder restart under the session's stable identity, so an exit during a
storage failure delays a session rather than losing it. Replay is idempotent. A journal at
capacity refuses new records and reports it; a corrupt journal is set aside once and
replaced. Unknown ownership is preserved and reported until it can be resolved or is provably
invalid; Replace restore invalidates prior work. Replacement synchronizes temporary content,
rename and parent-directory persistence and reports any failed step. A full or read-only
filesystem can still defeat the database and the journal together, so zero loss is not
promised when no durable destination accepts a write.

Paused emulator time counts while its process remains matched. Imported and
recorded totals are reconciled per game: recorded time never lowers a total, and
play recorded since the emulator's counter was last seen is added, because that
counter cannot already include it. A session the emulator later writes into its
own counter is not counted twice. Deleting a game's history takes the removed time off
the watermark, so a cleared game still shows later play rather than staying stuck at its
imported figure. A counter is not observed while a session for that
game is still open, since the emulator writes its counter on exit and the recorder
closes the session a few seconds later: observing in that window would credit the
session's not-yet-flushed time twice. Discord Rich Presence is off by default, publishes
the game name, source, start time and running-game count through the local Discord client.
It needs an application id in the config key
`discord_client_id` or the `OMAKADE_DISCORD_CLIENT_ID` environment variable, which
wins. A Discord that is not running is not an error and never disturbs recording.
Loading games internally without a
recognizable command-line path is covered on Hyprland when the emulator's window
title names the game and Omakade already knows that title, and stays uncovered
otherwise. Dolphin additionally records the game it is running while it runs: its
playtime file is rewritten every thirty seconds, keyed by disc id, through a
temporary file and a rename. That is the only record among the supported emulators
regenerated by the running game itself, so a Dolphin game loaded from Dolphin's own
file picker is attributed from it when the command line names nothing. The record is
read for a live Dolphin process only: a total that has not advanced since the process
was first seen attributes nothing, a disc id the library does not know is refused
rather than guessed at, two games advancing between two polls is refused for that
poll and resolved by the next one, and a record that stopped advancing keeps the game
it last confirmed instead of ending the session. Paused emulator time counts while its
process remains matched, so a game left on Dolphin's own menu keeps billing, which is
existing behavior rather than a property of the record. No other supported emulator
writes a comparable record, so PPSSPP, melonDS, RetroArch, Cemu, shadPS4, Ryujinx,
Eden and Xenia keep the command-line and window-title paths. Three limits of the
record path are known and unexercised: a game is attributed from the emulator's first
write, so a picker session shorter than that write and with no resolving title is not
recorded at all; only one record is watched, so two Dolphin trees running at the same
time share one source of evidence; and after a recorder restart a picker-attributed
row is closed at its last heartbeat and a second row opens when the record advances
again, because the recorded process identity of such a row is the one a title match
carries. ARM64 hardware acceptance remains separate from cross-architecture
build and package checks.
