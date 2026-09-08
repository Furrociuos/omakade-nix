# Local feature quality candidate

Branch: `codex/feature-quality-local`
Base: `033ca9e62d2dcaa2911f1136825789b1fd9501e5`
Worktree: `/home/bts/Projects/omakade-quality-local`

## Changes

- Preserve committed session duration through live recorder restarts. Recovery
  excludes unobserved downtime and closes mismatched survivors at their heartbeat.
- Capture zero playtime baselines and conservatively subtract already recorded
  time on late first imports. Preserve existing baselines and session rows.
- Close Qt SQL connections and prevent multiple new recorder instances from
  owning the same database, including stale-lock recovery after process exit.
- Use UTC release dates, the selected console on first matching, and replacement
  semantics for successful provider refreshes. Failed requests keep cached data.
  Metadata payload version 3 requests a refresh of older identified entries when
  credentials are available; manual identity choices stay selected.
- Show metadata release year beside the title, with an About section for first
  release, platform, genres, credits and a plain-text description. Long descriptions
  expand/collapse with keyboard activation and have larger couch typography.

## Verification

Validation is recorded in `build/quality-evidence/`. The regression baseline
failed in the expected places: live restart 30 rather than 150 seconds; first
session 1200 rather than 600 seconds; release date July 27 rather than July 28.
The fixed focused regressions pass, including late first import, repeated recovery,
metadata persistence/offline handling and duplicate daemon ownership.

The full 130-check suite passed after the typography/navigation changes. The final
focus-scroll adjustment is covered by the subsequent details/navigation test run.
Metadata persistence/date checks also passed under UTC, Pacific/Kiritimati and
America/Los_Angeles. Desktop and couch screenshots were inspected.
The first suite attempt failed at GTK display initialization; the offscreen test
configuration requires `QT_QPA_PLATFORMTHEME=generic` and `QT_STYLE_OVERRIDE=Fusion`
on this desktop. It is not an application regression.

All tests use isolated config/data/cache/runtime paths. Daemon lifecycle tests use
an empty private profile set so no real emulator is recorded and no app rescan is
sent. Installed binary hashes and the existing recorder PID remained unchanged.
No installation, service restart, push, tag, PR or release was performed.

## Remaining limitations

- Existing totals that were already lost or double-counted are not repaired.
- Imported counters and observed sessions lack enough information for exact
  historical overlap reconciliation. Late imports conservatively assume recorded
  time is included; recording gaps can delay visible increases. No broad claim of
  exact playtime accounting is made.
- Emulator file-picker launches, relative/sandbox path attribution, session editing,
  history backup, richer database failure handling and real emulator validation
  remain future work.
- Metadata requests were tested with fixtures, not live IGDB credentials. Release
  dates still use the existing English display format. Real desktop/controller
  acceptance, package lifecycle validation and release approval remain open.
- Save-file backups, RomM integration, home screen and genre/year browsing filters
  are not implemented in this candidate.

## Manual test checklist for later

1. In a test profile, identify a console game. Check year, selected console, first
   release, genre, description and credits. Reopen and confirm they persist.
2. Correct a match, refresh, then disconnect the network. Confirm the chosen game
   and cached details stay intact.
3. In desktop and couch layouts, expand/collapse a long description, navigate to
   metadata controls and back, then return to Play. Check readability from the couch.
4. In an isolated emulator fixture, record a first session and restart the recorder
   while playing. Compare duration before/after and after emulator exit.
5. Only after acceptance, prepare a versioned package candidate and validate its
   upgrade/service lifecycle before requesting publication approval.
