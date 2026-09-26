# Live launch, recording, and save recovery

The real-emulator workflow passed on September 12, 2026. It also exposed and fixed
an already-selected backup list failing to refresh after automatic capture.

## What ran

- Omakade's actual GameLauncher and SaveBackups implementations.
- A RomM installation context, explicitly configured for native RetroArch/Snes9x.
  Real-server catalog/credential integration was verified separately in
  [the RomM report](ROMM-LIVE-ACCEPTANCE-2026-09-12.md).
- `/usr/bin/retroarch` and `/usr/lib/libretro/snes9x_libretro.so`.
- A generated, disposable 32 KiB SNES test ROM that renders green and increments
  its first SRAM byte on boot. No commercial ROM or personal save was used.
- The installed `omakade-sessiond`, with a separate config and SQLite database.
- A temporary wrapper adding only the emulator's test config, frame limit, log,
  and screenshot arguments. It executes the real emulator and core.

## Verified results

| Check | Result |
| --- | --- |
| Saved explicit launch setup resolves to the actual core | Pass |
| Omakade starts the actual emulator from a RomM installation context | Pass |
| Automatic save copy completes before emulator start | Pass |
| Already-selected backup list immediately shows the capture | Pass after fix |
| Restore refused while the emulator is running | Pass |
| Screenshot center is the ROM's expected RGB `(0, 255, 0)` | Pass |
| Emulator writes a 2,048-byte SRAM file, first byte `0 → 1` | Pass |
| Restore reproduces the original all-zero save byte for byte | Pass |
| Undo reproduces the emulator-written save byte for byte | Pass |
| Reopened backup manager retains the versions | Pass |
| Recorder stores a closed session for the exact content path | Pass, RetroArch, 4 seconds |

The live Qt test passed in 10.038 seconds. Recorded elapsed time is based on
process polling, not the core's emulated frame duration; this short frame-limited
run is not a benchmark for playtime precision or long-session stability.

## Fix

`SaveBackups::protectLaunch` already created and verified the backup, then emitted
`changed`. Its selected `versions` list still held the previous data. It now reloads
that list before notifying the UI. The ordinary fixture regression selects the
save context before capture and checks the updated list without reselecting it.
The optional real-emulator test additionally verifies the complete recovery flow.

Implementation candidate: `aaabe0615ac808291ab5c01cdf5eb06f1efb008a`.
All **246 CTest checks passed** in **384.73 seconds**, with no QML runtime errors
found in the log. The capped Release build, staged/installed startup checks,
desktop-entry validation, and offline AppStream validation passed.

Installed app and recorder: `~/.local/lib/omakade/aaabe0615ac8-1.9-save-refresh/`.
The executable links and running recorder path were verified. Previous candidate
`3afe8190c90f-1.9-library-cleanup`, settings, and a consistent SQLite backup are
preserved in `rollback/`. Close Omakade and emulators before running that folder's
`restore-app.py` with Python to restore the previous binaries without replacing
current library data. The rollback database integrity check passed.

App SHA-256: `e5ea6395cf1f3d6a314463ef82f57ae8c364f910841ab235e5f429fdcab166f4`.
Recorder SHA-256: `abc00b729d09b5dd728dc638b28f256ef25c07a2024af858dd618691294c2a67`.

## Isolation and remaining acceptance

The normal recorder was paused for the controlled emulator runs and restored afterward.
The isolated run's core options, saves, backups, history, and database stayed under
`/tmp/omakade-live-game`. The first preflight used the normal RetroArch history path;
its QA history entry was removed. A QA recorder row created during an early failed
attempt was also removed. The final check confirmed no QA session in personal history.

This establishes real execution, save writes, session closure, restore, and undo for
the tested core. It does not certify every emulator, commercial game, controller,
audio path, or long-running gameplay session. The run was intentionally silent.

The opt-in test is `FeatureWorkflowTests::liveEmulatorRecordingAndSaveRecovery`.
Ordinary CTest skips it unless `OMAKADE_LIVE_GAME_ROOT=/tmp/omakade-live-game` is set.
It requires the disposable ROM/config/save fixture, a PATH wrapper invoking the real
RetroArch with a bounded frame count, isolated XDG data/config paths, and
`OMAKADE_LIVE_RECORDER` pointing to the matching recorder binary. Never point the
fixture at personal saves. Local evidence is in `build/live-game/`.
