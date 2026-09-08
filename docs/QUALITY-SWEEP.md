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

## Open findings, in recommended order

1. **P1: game identity still relies too heavily on titles.** `GameMetadata::matchResult` uses
   rating count to resolve multiple same-title candidates. ROM display cleanup discards region
   labels, although the original content path remains available. Implement the regional identity
   model before enriching uncertain matches further: preserve filename facts, fetch aliases and
   localizations, distinguish platform releases, and require review for ambiguity. Never infer
   identity from popularity. Protect explicit user matches during migration.
2. **P1: backup coverage has fallen behind personal data.** `BackupArchive::tableColumns` omits
   recorded play sessions, baselines, and game metadata, including manual identifications.
   `settingNames` also covers only a subset of the current preferences. Define what is recoverable,
   separate regenerable cache from user decisions, version the format, and add migration/restore
   fixtures before extending it. Current backups do not back up emulator save files.
3. **P2: artwork cache eviction can starve a source.** Steam, RetroArch, and Battle.net each subtract
   the other caches from a shared budget and prune their own files. If other caches consume the
   budget, the effective local allowance becomes zero. Referenced covers are deprioritized for
   eviction but are not protected. This can cause churn and blank cards. Use one coordinator with
   visible-artwork protection and a defined eviction order. Source inspection establishes this
   risk; it does not establish why Paperboy's original file disappeared.
4. **P2: storage-error propagation is inconsistent.** Some session progress/end writes and cover
   cache updates ignore SQL failures. `GameMetadata::persist` reports a write error but returns
   void, so callers can continue and later replace the status with a success message. Exercise
   failed writes/commits in isolated databases, keep unsaved work retryable, and return structured
   failures through the UI. Do not retrofit this with blind database retries.
5. **P2: process lifetime is not gameplay lifetime.** The recorder observes process arguments;
   loading or closing a title inside an emulator may not update those arguments. Launcher idle
   inhibition tracks the directly started process, so wrapper handoff needs runtime validation.
   Document current limits and add adapter-specific lifecycle evidence before claiming exact
   tracking or universal idle protection.
6. **P2: navigation acceptance remains incomplete.** The preceding navigation review fixed focus
   containment and exposed real organization controls in tests. A physical pad, actual platform
   shortcut dispatch, focus after emulator return, reconnect, and mixed mouse/controller use
   still need hands-on checks. See NAVIGATION-REVIEW.md.
7. **P3: whole-hour presentation makes short sessions look untracked.** Source models expose
   integer hours and sorting uses that role. Preserve a precise duration role, display minutes
   below an hour, and sort by precise time rather than rounded presentation.
8. **P3: maintainability increases regression risk.** Provider models repeat scan persistence,
   cover caching, and error handling. Navigation combines explicit links with geometric fallback.
   Many test drivers live in `src/app/main.cpp`, while demo visibility can differ from real UI.
   Extract shared policies and test drivers incrementally, retaining behavior tests throughout.

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

- 132/132 CTest cases passed, including the additional core regressions; private XDG and temporary
  directories, offscreen rendering, disabled session DBus, and no live-app IPC/controller access.
- The installed SQLite database passed a read-only `quick_check`.
- Of 1,478 metadata records inspected, no referenced portrait file was missing at audit time.
  This does not prove every source cover or every game identity is correct.
- Evidence: `build/quality-sweep/session-before.log`, `checks.log`, and `build.log`.
- No commits, tags, releases, messages, or assets were published. No candidate was installed.

Next work should address identity and backup coverage first, then cache coordination and durable
error handling. Each should be a separate local candidate with targeted failure tests and a
specific manual acceptance checklist.
