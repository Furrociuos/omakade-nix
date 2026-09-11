# Navigation and menus redesign

> Historical review and implementation record. For the reconciled September 8
> candidate, push authorization, and remaining acceptance gates, see
> [PUBLICATION-CANDIDATE.md](PUBLICATION-CANDIDATE.md). Earlier local-only status
> and test counts below describe their original snapshots.

Status: implementation complete for local acceptance, publication not authorized. Reviewed against `a0ebc14eaf7e4a4f7247c1a8b543323be7c88c3f` on September 8, 2026. The implementation notes below distinguish the first checkpoint from the final rollout. Installation provenance is recorded with the versioned local candidate.

## Outcome

Make browsing and playing immediately understandable, put occasional actions in predictable places, and allow new capabilities without adding another row of buttons. Preserve every existing capability, saved preference, library selection, and controller workflow.

Use three levels consistently:

1. **Destinations:** Home and Library.
2. **Context controls:** filters and sorting in Library; Play and game actions in Details.
3. **Configuration:** Settings, with categories and focused subpages.

Home remains optional and Library remains the default. No new empty destinations for planned features.

## Current code and pressure points

| Surface | Current implementation | Change needed |
| --- | --- | --- |
| Application and library header | `qml/Main.qml`: browsing presets, individual source buttons, availability, sorting, console layout, cover size, rescan, organization, saved filters, metadata filters, Home, Settings and Couch controls spread across rows | Separate navigation from query controls and maintenance actions |
| Details | `qml/screens/GameDetails.qml`: action grid plus artwork/link buttons under the cover, separate external link, permanently expanded alias text | Establish action priority and progressively disclose less-used information |
| Settings | `qml/components/SettingsPanel.qml`: Sources, Library, Connections, Controls & streaming, About & storage | Give unrelated tasks separate categories without widening the tab row |
| Home | `qml/screens/HomeScreen.qml`: Continue playing and Up next with identity-based focus restoration | Share the application shell while preserving queue behavior and focus |
| Editors and overlays | Separate artwork, manual game, saved filter, bulk organization, backup, startup restore, link and collection dialogs | Consistent headers, cancellation, focus return and scrolling |
| Input routing | `Main.qml` combines active-surface selection, explicit targets, spatial fallback, registered shortcuts and controller routing | Make behavior an explicit contract; migrate incrementally rather than replacing it all |

The existing navigation review in `NAVIGATION-REVIEW.md` remains relevant. Automated navigation coverage does not establish physical-controller acceptance.

## Application shell

Wide layout:

```text
OMAKADE    Home  Library                    Search games    Couch    Settings
Library / Super Nintendo             Sources    Filters (3)    Sort    View    More
[Installed ×] [1990s ×] [Adventure ×]                     84 games · Clear filters
```

This is a hierarchy sketch, not an exact pixel layout. Actual chips show only supported, active filters.

- The first row belongs to the app. The second belongs to the current screen.
- Search explicitly means game search. From Home it opens Library search, retaining a route back to Home. In Library it updates the current query. A visible clear action restores the previous browsing context where applicable.
- Details keep a prominent Back control and a compact shared header. Search or navigation away from Details must preserve a return location rather than destroying the caller's state.
- Couch Mode stays a direct, labeled toggle. Changing it preserves the current destination and selected game.
- Settings opens at its last category during the session; closing returns to the exact invoking control when it still exists.
- Scanning/update status uses one compact status area with access to existing progress and cancellation. Do not make every background job another header button.
- At narrow widths, use a deliberate compact header and a labeled Browse menu for less frequent context controls. Never let an arbitrary Flow become four or five header rows. Preserve direct Search, Back, Play and Filters access where relevant.
- Desktop and couch share destinations, labels and actions. Couch gets larger targets, fewer simultaneous controls and persistent input hints, not a separate information architecture.

## Library

### Browsing controls

| Existing control | Proposed home |
| --- | --- |
| All, Favorites, Recent | A compact browsing selector, default All games |
| Hidden | Visibility option in Filters, with a conspicuous active indicator |
| Individual source buttons and Emulated | Searchable Sources selector; preserve current filter semantics |
| Console portals and current console title | Existing cards plus a breadcrumb with Back to library |
| Installed, All games, Ready to install | Availability group in Filters |
| Status, collection, tag, genre, decade, platform | Named groups in one Filters panel |
| Saved filters | Saved views within Filters, also accessible from the browsing selector |
| Clear and Clear metadata filters | Individual removable chips, group reset inside Filters, one explicit Clear filters action |
| Title, recent, playtime, rating, popularity | Sort menu with current selection and direction if supported |
| Cover size and console-versus-games view | View menu |
| Organize | Select games action entering a dedicated selection toolbar |
| Pick a game | Library More menu, with an optional direct shortcut preserved if already supported |
| Rescan | Library More menu; source-specific rescan stays in source settings |
| Add manual game | Library More > Add game; source settings may link to the same editor |
| Collection management | Library collection management, linked from the collection filter |

Filters should apply immediately, matching current behavior, with Reset and Done rather than an ambiguous Apply/Cancel pair. Opening or closing the panel does not change filters. Search and source scope must have separately understandable reset behavior; Clear filters must not silently navigate out of a console or erase search.

Show a compact active-filter summary outside the panel. Long sets can collapse into a count with an accessible expansion. Distinguish no games found, no games installed, no metadata for selected filters, disabled sources and a scan in progress. Each state gets a relevant existing recovery action.

Selection mode replaces context controls with selection count, organization actions and Done. Sorting/filtering must not silently apply bulk edits to games the user never selected. Keep existing selection semantics explicit and tested.

Returning from Details restores query, source, console, filters, sort, scroll and game identity. If a game disappears, focus moves to the nearest remaining item, then the empty-state action. Background updates must not move focus or reload unaffected covers.

## Home

- Keep Continue playing and Up next as the initial sections.
- Queue removal and reordering belong to a game action menu or focused queue controls, not the global toolbar.
- Retain unavailable queue entries with a useful explanation. Do not silently remove them.
- Preserve section-qualified focus identity when a game appears in both sections.
- Opening Details and returning restores the originating section and card.
- Future sections must earn their space through usable data. Do not add placeholder recommendations or game-length promises.

## Game details

Primary actions:

```text
Play     Favorite     Up next     Manage
```

Use explicit selected states for Favorite and Up next. Play remains the default initial focus. When multiple installations or an unavailable preferred installation require a choice, expose the launch choice beside Play rather than burying the reason it cannot launch.

### Manage menu

| Group | Existing actions to preserve |
| --- | --- |
| Launch and installations | Manage in launcher, select/prefer installation, link/unlink installation |
| Identity and artwork | Identify game, select artwork, reset custom artwork, existing metadata refresh/rejection actions |
| Library placement | Hide/unhide, show separately in library where currently supported |
| Manual entry | Edit/remove manual entry when applicable |

Do not conflate “show separately in library” with navigation back to Library. Verify the current pin action and give it a label that explains its actual effect. Hide, unlink, remove entry, and delete files must never share an ambiguous Remove label. Preserve existing confirmations and add one where an irreversible action requires it.

The cover stays visible, but maintenance buttons no longer need to sit permanently underneath it. PCGamingWiki and other available outbound destinations move to a labeled Links section/menu. A link should identify when it opens an external application/browser.

### Information order

1. Title, cover, platform, relevant release year, rating and primary actions.
2. About the game: description, developer/publisher and concise release information.
3. Personal organization: status, tags and collections.
4. Play activity, installation information, achievements and other available insights.

Keep section order consistent; omit unsupported empty sections rather than presenting empty tabs. Long descriptions retain Read more. Background artwork stays decorative, stable and subordinate to legible content.

### Regional names without the wall of text

- Keep all provider names and regional evidence in the data used for matching.
- Show the ROM region and relevant regional/platform release date when supported. Clearly label a fallback date.
- Show a short catalog-title explanation only when there is a meaningful difference from the local title. Filename region/revision decorations alone should not trigger one.
- If provider evidence establishes a regional relationship, explain it concisely. Do not infer a country from arbitrary alias text or special-case a franchise.
- Put the complete list behind **Other names (N)**, collapsed by default. Deduplicate display equivalents; retain provider labels and full stored evidence.
- Do not truncate names irretrievably. Expanded content wraps and participates in normal page scrolling; avoid a nested scrolling trap.
- Expanding preserves focus on the disclosure. Collapsing or switching games must not leave focus inside hidden content. Reset expansion on a different game.
- Where evidence is ambiguous, show the available facts without claiming a regional equivalence. This presentation change must not change matching decisions.

## Settings

Use a category sidebar on wide screens and a category list with drill-in pages on narrow screens. Couch uses the same hierarchy with larger rows. Avoid adding more horizontal tabs.

| Category | Contents |
| --- | --- |
| Library & Sources | Enabled sources, source status/rescan, ROM/GOG folders, manual-game entry point, console layout overrides, standalone emulator preference, launch/auto-close behavior, playtime tracking |
| Appearance | Cover size, default console presentation, reduced motion and existing display options |
| Controls | Controller status, current input help, couch/desktop mode; only expose remapping if it exists |
| Connections | Steam, IGDB/Twitch, SteamGridDB, RetroAchievements setup, test/disconnect, metadata update/stop |
| Streaming | Existing Sunshine/Moonlight export options, app-list update and restart |
| Backup & Storage | Existing personal-data backup/restore, cache budget, downloaded artwork maintenance and storage locations |
| About & Troubleshooting | Version/build provenance, existing diagnostics, project and issue links, useful configuration paths |

View-menu controls and Settings entries must manipulate the same preferences. One canonical configuration page owns each source/connection; other entry points deep-link to it. Collection management moves to Library, with a transitional settings link if needed.

Keep connection configuration distinct from a successful connection test. Display only status the backend actually knows. Preserve drafts according to an explicit Save/Cancel contract; navigation must not silently save credentials or discard unsubmitted changes. Keep secrets out of diagnostics.

Separate cache cleanup from user artwork and personal data. Explain the exact scope of each existing clear action. Backup & Storage must say that current backups cover Omakade data, not imply emulator save protection.

Settings search is optional later. A sensible category structure is the first deliverable; there is no need to build a search index now.

## Shared menus, editors and input contract

Apply the same contract to Settings, Filters, artwork, metadata identification, manual games, saved views, bulk organization, linking, collection deletion, backup/restore and startup recovery.

- Every surface has a title, visible close/back affordance, deterministic initial focus and a remembered invoker.
- Back/Escape closes the innermost surface first. It must not also navigate the page beneath it or exit the app in the same event.
- Subpages return to their parent before closing the enclosing surface.
- Unsaved editors distinguish Save, Cancel and navigation away. Reuse existing validated semantics; confirm any necessary changes before implementation.
- Tab/Shift+Tab cycle through usable controls within the active modal. Arrows follow logical groups and never land on invisible, disabled or clipped targets.
- Preserve current controller bindings and glyphs. Confirm activates the focused control; Back follows the same hierarchy as Escape. Do not introduce required new bindings during the layout migration.
- Text fields and the onscreen keyboard own text-editing input while active. Directional editing must not also navigate the page underneath.
- Mouse movement alone must not steal keyboard/controller focus. Switching input mode updates hints without resetting selection.
- Background model changes preserve focus by stable identity rather than row number. If an action disappears, choose a documented adjacent fallback.
- Onscreen-keyboard closure returns to its text field; menu closure returns to its button; editor closure returns to its caller.
- Focus remains visible above sticky headers and inside scrolling panels. Long labels, large UI scale and narrow windows must not hide the only way out.
- Launch failure returns to an actionable state. Emulator return, controller reconnection and couch-mode changes preserve context.

## Implementation boundaries

Start with small reusable QML components: application header, context toolbar, action menu, settings category shell and disclosure section. Reuse `GlassButton`, `TextEntry`, existing backend calls and current models.

Give actions stable IDs, labels, enabled/visible conditions and handlers. Share action definitions where desktop and couch invoke the same behavior. This should be a small explicit model, not a general plugin framework.

Preserve current object names where tests or focus restoration depend on them. Update tests to express the new interaction path when a control legitimately moves behind a menu, rather than forcing hidden controls visible.

Document caller focus identity and return-state snapshots at surface boundaries. Consolidate active-surface handling incrementally as surfaces migrate; do not replace all overlay booleans and input routing in the first patch.

This work should not change metadata matching, source discovery, save formats, cache selection, launch commands or queue persistence. Any discovered defect in those systems gets a separate fix and evidence.

## Space for future capabilities

| Future capability | Natural location | Condition before exposing it |
| --- | --- | --- |
| Save versions and restore | Game Details > Saves; global policy in Backup & Storage | Reliable emulator/source adapters, safe snapshots and tested restores |
| RomM integration | Library source with one linked configuration page | Implemented authentication, identity and availability behavior |
| Per-game launch profiles | Details > Manage > Launch settings | Supported backend and clear override/reset behavior |
| More background jobs | Existing status area opening a task panel | Enough concurrent work to justify the panel |
| Game-length discovery | Filters and optional Home sections | Reliable library-wide data and clear unknown-value handling |
| Browser/remote play | Explicit Play option on compatible games | Implemented runtime, input and save behavior; not a current promise |

Add top-level destinations only for substantial independent workflows. A new integration normally adds a source or settings page, not another permanent header button.

## Delivery sequence and regression gates

Each phase is a small local candidate with its own rollback point. Do not change all navigation surfaces at once.

1. **Baseline and layout preview.** Inventory action handlers and current desktop/couch entry points, capture representative screens, and document focus/Back expectations. Verify every item in the relocation tables against enabled and disabled states. Preview wide and narrow layouts before wiring new behavior.
2. **Shared header and Library.** Introduce the shell, grouped controls and active-filter summary while reusing existing filter/sort handlers. Preserve console, saved-view, bulk-selection and Home return state. This is the recommended first implementation slice.
3. **Details.** Group actions into Manage; simplify visible release information and collapse aliases. Preserve installation choice, identity/artwork editors, organization, achievements and focus across Read more/disclosures.
4. **Settings.** Move existing sections into the category shell, one category at a time. Preserve source configuration, credential drafts, streaming controls and backup paths. Add links rather than duplicate configuration logic.
5. **Editor consistency.** Align remaining modal headers, buttons, focus return and text entry. Include startup recovery, not only dialogs reachable from the normal Library screen.
6. **Whole-app acceptance.** Test cross-screen journeys, physical controller behavior and visual fit on the exact local candidate before treating the redesign as ready.

Automated checks should include:

- Existing relevant CTests plus build/QML validation for each changed surface. The previously reported 144-test baseline is historical evidence, not a test run for this document.
- An action inventory proving every moved command remains reachable and invokes the correct handler.
- Keyboard and virtual-controller coverage for open, traverse, activate, cancel and return on each new menu/surface.
- At least 600x800 desktop and 1280x720 couch layouts, plus wide screens, long titles, long translations/aliases and large UI scale.
- Return-state tests through Home, Library, console portals, Details, Settings and nested editors.
- Empty/offline/unavailable states, metadata updates while focused, filtered-out selections, hidden games and multiple installations.
- Draft handling, backup restore choices, repeated Back events, onscreen keyboard, reduced motion and source-specific disabled controls.
- No renewed flashing, blanket cover reloads, scroll jumps or noticeable input delay during background updates.

Keep automated execution isolated from the installed app: private IPC and XDG/config/data/cache/runtime paths, test controller state and appropriate offscreen software rendering. Do not send synthetic controller events to the live library or launch real games from tests.

Physical acceptance on the exact candidate remains necessary:

1. Browse with D-pad and stick, including held input and rapid direction changes.
2. Search with the couch keyboard; clear text; close it and resume browsing.
3. Apply filters, enter a console, open a game, use Manage, return and verify position.
4. Reorder/remove Up next entries and return from a game appearing in both Home sections.
5. Visit every Settings category and nested editor; cancel drafts and use repeated Back.
6. Switch mouse/keyboard/controller and couch/desktop without losing focus.
7. Disconnect/reconnect the controller; launch and return from an emulator; verify launch-failure recovery.
8. Inspect the appearance with sparse metadata, long aliases, missing artwork and multiple installations.

Done means the actions are easier to find, all existing capabilities remain reachable, return state is reliable, automated checks pass, and the maintainer accepts the physical-controller candidate. It does not mean merely fitting the buttons onto fewer rows. Publication remains separately gated by explicit maintainer approval.


## Second review and first local implementation

The second code review found requirements that need explicit protection:

- Desktop sources support multi-selection. The eventual Sources selector needs checkboxes or an equivalent additive action, not only the single-choice behavior currently used by the couch browser.
- Couch Clear filters currently also resets sorting, search and console scope. Preserve that behavior until there are separately labeled Clear filters and Reset browsing commands, with migration tests. Do not silently change it while relocating controls.
- Applying a saved view intentionally updates browsing state; canceling its editor should restore the invoker. Those are different return behaviors.
- Metadata filter choices, hidden-state controls and source buttons differ between demo fixtures and a configured library. Test both, including unavailable actions.
- The shared shell cannot simply be copied onto Home and Details: their current focus containers and caller snapshots need an explicit migration. The first patch is the desktop Library header, not a claim of app-wide completion.

First local slice:

- Home and Library sit with Settings and Couch in the application row.
- Search and browsing presets sit together below application navigation; search has its own row below 720 pixels.
- Library More contains Organize, Saved filters and Rescan, using a reusable action-menu component.
- Organization and metadata filter buttons share one responsive strip. Their data handlers are unchanged.
- Canceling a library editor opened from More restores focus to More. Applying a saved view retains the existing return-to-results behavior.
- Controller tests now enter More, traverse its actions, cancel it, open both editors and cancel back to the invoking button. A 600x800 case supplements the existing tiled and wide cases.

Remaining: grouped Sources/Filters/Sort/View controls and active chips, the shared shell on Home/Details and couch, detail actions and alias disclosure, Settings categories, and the remaining editor migration. These remain staged work, not completed features.


### Validation for the first slice

- Build passed. Full isolated CTest suite: 145/145 passed in 62.05 seconds.
- After adding the mode-transition guard and render fixture, all four navigation cases passed again in 3.55 seconds. These exercise forward/reverse Tab, directional traversal, repeated menu opening, both editor handoffs, cancellation, selection retention and closing the desktop menu on entry to Couch Mode.
- Inspected rendered 600x800 Library/menu and 1600x900 Library previews. The header and menu fit; the source strip still requires the planned selector redesign.
- Testing caught and fixed a reopened popup starting after its previously focused action. Initial focus now explicitly selects the first enabled action, with Close as fallback.
- Evidence is in `build/quality-evidence/navigation/`: build and test logs plus preview PNGs. Fixtures use synthetic games and isolated application state.
- No installed binary, user library, settings or remote repository was changed. Physical controller and live-library acceptance remain outstanding.


## Final local rollout

The planned navigation overhaul is implemented for local acceptance. Earlier checkpoint notes above describe intermediate states, not remaining work.

- Library: Home/Library navigation, separate search/presets, Sources, Filters, Sort, View and More. Hidden games lives in Filters with a visible active chip. Metadata/status/availability chips can be removed individually. Clearing these filters preserves source, console, search and sort context.
- Sources: the existing enabled-source handlers and additive selection are retained in a wrapping panel. A separate source search is unnecessary for the current short list; the panel can scroll if more sources are added.
- More: Pick a game, Add a game, Organize, Saved filters, Manage collections and Rescan. Collection management deep-links to its existing canonical implementation. Bulk organization keeps its tested editor rather than introducing a second selection implementation.
- Details: Play, Favorite, Up next and Manage. Launcher, visibility, pinning, manual entry, default installation, artwork and linking actions are grouped in Manage. Installation choices and unavailable-default guidance remain immediately visible when needed. External links sit below About.
- Regional information: region/date and meaningful catalog-title differences remain visible. Other names are collapsed, with the complete provider evidence retained. Filename decorations do not by themselves cause a redundant catalog-name line. There are no game-specific matching rules in this change.
- Settings: Sources, Library & launching, Appearance, Controls, Connections, Streaming, Backup & storage, About & help. Wide screens use a sidebar; narrow screens use a category list. Existing fields stay mounted so switching categories does not silently discard drafts or save them.
- Home: Library, Search, Settings and mode switching are directly available. Desktop and couch share command meanings without forcing identical geometry. Couch navigation and browsing controls occupy separate rows.
- Input: reusable menus register as the active navigation surface; Back closes the innermost menu and restores its invoker. Value pickers return to Filters, sizing returns to View, and editor cancellation restores the initiating control. Mode changes close desktop menus. Global search/settings shortcuts cannot focus controls beneath unrelated editors.
- Couch: additive source selection is supported, and Clear filters is separate from Reset browsing. The latter preserves its original complete-reset behavior with an accurate label.
- Long filter labels are bounded with ellipsis and full accessible text; pointer users can read truncated labels in a tooltip. Menus scale for couch displays and scroll when needed.

Future save versions, RomM, launch profiles and background-task expansion remain future capabilities. They have designated locations, not empty menu placeholders or new product promises.

Acceptance still requires a physical controller and normal emulator launches. Automated fixtures do not replace that check. All publishing restrictions remain in force.
