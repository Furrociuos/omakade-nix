# September 8 publication candidate

The maintainer authorized pushing the reviewed candidate through btsouth on
September 8. This supersedes earlier local-only instructions in historical
review documents. Tags, public release assets, release publication, and merging
remain gated on exact-candidate acceptance and the checks in RELEASING.md.

The proposed version is 1.8.0 because Home, discovery filters, session recording,
and backup format 2 exceed the original 1.7.1 patch scope. README package links
are prospective until release assets exist; this branch must not reach main first.

## Reconciliation

Start: d33f84955024cfe1d5c7302b4578970e903fae47, based on GitHub main
b1c8311177eb8809ef4f382aa0054d14aa95c1e4. Preserve the existing 30-commit
ancestry, including the cover-preservation merge.

- The port-playtime-game-info, cover-preservation-local, and release-1.7.1
  worktree tips are ancestors of this candidate.
- The untracked feature plan in steam-launcher is an older copy of the tracked
  plan. Its original content is retained in the candidate.
- The old 1.7.1 source archive and cover-local package directory remain untouched.
  They are older artifacts, not publication inputs.
- The console-portals branch contains earlier development history superseded by
  the released console implementation and subsequent ported metadata/session work.
  It remains preserved; do not overwrite newer source with that old snapshot.
- PR #33 and its review branches remain separate deferred TV helper work.
- T3 checkpoint refs remain untouched.

## Current implementation

Home and Up Next, metadata discovery filters, regional title/date evidence,
portrait preservation, unified Game & Artwork, popup navigation, precise session
accounting, and personal backup format 2 are implemented. Historical plans may
still describe their pre-implementation findings. The changelog describes the
current scope; BACKUP-FORMAT.md defines archive coverage and compatibility.

This follow-up bounds rating hover to the rating text, uses the app's Qt Quick
Controls tooltip with a nearby anchor, and places developer/publisher credits
before the entire regional-information group. No new focusable control is added.
Other tooltip handlers are attached to their own title/button labels, with no
second rating-count tooltip attached to a combined information row.

## Acceptance still needed

Automated fixtures do not establish physical-controller or emulator acceptance.
The earlier Home wheel behavior was accepted; the final candidate still needs:

1. Home wheel scrolling during metadata refresh.
2. Keyboard and pad Sources/Filters, Back, and focus after emulator return.
3. Narrow and couch details; hover platform/date/rating; expand Other Names.
4. Game & Artwork scroll, Done, select, reopen, and reset.
5. Known regional titles and previously missing NES covers.
6. A real emulator launch/return and short-session accounting.
7. Backup preview and restore on disposable data with the recorder stopped.

Process-argument attribution cannot observe every internal emulator game change.
Provider aliases and artwork tagging are incomplete. Referenced artwork can keep
cache size above its soft limit. Older copied covers may lack a Current badge.
ARM64 runtime acceptance and disposable package lifecycle checks remain release
gates even when the local x86_64 suite passes.

Validation and candidate hashes are recorded in the PR and local evidence folder
`build/quality-evidence/publication/`; earlier test counts are historical.

## Local validation

- Release configure/build passed; the complete isolated suite passed 201/201
  in 75.23 seconds, including the 12 new tooltip cases and late-cover navigation.
- Narrow and couch tooltip screenshots were inspected. Credits precede regional
  details; platform/date hover and missing ratings do not activate the tooltip.
- Empty staged install inspected: app, recorder, profiles, service, metadata,
  icons, licenses, and documentation only. Isolated staged smoke passed.
- Desktop and AppStream validation passed. SBOM generator tests passed 2/2.
- Core tests include disposable restore/migration, session recovery, and daemon
  duplicate-owner/restart checks. No live library restore was performed.

The maintainer also launched Z-A and returned using Super+W on the previous
installed d33f849 build. Ryujinx's log confirms F5 paused emulation; its process
exited after window closure and the recorder closed a 130-second session.
Paused time remains counted while the emulator runs. This observation is useful
runtime evidence for that installation, not acceptance of this final candidate.

## Library return follow-up

The maintainer reported overlapping captions in Recent immediately after returning
from Z-A. An isolated sequence that hides the library, updates filters/layout,
and returns reproduced overlapping delegates. Forcing layout on visibility alone
did not resolve it; disabling the desktop GridView reuse pool did. Normal viewport
caching remains enabled and Home is unchanged. Regression fixtures cover repeated
hide/update/return, scrolling, window and cover-size changes, plus recording a
launch into Recent while Details hides the grid.

The initial GitHub AI findings job failed because the account lacks a Copilot
license, before performing a review. This is separate from the regular CodeQL
checks. Do not count that failed job as a completed review.

Follow-up validation: release build and 203/203 isolated CTests passed in 80.31
seconds. This includes both library regressions, tooltip checks, Home wheel
checks, and thousand-game startup/navigation. A fresh staged install and isolated
smoke passed. The final fix only disables desktop delegate reuse; the ineffective
visibility relayout workaround was removed. Human acceptance of the new return
behavior is still pending.

## Home polish follow-up

The maintainer requested the focused pre-release polish pass. Home now removes
its introductory slogan and puts Quick access after the game shelves. The
featured game has separate Play and Details actions. Play uses the existing
preferred-installation launch path and opens details as the return surface.
Unavailable featured games retain Details as the navigation fallback.

Home regression fixtures cover directional movement from Play to Details,
return focus, queue actions, filter restoration, and desktop/Couch layouts.
The new candidate needs a fresh manual check; acceptance of an earlier binary
and its CI/package results do not apply to this follow-up automatically.

Manual acceptance:
- Home: Play, Details, Up Next, and Quick access with keyboard and controller.
- Details: launch prominence, narrow layout, artwork dialog, and Back focus.
- Library: Recent labels after returning, empty filters, Sources and Filters.
- Couch: readable controls and every dialog usable without a mouse.

Emulator diagnostics remain stopped. Physical controller, emulator-return,
ARM64 hardware, and recorder/backup acceptance gates remain open.

Validation for this follow-up: Release build passed; all 203 isolated CTests
passed in 79.52 seconds; fresh staged installation and isolated smoke passed.
Inspected refreshed Home renders at 600x800 and 1280x720. All 22 Home fixtures
passed, including desktop/Couch navigation, delayed layout, and wheel behavior.
Local logs: build/home-polish-{check,full-check,install,smoke}.log.
