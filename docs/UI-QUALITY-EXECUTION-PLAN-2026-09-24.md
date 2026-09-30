# Omakade interface quality plan

Prepared 2026-09-24 against clean `review/ui-polish` at
`fd73d65c2ee638000b6979689cec03de8eabe100`. This is an execution plan, not a
claim that the interface has reached a particular score. Recheck the branch and
preserve new work before implementation. The installed local candidate is useful
for comparison, but edits and tests must use isolated data until an exact new
candidate is ready for a versioned local install.

## Outcome and limits

Make the app easy to scan, predictable to operate, and complete on desktop and
from a couch. A maintainer should be able to find a game, understand what will
launch, play or stop it, repair a broken entry, manage saves, and interpret Stats
without reading an explanation of the UI. Preserve every working capability and
the distinctions between recorded, imported, current, and inferred data.

"10/10" is a target for the owner's physical acceptance, not a test assertion.
Do not call it achieved based on screenshots or CTest alone. Keep this a local
candidate. `AGENTS.md` forbids push, PR, tag, or publication before relevant
checks, exact-candidate local testing, and explicit maintainer approval.

## Baseline to improve

Observed in the installed app with the real library, and in final fixture renders:

- Stats is a single long `Flickable` with nearly every aggregate at equal visual
  weight. Supporting labels run as small as 8-10 px; the live 757-by-826 logical
  window is hard to scan. The top-game detail and the first row reach the right
  edge. `This Year` mixes recorded-period figures with an all-time library total,
  requiring a long caveat before the user can read the numbers.
- `StatsScreen.qml` uses focusable, 10 px `SectionTitle` text as the only focus
  stops inside the scroller. Tab reveals later sections. Page Down did not scroll
  while a period button held focus in the live app. Diagnose key routing before
  declaring that a universal keyboard failure.
- Home and Library have a coherent visual theme and art-forward browsing. Home's
  horizontal shelf and the Library's dense toolbar need an explicit interaction
  pass, especially at narrow widths and with a controller.
- The 600-by-800 Game Details fixture clips a long title and metadata. Its primary
  Play action remains visible, but the title, date, rating, history, actions, and
  description compete in a narrow column.
- Repair Library exposes filters and reasons, but its large sparse panel separates
  diagnosis from next actions and leaves several transient status sentences in the
  main reading flow. The couch render uses little of its available area.
- Settings, editors, backup/restore, launch setup, Stop Game, and the card preview
  have extensive functionality and render coverage; their actual task clarity and
  physical input behavior need the same review before a whole-app rating.

Keep the strongest existing behavior: shared destination header, visible primary
actions, personal organization, broad source support, conservative repair and save
semantics, and clear attribution of playtime. The older
`STATS-AND-YEAR-IN-REVIEW-PLAN.md` records the data contract; its instruction to
push each slice is superseded by current `AGENTS.md` and this local-only request.

## Quality contract

Set a small set of reusable QML tokens before changing many screens. Existing
`Theme` colors remain the source of palette values. Add semantic text roles,
spacing, section separation, control height, focus outline, and desktop/couch
scaling in one shared place instead of further scattered `font.pixelSize` values.
Start with 14 px desktop body text, 12 px supporting text, 16-20 px section titles,
and 18 px or larger couch body text at 1080p, then adjust from rendered and live
evidence. Do not fix readability solely by enlarging a page that already has too
much content.

The following are acceptance targets, not a claim of WCAG certification for a
native Qt app:

1. No clipped, overlapping, or inaccessible information at the tested sizes.
   Ellipsis is acceptable for a cover-grid label when the full name is available
   by focus or opening Details; it is not acceptable for the Details title,
   essential repair reason, result, or action.
2. Normal text has at least 4.5:1 contrast against its actual surface, and large
   text and meaningful control boundaries at least 3:1. Test dark, light, and
   translucent Omarchy themes, including focused, selected, disabled, and error
   states. Use visual judgement as well as measured contrast.
3. At 200% text scaling, core tasks remain possible without loss of content.
   The whole view may scroll; actions cannot disappear off an unscrollable edge.
4. Every interactive element has a visible focus state, a useful accessible name,
   and a predictable Tab/controller route. Noninteractive headings must not be
   fake focus targets merely to make a page scroll. Wheel, Page Up/Down, Home/End,
   and controller shoulder or stick scrolling should work when relevant.
5. Every screen gives one primary action and a clear return path. Destructive or
   consequential actions show the target and effect before confirmation and a
   truthful result afterward. Status text appears near the action or in one
   dismissible status area, never as a pile of old notices.
6. Loading, empty, partial, offline, disconnected-storage, permission, and error
   states explain what is known and what the user can do. No UI claims that a
   drive, emulator, file, save, or game is healthy beyond the check actually run.
7. The user's current selection, filter, scroll context, and focus survive return
   navigation, rescans, asynchronous model replacement, and desktop/couch changes
   where the same control still exists.

## Proposed information architecture

Keep Home, Library, and Stats as destinations. Settings remains a task-specific
overlay. Maintain one shared header; keep screen controls beneath it. Use a
consistent component for page title, short scope/description, primary action,
and status. Desktop and couch should share labels and task order, with a more
spacious couch layout and fewer controls shown at once.

### Stats: redesign first

Preserve `PlayStats` as the data source and its existing honesty rules. Change the
presentation, not the totals. Use three clearly labeled views inside Stats:

| View | Default content | Secondary content |
| --- | --- | --- |
| Overview | Recorded time, days played, games played, sessions; one featured top game; next three ranked games; one simple weekly pattern; a short coverage badge | Full ranking and period achievements through explicit "See all" or "Details" controls |
| Play patterns | One chart at a time for hour of day or weekday, with selected bar value; session duration distribution; longest session and streak; habits/returns | Full buckets and contextual explanations via disclosure, not permanent paragraphs |
| Library snapshot | All-time reconciled library total; current collection size and completion; systems, sources, genres, top rated | Expandable full lists with counts and clear denominator |

`This Year` and `All Time` select the period for dated data only. Place the coverage
start date in a compact "Recorded since Sep 8, 2026" badge with an accessible
explanation. In the Library snapshot, label lifetime/imported totals as such and
make it obvious that the period selector does not affect them. Period achievements
should carry their own dated label. Current completion status must not be called
"finished this year" because it has no completion date.

Draw 24-hour bars with readable axes at sensible intervals and an accessible value
for every hour. Provide a list or table alternative for mouse, keyboard, and
screen-reader users. Weekday names and values should not rely on hover or color.
Use bars only where comparison helps; a plain ranked list is better for a few
systems or sources. Keep absolute time and share together. Never infer an exact
date from an imported lifetime counter. Avoid redundant prose that repeats a
chart; keep definitions behind an info control.

Replace focusable `SectionTitle` text with real view selectors, disclosure
buttons, chart controls, and a scrollable content focus strategy. Ensure period
switching, opening/closing details, and returning from the card preserve a useful
focus target and scroll position. Test zero sessions, one game, skewed data (one
game owns 85% of time), missing metadata, recording disabled, incomplete period,
database error, long translated titles, and several thousand sessions. Verify
card figures and labels still match the redesigned screen and exported PNG.

### Home and Library

- Home: keep one featured game and the queue, but make "Continue", "Up next",
  and suggested games visibly distinct. A suggested game needs a specific reason;
  a manually queued game needs no algorithmic claim. Show unavailable storage or
  launch setup before a Play attempt rather than implying readiness.
- Give horizontal shelves a visible position/next affordance. Wheel over a shelf
  must move it consistently without stealing vertical page scroll when the shelf
  cannot move. Arrow keys/controller movement reveal the focused card, including
  after delayed artwork and queue updates. Test wheel bursts under load, reduced
  motion, first/last card, empty/full queue, and repeated destination switches.
- Library: keep Search and the current filter context obvious. Show active filter
  chips, result count, and one clear reset; move lower-frequency controls into
  the existing menus. Make source, availability, sort, and view labels distinct.
  Preserve browsing state after Details, Settings, repair, and scans. Distinguish
  "scan pending", "source disconnected", "no matching games", and "library empty".
- Cards: ensure cover, title, platform/source, availability, focus/selection, and
  launch intent can be recognized quickly. Do not make muted metadata so small it
  becomes decorative. Long titles may elide in a grid only with a reliable route
  to the full title. Keep the 1,500 and 10,000 item responsiveness checks.

### Game Details and launch/stop

- Reflow the title and metadata first. At the 600-by-800 stress size and the
  app's real minimum width, show the full title through wrapping or a deliberate
  expanded title region. Separate selected installation, regional release, rating,
  playtime provenance, last played, and achievements into readable rows. Use
  concise labels and disclosure for the long provider description and aliases.
- Keep Play or launch setup as the leading action. Show the exact selected source
  and installation or emulator when it matters. Stop Game must name which tracked
  game or process will be stopped, including the Steam delegation case. Do not
  imply that closing Steam itself is equivalent to ending an individual game.
- Put occasional actions (identify, artwork, backups, metadata, manage) in a
  consistent Manage area. Preserve back destination, selected game, and scroll
  position. Test long title, missing cover, partial date, no provider data,
  duplicate installations, disconnected drive, unavailable emulator, active and
  stale sessions, Steam and emulator stop outcomes.

### Repair Library

- Start with reason counts and plain-language categories. Counts must come from
  the same current snapshot as the list, visibly mark "checking" or "stale" while
  asynchronous availability is unresolved, and distinguish a missing game file
  from an absent drive, scan failure, unavailable emulator, and duplicate hint.
- Use a compact queue/list plus a selected-game pane at wide sizes, and a single
  item flow at narrow/couch sizes. Show diagnosis, evidence, recommended next
  action, and less common alternatives in that order. Keep filters and progress
  visible without making the cover consume most of the viewport.
- For relocation, preview old and new path, what identity/organization follows,
  whether save backups will be copied, and what undo will restore. Report skipped
  or conflicting backups explicitly. Never imply saves were moved or restored
  when only backup copies were made. Preserve the existing conservative checks
  for symlinks, shared saves, missing drives, stale scans, and repeated moves.
- Replace stacked transient messages with one current result and an accessible
  history/log if needed. Confirm retries, cancellation, restart, and undo from the
  same location; do not leave a stale success message after a later failure.

### Settings, editors, saves, and card

- Audit Settings by task: sources, connections, library organization, controls,
  save protection/backup, and app status. Keep form labels and help text beside
  their fields. Make save/apply feedback local, preserve unsaved input on a failed
  operation, and provide visible disabled reasons and a predictable close/return.
  Redact tokens in screenshots and logs.
- Give Manual Game, Artwork, Backup, Bulk Organization, Saved Filters, and
  Restore screens the same title/action/error/focus pattern. Use real game names
  and effects in confirmation. Test long paths, permission failure, cancellation,
  restart, no results, and mixed mouse/controller input. Keep backup and save
  actions semantically distinct from game launch or repair actions.
- Save panels must identify the emulator, save set, backup destination, shared
  ownership, and effect of delete/restore. Never present "protected" as proof
  that an unverified save location is covered. Keep the successful backup and
  rollback semantics established by the current implementation.
- Card preview: ensure legible preview and export at 1200-by-2000; show the period
  and data provenance on the image; give a plain saved-path/result message and an
  obvious return to Stats. Test unavailable Pictures folder and failed write.

## Execution order for an implementation agent

### Code and test map

| Work | Main implementation | Existing checks to extend |
| --- | --- | --- |
| Shared shell, typography, focus | `qml/components/AppHeader.qml`, `GlassButton.qml`, themed controls, `src/theme/OmarchyTheme.*`, `qml/Main.qml` | Controller navigation and desktop/couch render cases in `tests/CMakeLists.txt` |
| Stats and card | `qml/screens/StatsScreen.qml`, `YearInReviewPreview.qml`, `qml/components/YearInReviewCard.qml`; preserve `src/library/PlayStats.*` semantics | `omakade_stats_*`, `omakade_card_export*`, Stats cases in `tests/CoreTests.cpp` |
| Home and Library | `qml/screens/HomeScreen.qml`, `qml/components/LibraryView.qml`, `CouchLibraryView.qml`, `GameCard.qml`, `qml/Main.qml` | `omakade_home_*`, `omakade_library_*`, controller and 1,000/10,000-game cases |
| Details, launch, stop | `qml/screens/GameDetails.qml`, `qml/components/LaunchSetupPanel.qml`, `GameStopPanel.qml` | `omakade_detail_*`, `omakade_launch_*`, `GameStop*Tests.cpp` |
| Repair and saves | `qml/components/LibraryRepairPanel.qml`, `src/library/LibraryRepair.*`, `SaveBackupMenu.qml`, `SaveProtectionPanel.qml` | `omakade_library-repair_*`, `FeatureWorkflowTests.cpp`, `SaveBackupsTests.cpp`, `SaveSetTests.cpp` |
| Settings and editors | `qml/components/SettingsPanel.qml`, `qml/screens/*Editor.qml`, `RestoreStartup.qml` | Existing settings, editor, backup, and couch render cases in `tests/CMakeLists.txt` |

Inspect nearby model and service code when the screen's status or action depends
on it. Do not treat this table as permission to change data semantics without a
separate regression and migration review.

### Milestones

1. **Inventory and baseline.** Recheck Git, `AGENTS.md`, live install, and any
   concurrent work. Capture screen/state matrix using fixture data and read-only
   real-library views. Record each issue as confirmed defect, design decision, or
   unverified concern. Measure current contrast, overflow, focus route, and
   first-use task completion. Keep a screenshot index with exact build commit.
2. **Shared UI foundation.** Add typography/spacing/control/focus tokens, a
   reusable page/section/status treatment, and a small number of accessible
   controls. Migrate one representative screen first to validate the system.
   Do not mass-replace every literal font size without visual review.
3. **Stats vertical slice.** Implement Overview, then Patterns, then Library
   snapshot and card adjustments. Preserve the data model contract. For each
   slice, check semantic labels, visual scan order, keyboard/controller scroll,
   narrow/couch renders, empty/error states, and exact figure agreement.
4. **Daily path.** Refine Home shelf, Library controls/cards, and Details reflow.
   Test the route Home -> Library -> Details -> Play/Stop -> back, including
   updates while browsing. Treat narrow title/metadata overflow as a release
   blocker for this polish candidate.
5. **Recovery path.** Refine Repair, relocation, and save flows. Pair UI changes
   with underlying workflow tests only where the UI depends on a new claim or
   state. Run repeated relocation, undo, shared-save and missing-drive cases in
   isolated data before a real-library acceptance pass.
6. **Remaining surfaces.** Audit Settings, editors, overlays, card, and couch
   input against the quality contract. Fix actual gaps found; avoid broad feature
   additions. Review all final screenshots as a set for consistency.
7. **Exact candidate validation.** Build Debug and Release, run the complete
   suite at the final commit, review QML warnings and renders, then install the
   exact tested build using the versioned local-install/rollback pattern in
   `docs/LOCAL-INSTALL-PROTONDB-2026-09-09.md`. Keep config, a consistent library
   DB backup, previous binaries and link targets. Verify app and recorder hashes,
   service, desktop entry, and a usable window. Do not restore an old database
   merely to roll back binaries. Do not publish.

Work in small reviewable commits. Each commit should state the user-visible
problem solved and include the relevant focused tests and screenshot evidence.
Avoid building a new navigation framework or rewriting `PlayStats` just to move
content. Recheck the full suite after shared components or input routing changes,
because those affect many screens.

## Verification matrix

| Dimension | Required cases |
| --- | --- |
| Window | 600x800 stress fixture; real minimum width; 820x620; 1280x720; 1920x1080; 2560x1440; ultrawide and 4K couch |
| Text/theme | normal and 200% text; dark/light/translucent themes; reduced motion; high contrast where supported |
| Input | mouse click, wheel, touchpad-style wheel bursts, keyboard Tab/Shift+Tab and page keys, controller D-pad/stick/confirm/back, mixed input and controller reconnect |
| Data | empty, one game, 1,500 games, 10,000 games, long titles/paths, missing artwork, incomplete scan, offline metadata, disconnected storage, skewed Stats data |
| Task | browse/search/filter; queue and play; Details and Stop; Stats period/chart/card; repair reason/relocation/undo; save backup/restore; Settings edit/cancel/restart |
| Failure | unreadable DB, failed export, failed write, source scan error, unavailable runtime, stale async result, missing drive, partial backup copy |

Automated checks should assert meaningful outcomes: no QML binding errors, no
unreachable focus or clipped essential controls, expected period/data labels,
correct scroll movement, and correct result states. Screenshots are evidence to
inspect, not a substitute for assertions. Add a short human task script for each
major flow; observe at least one fresh user where practical. The owner must test
the exact local candidate on the real library and a physical controller before
assigning a final score or approving publication.

## Exit report

Deliver a per-screen before/after summary with remaining limitations; exact
branch/commit; focused and full-suite results including failures/retries; visual
matrix and screenshots; app/recorder install paths and hashes; backup/rollback
location; and a concise real-library acceptance checklist. Ask the owner to rate
clarity, appearance, and feel after hands-on use. Iterate on their concrete
misses. An agent should not write "10/10" in the handoff on its own authority.

## Reference criteria

- [Qt Quick accessibility](https://doc.qt.io/qt-6/qml-qtquick-accessible.html)
  documents roles, names, focusability, and actions for custom QML items.
- [Qt Quick Controls focus management](https://doc.qt.io/qt-6/qtquickcontrols-focus.html)
  describes focus scopes and control focus policies. Apply this to the actual
  controller and keyboard route in this app, not only to individual controls.
- [WCAG 2.2](https://www.w3.org/TR/wcag/) supplies useful contrast and text
  resizing targets. Treat them as design benchmarks for this native Qt app,
  not as an unverified conformance claim.
