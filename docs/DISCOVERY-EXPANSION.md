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

## Next increment

Build the optional Home view using stable game identities and existing recent activity, followed
by a persistent, manually ordered Up next queue. Keep the current library directly accessible.
Game-length filtering waits until the current Steam-focused insights service provides consistent
library-wide values. Save-file versioning and RomM remain later, separately validated projects.
