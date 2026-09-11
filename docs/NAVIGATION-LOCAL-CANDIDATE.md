# Local navigation candidate

> Historical review and implementation record. For the reconciled September 8
> candidate, push authorization, and remaining acceptance gates, see
> [PUBLICATION-CANDIDATE.md](PUBLICATION-CANDIDATE.md). Earlier local-only status
> and test counts below describe their original snapshots.

The navigation overhaul is implemented and tested locally. Nothing has been pushed or published.

## Changes

- Library controls are grouped into Sources, Filters, Sort, View and More. Active filter chips can be cleared individually, and Hidden games is inside Filters.
- Sources retain additive selection. Saved views, organization, manual entry, collection management and scanning remain available through More.
- Details uses Play, Favorite, Up next and Manage. Other names is collapsed by default while all identification evidence remains stored.
- Settings has separate categories for sources, library/launching, appearance, controls, connections, streaming, backup/storage and help. Narrow windows use a category picker instead of rows of tabs.
- Home and Couch Mode separate navigation from browsing controls. Home exposes Search and Settings. Couch Clear filters does not reset search, source, console or sorting; Reset browsing retains the full reset.
- Menus and editors return focus to their caller. Desktop menus close when changing mode. Back and Tab stay within the active surface.

## Evidence

The final source candidate passed all 159 CTests in 63.63 seconds using isolated application state and no physical controller input. The build and whitespace checks passed. Logs and screenshots are in `build/quality-evidence/navigation-final/` and `build/dev/tests/`.

Coverage includes menu opening/reopening, forward/reverse Tab, directional traversal, multi-source selection, nested filter and sizing menus, editor cancellation, linked-installation defaults, metadata/alias disclosure, Home return state, desktop/couch switching, narrow Settings categories, and 4K menu rendering. Existing source, backup, startup-recovery, launch and library core tests also passed.

The local installer records the exact source commit and binary SHA-256 in the versioned candidate's `candidate.json`. It retains the previous executable and desktop entry. The system package is not replaced.

## Maintainer check

1. Use the controller to open Sources and combine two sources. Apply a genre/decade filter, remove one chip, and verify Back returns through the picker and menu without losing the selected game.
2. Open a familiar game. Traverse Play, Favorite, Up next and Manage. Open and cancel artwork/identification, and check the same game remains selected.
3. Check FFIII/FFVI regional information and expand/collapse Other names. Check a long title and a game with sparse metadata.
4. Visit Settings categories, edit a field without saving, change categories and return. Check Back, Tab/Shift+Tab and the couch keyboard.
5. Switch desktop/couch, browse Home, disconnect/reconnect the controller, then launch a game and return to Omakade.

Physical-controller feel, emulator-return behavior and real-library visual acceptance remain manual checks. This candidate does not add save-file versioning, RomM or new metadata matching rules.

## Tiled layout and keyboard follow-up

Game actions now have equal widths and consistent gaps in each responsive layout. Shared action popups handle arrow and Tab keys within the modal, with Enter activation and Escape returning focus. Removed duplicate detail arrow handling that could skip a control. Installation choices return Down to Play.

Validation: all 159 isolated tests passed (63.67 seconds), including actual keyboard events for Sources, Filters, Sort and both Tab directions in More, controller paths at four window sizes, and equal action widths. Visually inspected 900x720 and 1256x836 detail renders. Physical controller acceptance remains with the maintainer.
