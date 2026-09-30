# Local Couch navigation fix, September 29

> Historical snapshot from before the versioned 1.13 candidate was prepared.
> See [the 1.13 candidate record](1.13-RELEASE-CANDIDATE.md) for current release gates.

The subsequent [startup focus fix](LOCAL-STARTUP-FOCUS-FIX-2026-09-29.md) supersedes
the installed app below and adds cold-launch tests that do not assign focus.

The installed app now routes Left/Right within the visible row and Up/Down between
adjacent rows. Every filter control can reach the header. Wrapped rows remain
reachable, and returning from games preserves the control used to enter them.
Tab/Shift+Tab include the games and stay inside the library.

Controller discovery and the first input from another controller previously reset
focus to the games. Valid library and keyboard focus is now preserved. Empty
results also move focus away from unusable game views and actions.

## Verification

- 299/299 Release CTests passed across the full run and corrected isolation rerun.
  The core suite reports 263 passed and two optional data-dependent probes skipped.
- Five navigation contracts passed at 900x700, 1280x720, 1920x1080, 2560x1080 and
  3840x2160. They check 4,320 directional links using keyboard events and an SDL
  virtual controller through the production polling code, with D-pad and analog input.
- The contract also checks reachability, detail/grid layouts, changing labels,
  console filters, empty results, Tab order, modal focus, destination return,
  native game selection, hotplug/switching, repeat/release and toolbar/mode shortcuts.
- Native Wayland contracts passed for both the build and exact staged application
  in an isolated Omarchy desktop. A screenshot confirmed visible focus in that window.
- The exact staged app passed smoke testing. Desktop metadata validation passed.

One early suite run was invalidated by overlapping a rebuild. The frozen-binary
run passed 291 checks; three checks needed the box's packaged-profile alias and
relative app/recorder helper paths corrected. All three then passed. The logs retain
these failures and the successful rerun. No application code changed during that rerun.

## Installed candidate and recovery

The source base is `4556ebd2be15b0c4b7e7ad0bce25aa4c21d1c1a3` with the uncommitted
navigation patch. Its snapshot and patch digest are recorded in the evidence folder.

App: `/home/bts/.local/lib/omakade/navigation-20260929-8e4f64ca/usr/bin/omakade`.

SHA-256: `8e4f64ca0387ad3a6bd956b854074309c291750efcae38dd88db56c9229c1da9`.

The command symlink and desktop entry select this app. The running process was
verified against that path and hash. Configuration and a consistent database backup
were preserved. The recorder target and service were not changed. Keep the previous
candidate prefix, which supplies the compiled profile path and rollback binary.

Rollback: `/home/bts/.local/state/omakade/navigation-install-20260929-190745/rollback.py`.
Close Omakade, run this script, then reopen it. It restores the prior app and desktop
entry while preserving newer database history and the recorder.

Evidence: `/data/user-data/bts/builds/omakade-ui-quality/navigation-20260929/`.
See [the navigation contract](CONTROLLER-NAVIGATION.md) for routine testing.

Nothing was committed, pushed or published. Publication still requires maintainer
testing of the exact candidate and explicit approval.
