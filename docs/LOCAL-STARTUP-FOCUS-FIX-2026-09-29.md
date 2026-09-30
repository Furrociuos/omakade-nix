# Local startup focus fix, September 29

> Historical snapshot from before the versioned 1.13 candidate was prepared.
> See [the 1.13 candidate record](1.13-RELEASE-CANDIDATE.md) for current release gates.

A cold Couch launch could leave the active window focused on `couchLibrary`, a
container that handles no game or button input. Startup focus restoration treated
that container as a valid destination. Clicking a game assigned usable focus,
which explained why both keyboard and controller input started working afterward.

Startup now restores focus to the games, or Settings when the Couch library is
empty. Controller discovery still preserves valid game, toolbar and keyboard focus.
The cold-launch probe reproduced the container focus failure before the fix.

The previous route tests assigned focus before traversing controls and missed this
case. Sixteen new CTests use the normal startup activation and first-frame
controller initialization. They never request activation or assign focus themselves.
Separate processes check keyboard and SDL controller input, desktop mode and both
Couch layouts, empty results and delayed Couch results. First arrow and confirm
must work without a mouse.

The tests also exposed a clear-filter problem: clearing an already blank desktop
search field did not clear a search stored in the library model. Clear Filters now
clears the model explicitly. Hidden Couch controls return null Tab destinations
when no usable controls exist, avoiding startup binding warnings.

## Verification

All 16 cold-launch cases passed on the exact staged application with native
Wayland windows in omabox. A separate cold launch using compositor keyboard input
changed the selection with its first Right press and opened that game with Enter.
Screenshots document each step. No mouse input was sent.

All 315 Release CTests passed, including the 16 new startup cases and five
navigation contracts covering 4,320 keyboard, D-pad and analog directional links.
The full suite and binary snapshots are recorded in the evidence folder. The
initial startup matrix passed 14/16; the two desktop empty-state failures exposed
the clear-filter issue above and passed after its correction.
Physical device pairing and streaming transport remain outside these isolated checks.

## Local candidate

Source base: `4556ebd2be15b0c4b7e7ad0bce25aa4c21d1c1a3` with the uncommitted navigation
and startup focus patches. Nothing is committed, pushed or published.

App: `/home/bts/.local/lib/omakade/startup-focus-20260929-b29758e6/usr/bin/omakade`.

SHA-256: `b29758e653f45aea2d1b71337d562857b7d09b94b2486fd1e21f36c71d72f7d3`.

Evidence: `/data/user-data/bts/builds/omakade-ui-quality/startup-focus-20260929/`.
Its `installed-candidate.json` records the source snapshot, checks and rollback script.
Installation preserves the configuration, a consistent database backup, the prior
app and desktop entry. The recorder stays on the September 24 candidate. Keep that
candidate's resource prefix, which supplies the compiled session-profile path.

To roll back, close Omakade, run the recorded `rollback.py`, then reopen Omakade.
Rollback changes the app and desktop entry while preserving newer database history.
Publication still requires exact-candidate maintainer testing and explicit approval.
