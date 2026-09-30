# Omakade status and local acceptance

> Historical snapshot from before the versioned 1.13 candidate was prepared.
> See [the 1.13 candidate record](1.13-RELEASE-CANDIDATE.md) for current release gates.

Initial review: September 29, 2026. The status review below records the candidate
before the subsequent Couch navigation repair. That repair is now installed and
launched; see [the local navigation handoff](LOCAL-NAVIGATION-FIX-2026-09-29.md)
and the subsequent [startup focus fix](LOCAL-STARTUP-FOCUS-FIX-2026-09-29.md).
The changes remain uncommitted on `review/ui-polish`.

## Current state

- Public release: [1.12.0](https://github.com/btsouth/omakade/releases/tag/v1.12.0),
  published September 23, with x86_64 and ARM64 packages.
- Main: `e11de340b248484e845846875d9774b020f309ba`. Its architecture CI and latest
  CodeQL runs passed. There are no open PRs.
- Local application candidate: `4556ebd2be15b0c4b7e7ad0bce25aa4c21d1c1a3`,
  44 commits and 68 changed files ahead of main. It includes UI, repair,
  asynchronous availability, and save-backup relocation work.
- Installed app SHA-256:
  `e438fb0610d1c535824fb9e9a46445a9a58e199cb5225998762712f765f30d8f`.
- Installed recorder SHA-256:
  `1b464973851ad5809c9acd1928a3009c4fc330906b94eac21d3fb7c75bf01ded`.
  Both hashes match the installation manifest. The recorder service is active
  and selects this candidate.
- Earlier full Debug and Release logs each report 294/294 passing. These are
  September 24 results, not freshly repeated full-suite claims.
- The installation handoff lists real-game and physical-controller acceptance
  as pending. No publication approval is recorded for this candidate.

## Review scope

The focused source review traced relocation preview, backup copying, receipt
validation, launch-setup persistence, undo, and asynchronous availability. Copies
are staged and verified before commit. Destination conflicts, active emulators,
pending recovery, shared-save aliases, and launch-setup failures have explicit
guards. Undo keeps backups at both paths. Existing tests exercise unchanged
originals, copied restore data, failed copies, repeated relocation, independently
repaired metadata, and rollback after launch-setup storage failure.

This is a focused review of those paths, not a whole-branch safety audit. Real
launcher data, gameplay and physical controller behavior require owner testing.

## Fresh validation

Checks ran in a private, network-isolated omabox using fixture data and the
existing candidate binaries. The Release app hash matches the installed app.
Generated CTest definitions were copied into the box with screenshot output
paths redirected to its writable home. Source and binaries stayed read-only.

- Candidate reflow and return-to-Recent: 10 repetitions per test in each of
  Debug and Release, **40 executions passed**.
- Installed public 1.12.0 package: 10 repetitions per reflow test,
  **20 executions passed**. Its binary hash is
  `c09cc77810fdd106c10963ccde2e35f075a2f018e83ae63b16fa5e83610b4228`.
- Focused Release core, save-backup, Home-wheel, repair and startup checks:
  **21/21 passed**. The underlying core suite reports 263 passed and two skipped
  optional probes requiring supplied artwork or a live recorder database.
  The save-backup suite reports 32 passed, zero failed and zero skipped.
- Details direction/stop visibility and game-stop suites: **9/9 passed**.
- Software-rendered Couch startup fixtures: first frame in **321 ms** for 1,000
  games and **560 ms** for 10,000 games, within their 500/1,000 ms limits. These
  are fixture measurements, not the real library or physical GPU.
- The availability evaluator checked 10,000 fixture paths in **11 ms** on a
  worker thread; its runtime-plan resolver was reused for equivalent entries.
- Fresh narrow repair and relocation-confirmation renders were inspected.

Reflow-only runs initially warned that the compiled installation profile path
was not mounted. Profiles were supplied in the private config before stop and
repair checks. The box also emitted an accessibility-bus warning; screen-reader
integration is not validated by these checks.

Persistent logs and reviewed images are saved under
`/data/user-data/bts/builds/omakade-ui-quality/status-review-20260929/`.

## Issue disposition and remaining work

- [GitHub #54](https://github.com/btsouth/omakade/issues/54) was closed after
  confirming commit `6708029` is included in current main and running 60 passing
  isolated reflow/return-to-Recent executions across the candidate and installed
  public package. This is bounded evidence; reopen the tracker if it recurs.
- [GitHub #9](https://github.com/btsouth/omakade/issues/9) and
  [SBS-1144](https://linear.app/southboundsoftware/issue/SBS-1144/maintain-the-real-launcher-compatibility-matrix):
  ongoing real native/Flatpak launcher reports. They do not block already-shipped
  releases or independent development.
- Stats stores observed intervals but still uses the proportional allocator.
  Interval-based allocation and suspend-gap validation remain future work.
- The earlier TV helper handoff lists physical TV/audio/controller acceptance as
  outstanding. This review does not refresh that hardware result or OPR delivery.

## Remaining publication acceptance

Routine keyboard and controller route checks are automated. The navigation
contract covers the library rows, both layouts, empty results, dialogs, destination
return, controller switching, and button repeat/release at five sizes. Remaining
owner acceptance concerns the actual library, device transport and gameplay.

1. Close Omakade completely, then launch `/home/bts/.local/bin/omakade` to select
   the exact local candidate. Confirm your library and saved preferences appear.
2. Search and filter the Library, open Details, then return. Check selection,
   filters, scroll position and focus. Scroll Home's shelves and try Details in
   a narrow window. Essential titles, labels and actions should remain reachable.
3. Confirm the physical controller pairs and its button prompts match the device.
   Check TV/audio/streaming transport when those are part of the release scope.
4. Play one familiar Steam game and one emulator title for 30-60 seconds each.
   Check Stop Game before, during and after play. Try stopping one title, then
   check that it exited and its session history appears without duplicate rows.
5. Open Stats Overview, Play patterns and Library snapshot. Switch periods and
   check the labels distinguish recorded-period data from imported lifetime totals.
   Open Repair Library and Save Backups, inspect the reasons and versions, and
   cancel the locate-file preview. Use disposable content for any confirmed
   relocation, restore or deletion test.

Report each item as passed or failed, with the screen, game/source and input used
for any failure. Acceptance does not itself authorize publication.

## Recovery and next development

Rollback bundle:
`/home/bts/.local/state/omakade/local-install-4556ebd-20260924-230639/`.
Close Omakade before using its `rollback-binaries.py`. Binary rollback preserves
newer play history; do not restore the older database just to change executables.

After this candidate is accepted, the first cleanup slice should extract the
render and interaction test harness from the 7,235-line `src/app/main.cpp` while
preserving test names, fixture behavior and production startup. Keep that change
separate from behavior changes. Measure cached startup, filtering, artwork work
and repair scans before selecting optimizations. Existing 1,000/10,000-game
fixture thresholds are regression checks, not real-library performance claims.
