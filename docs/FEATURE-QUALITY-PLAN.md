# Omakade feature quality and expansion plan

Reviewed September 8, 2026 against `033ca9e62d2dcaa2911f1136825789b1fd9501e5`
on `codex/port-playtime-game-info`. This is a plan and bounded code review, not a
release approval or a completed product audit. No application behavior or installed
data was changed during this review.

## Local implementation status

A first candidate is being prepared in `/home/bts/Projects/omakade-quality-local`
on `codex/feature-quality-local`. It fixes live recovery, zero and late initial
baseline capture, UTC date handling, first-match platform selection, successful
provider-field replacement, SQL connection cleanup and daemon ownership. It also
adds the metadata year beside the title and a readable About section with keyboard
expand/collapse. See `LOCAL-CANDIDATE.md` for validation and remaining work.

The original findings below describe the reviewed baseline, not an assertion that
these defects remain in the candidate. Exact reconciliation across recording gaps,
legacy history corrections, broader process attribution and database error handling
remain separate work. No installed data is migrated by this development run.

## Assessment

The recent work adds useful foundations. Metadata reuses the existing provider
queue, preserves explicitly selected identities, and refreshes older payloads by
version. Tracking separates process matching, persistence, and elapsed-time handling,
uses PID plus process start time, and supplies a controllable clock for tests.

The tests do not establish release confidence for the new behavior. The restart
test covers a dead process, not adoption of a live session. The merge test checks
the formula without exercising an imported counter advancing through the first
recorded session. The richer metadata test checks parsing, not acceptance,
persistence, refresh, or the rendered details screen. Fix these gaps before adding
features that depend on their data. The assessment concerns the code, irrespective
of which model authored it.

## What exists and what to build on

| Area | Current implementation | Next useful improvement |
| --- | --- | --- |
| Identification | `GameMetadata`: platform-aware matching, manual search/choice/rejection, stored provider identity, artwork selection, background queue | Make match state, correction and refresh reliable and understandable; do not recreate the matcher |
| Game details | Ratings, popularity, game-length data, new release/genre/credits/summary block | Correct dates and platform semantics, persistent refresh tests, readable desktop and couch layouts |
| Playtime | `src/tracking`, six emulator model integrations, imported counters, daemon and settings toggle | Correct accounting and recovery, verified attribution, recorder status, inspectable history |
| Library discovery | `LibraryFilterModel`: Recent mode, recent/playtime/rating/popularity sorting, favorites, completion filters, collections, tags, saved filters and random selection | Reuse these models for an optional home screen and intentional play queue |
| Launch preferences | Linked installations and preferred installation selection; platform-specific delegated launching | Explain what will launch and why it is unavailable; scope per-game profiles after an adapter review |
| Backup | Explicit personal-data tables, settings and artwork export/restore | Define preservation of manual metadata choices and session history; these are outside the current table allowlist |
| Remote collection | No RomM source in the reviewed code | Optional RomM adapter after the local experience is reliable |
| Saves | Existing backup concerns Omakade data, not emulator save files | Separate save discovery/backup project only after source-specific restore requirements are established |

## Findings to fix first

### P1: live-session recovery overwrites recorded duration

`src/tracking/SessionRecorder.cpp`, `recover`, initializes an adopted session's
`elapsedMs` to zero. `updateProgress` and `endSession` replace the stored total.
Consequently the first post-restart write loses the pre-restart duration.

Confirmed with the current rebuilt tracking library and an in-memory database:
120 recorded seconds, recovery of a still-live PID/start-time pair, then 30 more
seconds produced **30 seconds instead of 150**.

Preserve committed elapsed time when adopting a session; do not count unobserved
downtime. Cover live and dead processes, PID reuse, repeated restart, next heartbeat,
normal exit, and switching recording off. Inject liveness for deterministic tests.

### P1: baseline capture can double-count the first session

`src/tracking/SessionDatabase.cpp:173` declines to capture zero imported time.
`RyujinxGameModel::load` and the PCSX2/RetroArch equivalents capture baselines on
load. After an emulator first writes its own time, that total becomes the baseline
even though the recorder already contains the same session.

Confirmed using a temporary database: initial imported time 0, one 600-second
session, then imported time 600 produced **1200 seconds instead of 600**.

Capturing zero is necessary but not sufficient. Define a reconciliation policy for
late discovery, recorder-disabled periods, resets of emulator counters, and stale
imports. A fixed `max(imported, baseline + tracked)` also can hide newly recorded
time until tracking catches up after an unrecorded interval. Keep imported and
observed evidence separate and test a chronological sequence of snapshots. Do not
silently rewrite existing history when overlap cannot be established.

### P2: release dates depend on the local timezone

`GameMetadata::parseMatches` converts epoch seconds to a local date. The current
fixture timestamp is July 28, 1997 UTC and displays July 27 in America/Louisville.
The existing test only checks the year.

Use UTC calendar dates and format at presentation time. Label IGDB's first release
as such; it is not necessarily the release date of the selected platform/edition.
Test exact dates in positive and negative offsets, missing values, and year edges.

### P2: platform text is computed before the current platform is assigned

In `GameMetadata::acceptMatch` (lines 984 and 995), `platformText` reads the old
payload's platform before assigning `m_active.system`. A newly matched console
game can therefore show the provider's whole platform list instead of its selected
console. This is a code-path finding; a full accept/persist/UI reproduction remains
to be added.

Derive the selected platform from the current game first. Distinguish that platform
from other supported platforms; never call the whole list the original platform.

### P2: successful refresh retains removed provider fields

`acceptMatch` preserves the old payload for the same IGDB identity and overwrites
new fields only when nonempty. A successful refresh with an absent summary, credits,
or genres keeps stale values. Define replacement semantics for provider-owned
fields while preserving user choices. Failed requests should retain cached data.
Test both cases separately, including changing the selected match.

### Further review and verification required

- Process matching returns the first argument with a recognized extension and
  stores it verbatim. Verify relative paths, symlinks, Flatpak paths, argument
  syntax, playlists, AppImages and title changes inside an emulator. Match the
  library's identity consistently; avoid silently attributing ambiguous activity.
- Profiles list more emulators than the UI integrates. Recording a path does not
  establish that its total appears in Omakade. Publish a verified coverage matrix.
- The daemon has no explicit single-recorder guard. Two instances could duplicate
  sessions or interfere during reconciliation. Add ownership and lifecycle tests.
- Several database writes ignore errors. Add failure reporting and recovery tests
  for locked/unwritable storage and schema initialization failures.
- `PlaySessionStore` does not explicitly close/remove its named Qt SQL connection.
  The focused test emits a duplicate-connection warning. Fix lifecycle cleanup.
- Existing backup allowlists omit `play_sessions`, `play_baselines` and
  `game_metadata`. Decide which user-owned history and identity overrides belong
  in the archive. Keep re-downloadable metadata distinct from irreplaceable choices.
- The five-second polling interval and thirty-second heartbeat impose observation
  limits. Show approximate process runtime honestly; foreground focus alone is
  not proof of play or pause. Do not automatically exclude unfocused couch play.

## Delivery sequence

### 1. Correctness candidate

Fix the two P1 accounting defects, dates, platform selection, provider refresh
semantics, connection cleanup and recorder ownership. Add regressions that fail
on this baseline. Define migration behavior before touching existing session data.

Acceptance: exact expected totals through recovery and import timelines; no
cross-game attribution; correct dates and selected console on first identification;
cached details survive offline failures; successful refresh replaces provider data.
Run the full relevant suite after fixes, plus package upgrade and daemon lifecycle
checks in isolation. Local maintainer validation is required before publication.

### 2. Finish metadata and details

- Keep the existing identify/correct/reject actions. Present clear matched,
  uncertain, unmatched, unavailable and refreshing states without making people
  interpret internal queue messages.
- Show the selected game's platform, first release, genres, credits and background
  with clear provenance. Test sparse data and long names/summaries.
- Make corrections durable across restart, rescans and provider refresh. Add a
  deliberate per-game refresh and retry path where the current UI lacks one.
- Add useful genre/year filtering only after fields have model roles and stable
  persistence. Extend saved-filter serialization and migration at the same time.
- Verify controller reachability and scrolling in small desktop and 1080p couch
  layouts. Capture populated fixtures, not only empty metadata placeholders.

Acceptance: identify, correct, refresh, disconnect, restart and return using only a
controller. Long details remain legible and the primary launch action stays easy
to reach. No additional provider until a measured matching gap justifies one.

### 3. Make play history trustworthy and visible

Add recorder status and an explanation of supported attribution, recent sessions
per game, provenance of imported versus observed time, and explicit correction or
deletion with confirmation where appropriate. Decide backup/export coverage first.
Measure idle daemon cost and refresh-query cost against a large history fixture.

Acceptance: a real launch and exit for each supported emulator path; terminal and
Omakade launches; suspend/resume; recorder restart while playing; tracking toggle;
emulator UI launches marked unsupported until actually verified. A running service
alone does not count as successful session attribution.

### 4. Improve the daily library experience

Prototype an optional home screen using existing recent activity and organization
models. Start with Continue playing and a small user-controlled Up next queue.
Continue launches the preferred installation; it does not promise save-state resume.
Use stable identities across linked installations, exclude hidden games, handle
missing storage explicitly, and preserve controller focus as activity updates.

Do not ship several rows that repeat the same covers or a new ranking system
without a clear benefit. Compare startup, search and controller navigation with the
current library at the same size. Keep the library directly accessible and let the
user choose their startup view. Add suggestions only when the underlying data can
explain why a game was suggested.

### 5. Bounded expansion after local acceptance

**RomM:** begin with a read-only adapter investigation against a pinned API version
and test server. Establish authentication, pagination, provider/platform identity,
and offline cache behavior. Then prototype one selected download with progress,
cancel/retry, space checks, atomic completion and local-emulator launch. Preserve
remote provenance and prevent duplicate local entries. No bulk synchronization or
save sync in the first candidate. Reference: https://romm.app/

**Per-game setup:** inventory existing launch options and preferred-installation
behavior per source. Pilot a small reversible profile for one emulator, with
preview/reset and a clear configuration owner. Keep launcher-managed settings
with their launcher; validate against real commands before generalizing.

**Saves:** defer implementation. Start with verified save-location adapters and
read-only discovery, then a versioned backup/restore drill for one emulator. Live
saves, multiple profiles, conflicts and partial restores must be resolved before
cross-device sync. This is separate from Omakade's current personal-data backup.

## Evidence and limits

Rebuilt `omakade_core_tests` and `omakade_tracking` against the reviewed source.
Ran six focused test functions covering the new matcher, recorder, merge and
metadata behavior: all six passed, plus QtTest setup/cleanup. The independent
temporary-database probes still reproduced both accounting errors. A Qt conversion
probe confirmed numeric platform IDs do convert through `toStringList`; that
suspected issue is not a finding.

Local evidence: `build/feature-audit-2026-09-08/probe.cpp`, `probe-results.txt`,
and `focused-tests.txt`. Probe writes used temporary/in-memory databases; focused
tests used isolated XDG config/data/cache directories. No daemon was launched or
stopped. This review did not run the full suite, render the new screen, call live
metadata APIs, test a RomM server, or prove real emulator attribution. Those remain
explicit candidate gates, not implied by the passing focused tests.

Recommended first implementation scope: phase 1, then phases 2 and 3. Reassess the
home screen with reliable real data before committing to the expansion projects.
