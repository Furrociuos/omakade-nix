# Home redesign, local candidate

Home now uses a featured recent game, portrait tiles, an Up next shelf, and suggestions from the user's own library. Quick access opens consoles, sources, collections, saved views, favorites, backlog, or all games. View all on Continue playing opens the complete recent library.

Suggestions use available, non-hidden games outside the recent list and queue. Backlog and favorites take precedence, followed by genre overlap with recent games, unplayed games, and older games. Completed and abandoned games are excluded. Every suggestion includes its reason. Ordering is deterministic and linked installations are deduplicated. No new provider or network integration is involved.

Game tiles retain their instances when artwork or play history updates. Queue entries keep their saved identities and unavailable entries remain removable. Queue actions live in the shared keyboard/controller menu. Home opens game details through the existing preferred-installation path and restores the previous library filters on return. Quick access deliberately starts a fresh view; saved views apply their stored filters.

## Verification

- Isolated unit coverage for suggestion exclusions, priority, stability, shortcut counts, and disabled sources.
- Keyboard traversal across game tiles, scrolling focused tiles into view, header/featured navigation, opening details and returning, queue removal, and clearing stale searches through quick access.
- Home navigation renders at desktop and couch sizes; overview renders at 600x800, 1280x720, and 2048x1152, plus an empty-library render.
- Full application test results and installed artifact identity are recorded with the local candidate.

## Maintainer checks

1. Open Home full screen and tiled. Check real cover art, long titles, and reading size.
2. Use arrows/D-pad to move between the header, featured game, shortcuts, and tile sections. Use Enter/confirm and Escape/Back.
3. Open a game and return. Add a suggestion to Up next, reorder it, and remove it through Queue actions.
4. Try console, collection, and saved-view shortcuts after leaving a search active in Library.
5. Review whether the suggestions are useful for your library. Physical controller feel and actual artwork acceptance remain manual checks.

Everything remains local. No publication is authorized.

## Delayed-opening layout fix

The first candidate's demo previews opened Home before the first frame. Opening it later from the running library reproduced a Grid polish loop and collapsed all its sections. Shelves now calculate tile positions and total height from available width and item count, without circular implicit-size dependencies.

Eight additional regression cases open Home after startup, resize it, update the queue, leave, and reopen it. They check non-overlapping sections and tiles at four desktop/couch sizes and fail on layout-loop warnings. The original broken layout was reproduced before applying this fix.

## Mouse wheel scrolling

Home now animates discrete wheel notches over 150 ms, accumulates repeated input, and reverses from the current position rather than finishing an old target. Pixel deltas remain direct. Reduced motion keeps immediate scrolling. Navigation reveal, scrollbar dragging, resizing, and content-size changes cancel pending animation.

Four wheel-input regression cases cover narrow/wide windows with reduced motion on and off, accumulated input, reversal, pixel deltas, bounds, and navigation taking over. Existing library wheel tests remain part of the full suite.
