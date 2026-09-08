# Omakade quality sweep, September 8, 2026

Risk-based review of the local candidate after e3e2a20. This covers critical paths across the
app, not a claim that every line, device, provider, or failure state has been exhaustively tested.
All changes and evidence stay local. The installed application remains 1.7.1-3.

## Fixed in this sweep

| Priority | Finding | Change and evidence |
| --- | --- | --- |
| P1 | A new game reported for the same PID/start time inherited the previous active session. | Close the previous game at the observation boundary and begin a new session. Regression reproduced 90 seconds charged to game A instead of A=60, B=30; now passes. |
| P2 | Process discovery included readable processes owned by other users. | Limit discovery to the effective user. Regression requires the current process to appear and every surviving returned process to have the expected owner. |
| P2 | Many settings setters ignored save failure, leaving no indication that changes might disappear after restart. | Central save-failure signal and a visible toast. Regression forces a write failure with an occupied destination, checks notification, then verifies recovery and persistence. Live settings still apply in memory; this does not make every setter transactional. |
| P2 | Invalid JSON preserved cached metadata but did not set the selected game's error state. | Preserve the cached description and expose a refresh error on the detail page. Regression verifies both. |

The session fix only separates games when the matcher reports a changed path. It cannot detect
an internal emulator game change that is invisible in the process arguments.

## Follow-up fixes

| Concern | Local change |
| --- | --- |
| Ambiguous identity | Removed popularity-based tie-breaking. Older automatic matches are rechecked; manual IDs stay protected. Exact provider aliases and localization names are recognized on the correct platform, with one bounded fallback query. Truncated result sets require review. |
| Lost regional context | Preserve the local title and original ROM filename alongside the provider title, aliases, localization region names/identifiers, and edition information. Candidate buttons show edition and ID. No game-specific numbering rules or country inference from alias comments. |
| Portrait changes | Keep the existing portrait when the catalog ID changes. Ambiguous identities cannot trigger new automatic artwork selection. |
| Backup coverage | Version 2 includes explicit IGDB choices, recorded sessions with stable IDs, baselines, and newer library preferences. Rating/popularity sorting no longer prevents export. Version 1 remains readable. See BACKUP-FORMAT.md for merge, replacement, recorder-lock, and exclusion rules. |
| Source cache starvation | Steam, RetroArch, Battle.net, and metadata portraits share the same eviction policy: protect referenced files and remove unused files first. A source cannot remove another source's files. The configured size is a soft target while artwork remains referenced. |
| Failed metadata writes | Return failure, stop the dependent workflow, preserve the committed payload, and retain the unsaved choice for an explicit retry. Pending writes are excluded from background refresh and protected from portrait pruning. |
| Failed session writes | Check schema preparation, progress, and closure writes. Keep failed closures with their original time boundary and retry at the normal flush interval. Report failure through daemon logging and the app's local notification channel. |
| Failed artwork writes | Steam and RetroArch share a transactional batch writer that retains pending changes on failure. Battle.net reports a failed update before replacing its model's saved path. |
| Short play sessions | Propagate precise seconds through sources, linked games, and console portals. Display minutes and sort by precise duration. Preserve the existing maximum-across-linked-installations behavior. |
| Duplicated policies | Extract shared cache eviction and artwork persistence helpers. Other scan policies and the large main.cpp test harness remain future maintenance work. |

## Remaining limits and acceptance

- Provider evidence is incomplete. There is no automatic regional-title selection or regional
  release-date preference yet. Alias/localization coverage cannot guarantee every ROM is matched.
  Uncertain results require identification; descriptions for an uncertain cached ID remain visibly
  flagged until confirmed. IGDB field references: https://api-docs.igdb.com/#alternative-name and
  https://api-docs.igdb.com/#game-localization.
- Cache limits are soft while files remain referenced. This avoids blank cards and download churn,
  but a strict global least-recently-used coordinator with visible-only protection remains future work.
- Play-history merge intentionally preserves existing history for an already tracked game path.
  Restore requires stopping the recorder. Emulator saves and save states remain excluded.
- Pending writes exist in memory and can be lost if storage stays unavailable until exit. A failed
  initial session insert is reported; the recorder resumes recording when a later poll can insert.
  This is not a guarantee of complete tracking during a storage outage.
- Process arguments are still the observation source. Loading or closing a title inside an emulator
  can be invisible. Wrapper handoff, game exit, and idle protection need adapter-specific runtime
  testing; no broad process-descendant heuristic was added without that evidence.
- Physical controller repeat/reconnect, actual platform Tab dispatch, focus after emulator return,
  and mixed mouse/controller use still need the manual pass in NAVIGATION-REVIEW.md. Automated
  offscreen checks do not establish hardware acceptance.

## Manual candidate checklist

1. Confirm a known match and an ambiguous regional/edition match. Retry a selection, restart,
   and confirm the chosen ID and artwork remain. Verify Paperboy and other NES portraits.
2. Check sub-hour playtime display and sorting, then a normal emulator session and exit.
3. Export a version 2 archive. Inspect its preview and exclusions. Test merge/replacement on
   a disposable library with the recorder stopped, including an older archive.
4. Traverse library, details, organization, metadata, backup, settings, and dialogs with keyboard
   and a physical pad. Check cancel/back, reconnect, and returning from a real emulator.

## Subsystems inspected

- Scanning and identity: ROM folder normalization, representative emulator/Steam import paths,
  incomplete-scan guards, cached-library loading, and metadata candidate selection.
- Persistence and organization: settings serialization, linked-game transactions, bulk personal
  state writes, saved filters, and metadata writes.
- Tracking and launch: process discovery/matching, session transitions/recovery, detached launch
  tracking, path/argument construction, and idle-inhibition connection.
- Artwork and metadata networking: provider refresh states, response limits/timeouts, portrait
  preservation, and cache pruning.
- Backup/restore: archive bounds, validation, snapshot transactions, table/settings coverage,
  recovery journal checks, and atomic output paths. No live restore was performed.
- UI/navigation/theme: latest navigation changes and coverage, notification handling, duration
  presentation, and theme watcher handling. Full UI tests ran again after these changes.

Existing strengths include incomplete-scan preservation in inspected model paths, transactions
for linked and bulk personal data, bounded provider responses, atomic backup output, and isolated
backup recovery tests. These are specific observations, not whole-subsystem safety guarantees.

## Verification

- 132/132 CTest cases passed, including 11 new core regressions and expanded history/identity
  interruption-recovery fixtures; private XDG and temporary
  directories, offscreen rendering, disabled session DBus, and no live-app IPC/controller access.
- The installed SQLite database passed a read-only `quick_check`.
- Of 1,478 metadata records inspected, no referenced portrait file was missing at audit time.
  This does not prove every source cover or every game identity is correct.
- A read-only live IGDB probe accepted the expanded fields and alias query: searching Starwing
  on SNES returned Star Fox (ID 8581), with two aliases and two localizations. This verifies one
  real provider response, not universal catalog coverage.
- Evidence: `build/quality-sweep/session-before.log`, `checks.log`, and `build.log`.
- No commits, tags, releases, messages, or assets were published. No candidate was installed.

Follow-up evidence: build/quality-sweep/remaining-targeted.log, remaining-checks.log, and
remaining-build.log. These tests use private storage and disabled live-app IPC/controller access.
No follow-up candidate has been installed or published.
