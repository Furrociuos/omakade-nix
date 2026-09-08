# Navigation review, September 8, 2026

The app does not yet have an exhaustive navigation guarantee. Navigation combines explicit
links, spatial fallback, keyboard focus order, modal selection, and controller input routing.
Layout changes can affect these paths differently.

## Findings and changes

- Detail render tests used DemoMode, which hid status, tags, and collection controls. The new
  fixtures explicitly show those controls without enabling live data or hardware access.
- An injected stale explicit target reproduced focus escaping the detail screen. The shared
  focus router now requires explicit and preferred targets to belong to the active container.
- Detail tests now traverse the registered Tab and Shift+Tab shortcut routes, require status,
  tags, collections, and metadata controls to be visited, require the cycle to return, and check
  usable focus and vertical window bounds. Raw window key injection bypasses Qt's platform
  shortcut dispatcher, so these tests activate the registered shortcuts directly.
- Arrow tests verify Read More to organization and back, in expanded and collapsed states.
- Additional detail fixtures cover 600x800 desktop and 1280x720 couch layouts.

## Existing coverage reviewed

| Surface | Automated coverage | Limit |
| --- | --- | --- |
| Library and console portals | Desktop/couch navigation, toolbar paths, filtering/back, large libraries, stale selection | Selected fixture states |
| Game details | Actions, description expansion, organization, metadata, both Tab routes, focus containment | Synthetic data; not every provider/state |
| Settings and text entry | Settings paths, editor fields, clear actions, on-screen keyboard reachability | Selected sections and field states |
| Artwork/manual editors | Entry, editing, controller text entry, close | Selected fixture data |
| Saved filters and bulk organization | Scrolling, field entry, selected actions and close | Selected layouts and data |
| Backup/restore | Preview, navigation to choices, cancel, confirmation | Isolated fixture storage only |
| Input routing | Virtual controller mapping and focus ownership checks | Not physical hardware end to end |

## Remaining acceptance

A physical-controller pass is still needed for D-pad/stick repeat, confirm/back, reconnection,
focus after returning from an emulator, and mouse-to-controller switching. Also check actual
Tab/Shift+Tab platform dispatch, text editing, and dialog focus restoration with the installed
app. These checks must not be represented as completed by the automated suite.

Validation: all 132 CTest cases passed in isolated directories. The injected out-of-screen
explicit target failed before the router fix and passed afterward.

All work remains local. The installed build has not been replaced by this navigation candidate.
Evidence is under build/odyssey-audit/navigation-*.log.
