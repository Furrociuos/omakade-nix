# Discovery expansion, local candidate

The first increment adds genre, release-decade, and platform filters to the existing library.
It does not install or publish a release.

## Behavior

- Desktop has three filter buttons; Couch Browse has corresponding scrollable categories.
- Criteria combine with existing search, source, availability, favorites, and organization filters.
  Console-card counts use matching members, so a console with no matching games disappears.
- Genre comes from confirmed cached IGDB metadata. Ambiguous or rejected identities do not supply
  genre filters. Decade uses the catalog year, falling back to the source's year. Regional dates
  remain installation-specific on game details. Unknown values do not match a selected criterion.
- Platform groups emulated systems by Omakade's console catalog; non-console installations use PC.
- No additional network request is needed to filter. New metadata updates the results and choices.
- Saved-filter state version 2 records genre, decade, platform, and console scope. Legacy version 1
  still loads and clears newer criteria. Unsupported states fail without altering the current view.
- Library and backup validation share SavedFilterRules. Archive round trips preserve the criteria;
  older clients reject the new saved-filter state rather than silently broadening its results.

## Validation

The core regression covers combined criteria, live metadata changes, uncertain matches, empty
results, restart, legacy saved views, console-scope restoration, and archive round trips. Desktop
UI tests select and clear a decade with keyboard events at 600x800 and 1280x720. Couch navigation
reaches the release-decade category through the scrolling list and applies a value.

Final development build and all 134 CTest checks passed in 52.07 seconds, including the new
metadata regression, desktop picker tests, and expanded couch-navigation checks. Reviewed the
600x800 picker screenshot. Logs are in build/quality-sweep/discovery-build.log and
discovery-checks.log. Tests use private XDG/TMP directories, offscreen software rendering, and
disabled session DBus.

Physical-controller acceptance remains separate. No installed application data is used by tests.

## Home and Up next, second increment

- Optional Home is accessible from desktop and Couch Mode. Library remains the startup view.
- Continue playing shows up to eight available, non-hidden games using existing launch activity.
- Up next stores up to 100 installation identities, with add, remove, and manual ordering.
  Add games from their details or Continue playing. Selecting an entry opens game details.
- Linked installations appear once. Removing a linked queue entry removes its queued members.
  Hidden entries remain stored but are omitted. Disconnected or disabled-source entries stay
  visible as unavailable, keeping their title and place in the queue.
- Returning from details restores the library's filters and Home focus. Cover placeholders
  appear when artwork is missing. Home refreshes only while active, avoiding a startup scan.
- Queue writes are transactional. Backups include queue order. Merge preserves existing order
  and appends new identities; exceeding 100 entries rejects the restore. Restoring an older
  backup without a queue preserves the current queue.

Storage tests exercise restart, links, source availability, hidden entries, failed writes, filter
restoration, and backup round trips. Render tests exercise keyboard opening and removal in both
views at 600x800 and 1280x720. Physical-controller acceptance remains a separate local check.
Home render fixtures emit a Qt DelegateModel cancellation warning during initial setup; their
opening, filter restoration, and queue navigation assertions pass.

Final Home candidate: development build and all 138 CTest checks passed. Evidence is in
`build/quality-sweep/home-build.log` and `home-checks.log`. Screenshots for desktop and Couch
Mode were reviewed at narrow and standard sizes. The installed application is unchanged.

## Cover flashing correction

Background metadata updates previously invalidated the entire library layout, briefly replacing
all visible covers with placeholders. Data-only updates now refresh filtering incrementally;
structural console changes retain the bulk rebuild path. The active sort role is registered so
rating, popularity, recent activity, and playtime can update without unconditional invalidation.

The regression reproduced 12 layout invalidations for 12 metadata updates before the fix and
zero afterward. It also checks live rating reordering and metadata-filter membership changes.
Evidence is in `build/quality-sweep/flashing/`.

## Later increments

Game-length filtering waits until the current Steam-focused insights service provides consistent
library-wide values. Save-file versioning and RomM remain later, separately validated projects.
