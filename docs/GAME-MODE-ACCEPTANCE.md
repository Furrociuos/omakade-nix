# Game Mode acceptance, 2026-10-01

Local candidate only. Nothing committed, pushed or published during this polish pass.
The final real-device Stop Games and Leave check passed. Publication still requires
the maintainer's explicit approval.
The reported emulator picture issue now has a verified game-specific workaround.

Base commit: `323d276`, plus the changes in this worktree.

Current responsiveness-fix executable SHA-256:
`4e764b64f9f7d1ff61bb8f269421f2f383a0b686784a692993ac08b10e1defdc`

Staged locally at `build/stop-fix-install/usr/bin/omakade` and installed separately at
`~/.local/lib/omakade/game-mode-1.14.0-candidate/usr/bin/omakade`, with the matching
session daemon, process profiles and documentation. **Passed the final real-desktop check on 2026-10-01.**
The default installed launcher was not replaced.

This replaces the earlier side-by-side executable with SHA-256
`240debc77feabea43e54e3825564627629fb23f1a51de2eb1b3bcc1e890bb289`. Its hardware
results are historical evidence; that executable remains in `build/polish-install`.

## Freeze report and current fix

The maintainer reported that both RetroArch and Omakade freeze/crash when the test
closes a game, and supplied an Application Not Responding dialog naming RetroArch.
This overrides any earlier suggestion that normal cleanup exit codes alone prove
the workflow is healthy. Repeated hardware tests have stopped at the maintainer's
request. The maintainer subsequently authorized the single final check reported below.

Source inspection found synchronous full-library/process scans before opening
Game Mode controls, during confirmation, and after stopping games. These now run
on workers. Controls open before discovery; Back to Library remains the initial
focus, the confirmed list is revalidated on a worker, and post-stop discovery
cannot reuse a scan started before the stop. Discovery cancels on service teardown.
Settings use the same asynchronous result. A delayed artwork callback now checks
that its objects still exist before accessing them.

The paired symptom was then reproduced in the isolated omabox with copied real
playlists, the cached library, user process profiles and a copy of Mario's ROM.
Debugger stacks captured two separate stalls:

- Omakade's main thread was in `QFileInfo::canonicalFilePath`, called through
  `ProcessMatcher::match`, `GameStop::plan` and `GameStopService::liveGames`.
  Discovery now computes emulator matches once per process snapshot and reuses
  them across library rows, in addition to running off the UI thread.
- RetroArch's main thread was waiting in `wl_display_dispatch_queue` through Mesa
  EGL after Omakade covered it. With GLCore, VSync on reproduced its Not Responding
  dialog; VSync off avoided that stall. Threaded video alone did not fix it.
  These are isolated software-rendering results, not proof of the host GPU cause.

The new executable completed both real-process flows inside the isolated desktop:

1. VSync off: controls and confirmation opened promptly; Cancel retained Mario and
   Game Mode. Confirmed Stop Games and Leave asked RetroArch to close without force,
   removed the session journal, and kept Omakade alive.
2. VSync on: RetroArch became unresponsive again. Omakade remained responsive,
   displayed confirmation and force-closed only the attributed game after the
   grace period. Game Mode then exited and Omakade remained alive.

Omakade also quit normally afterward. No real-desktop app was launched during the
isolated fix pass that preceded the final authorized hardware check. The Mario-only host override now adds `video_vsync = "false"` beside
`video_driver = "glcore"`. Its previous contents are backed up in
`build/game-mode-hardware/mario-override-before-vsync.cfg`. This VSync setting has
now passed the final hardware check below; tearing during gameplay was not
assessed. Global RetroArch settings and the CRT shader are unchanged.

- Targeted devbox run: **19/19 passed**, 9.87 seconds.
- Added gated-worker checks for responsive discovery, discarding superseded
  results, and revalidating the confirmed list without blocking the UI thread.
- Added attribution parity coverage for reused emulator matches. Corrected a
  synthetic test PID that could collide with the test runner's protected PID.
- Game Mode UI checks cover safe Back focus and asynchronous discovery.
- Final devbox full suite: **318/318 passed**, 478.89 seconds.
- Full build completed. The remaining unit binaries were relinked against the
  updated static libraries and rerun: **15/15 passed**, 59.95 seconds. Omakade's
  executable SHA-256 was unchanged and matches the isolated checks.
- Staged installation and `git diff --check` passed. Older results below are retained
  as history; the checks above apply to the responsiveness-fix candidate.

## Changes

- Named display and sound selectors preserve unavailable saved choices.
- Compact Game Mode controls show the display, sound output and running games.
  Back to Library keeps the session active. Stop Games and Leave has a separate
  confirmation with Cancel focused. Failed stops keep Game Mode active.
- Device discovery no longer blocks Start or Leave on the UI thread.
- Fullscreen is restored when the library regains focus.
- A dead, unreaped Omakade process no longer blocks recovery.
- A side-by-side installation finds its own process profiles.
- Demo/test artwork stays local, fixing three fixture shutdown hangs in DNS lookup.
- README, guide, unreleased 1.14.0 metadata and the isolated-desktop demo are prepared.

## Automated and isolated checks

- Full devbox suite: **318/318 passed**, 518.00 seconds. This run used executable
  `30f53faa06b9c8048f2e7b266bbc93448947e54f159de255a705f75693a5de0a`.
- After adding the dead-owner recovery fix, stop/leave safety assertions and
  large-screen text scaling: **17/17 relevant tests passed**, 8.91 seconds.
  These cover Game Mode, stop services and dialogs, and Controls settings.
- The Game Mode UI tests cover entry from Desktop and Couch Mode, safe initial focus,
  Back to Library, safe Cancel when games are running, ignoring unrelated stop
  completion, retaining the session on failure, and leaving after successful stop.
  Stop completion in these UI tests is injected; service tests cover the stop engine.
- The final Game Mode unit target also passed after making its discovery gate atomic.
- Recovery has a real zombie-child regression test. Device discovery has a
  gated-worker responsiveness test. Device selectors cover missing saved outputs.
- Visual/input checks in the isolated omabox: dark 1600x900 controls, keyboard
  display selection and persistence, Escape/Return, and light 1280x720 and 3840x2160 controls.
- Earlier isolated regression: 11/11 return-from-game assertions passed after the
  fullscreen fix. The original executable fails the same regression.
- An actual RetroArch process in the isolated desktop passed the complete Stop Games
  and Leave flow: Cancel retained both game and session, then confirmation stopped
  RetroArch, removed the session journal and left Omakade running. This used the
  installed process profiles, not injected stop completion.
- AppStream validation, staged installation and `git diff --check` passed.

The previous three UI timeouts were traced with gdb to a worker blocked in DNS
resolution while loading artwork for demo fixtures. The previous first-frame
failure was timing-sensitive; the full successful run above includes that check.

## Real hardware evidence

Historical runs before this polish pass:

- 29/29 physical session assertions on the original candidate: both Dell outputs,
  analog sound and G733 headset routing, notification state, output disable/enable,
  recovery and restoration of original monitor modes/workspaces/window geometry.
- 12/12 short game-window assertions after the fullscreen fix: Hollow Knight and
  Super Mario World, each closed deliberately after three seconds.

During this polish pass, using the installed `30f53f...` candidate:

- Direct Steam comparison, without Game Mode: Hollow Knight stayed open for
  30 seconds, with an uncorked game audio stream, then closed on request.
- With Steam already open on another workspace, Hollow Knight opened on the
  selected Game Mode monitor. The final run on DP-2 stayed open for 20 seconds,
  routed its active audio stream to the selected headset, and restored library
  focus and fullscreen after deliberate close.
- Super Mario World through RetroArch opened on DP-2's Game Mode workspace,
  remained open for 20 seconds and had an uncorked stream on the selected headset.
  The compositor recorded fullscreen on the whole display. The maintainer
  nevertheless reported the picture looking wrong in size/resolution.
- Stop Games and Leave did **not** complete in the real-device driver. Do not count
  its timeout, cleanup commands or injected UI test as successful hardware acceptance.
- Several runs were interrupted by activity on the other monitor, Steam Big Picture
  or a screenshot attempt. Their focus/placement failures are inconclusive.
  One interrupted Omakade process was observed exited by SIGKILL with no new core
  dump or matching OOM evidence found. The source of that kill is unknown.
- Final cleanup verified no test games, no active host Game Mode journal, DND off,
  and restoration of the current Bluetooth output. Future Game Mode preferences
  were set to DP-2 and the built-in analog speaker output, as requested.

Final candidate hardware comparison (`240debc...`):

- Direct RetroArch launch and launch through Game Mode both survived to the title
  screen. The Game Mode launch was fullscreen on the selected workspace.
- Both window captures show similar offset/cropping. They are 2048x1152 captures
  on a 2560x1440 display at 125% scale. A subsequent full 2560x1440 monitor
  capture reproduced the same cropping with RetroArch launched directly, and the
  maintainer supplied a matching screenshot. This is not confined to window capture
  and occurs without Omakade. RetroArch 1.22.2-5 uses Vulkan and CRT-Royale here;
  a temporary OpenGL Core override subsequently corrected the picture.
- The follow-up Game Mode capture aborted because its emulator window disappeared
  before capture. Its exit status was not recorded, so no cause is established.
  The harness now needs process-exit diagnostics before this run is repeated.
- The automated F11 menu-opening step timed out. Stop and Leave was therefore
  not exercised on hardware in this run. Cleanup closed the games deliberately;
  direct RetroArch and Omakade exited normally with status 0, and the session
  journal was removed. No crash was observed in this comparison.
- RetroArch settings on the real desktop were not changed.

Renderer comparison on DP-2, 2560x1440 at 125% scale:

- OpenGL Core (`glcore`) with the existing CRT-Royale preset: the full title frame
  is centered with both side borders and the bottom copyright visible.
- Vulkan with shaders disabled: the right and bottom remain cropped. The shader
  is therefore not required to trigger this reproduction.
- Both variants used the NVIDIA RTX 4070 SUPER and exited normally with status 0.
  The test used temporary `--appendconfig` files with config saving disabled.
  The global RetroArch driver remains Vulkan.
- Installed a game-specific override at
  `~/.config/retroarch/config/Snes9x/Super Mario World (NA).cfg` containing
  `video_driver = "glcore"`. No previous file existed. Removing that file undoes
  the workaround. The global configuration hash is unchanged.
- Through the final Game Mode candidate, the full title frame is now centered with
  both sides and the copyright visible, with CRT-Royale preserved. Seven checks
  passed: game remains visible, fullscreen on the target workspace, Omakade remains
  alive, CLI leave after deliberate game close, audio restoration, notification
  restoration, and normal Omakade exit. This does not exercise Stop Games and Leave.

The original Steam run recorded Proton 11.0 `wine64-preloader` PID 1226819 dying
with SIGSYS / SYS_SECCOMP while starting `services.exe` at 20:16:40 EDT. Core
inspection identifies x86_64 syscall 158 (`arch_prctl`). The rejecting policy is
unknown. No repeat core was recorded in the direct Steam comparison or subsequent
launch checks; this does not establish the cause of the original failure.

## Final hardware check, responsiveness-fix candidate

The maintainer explicitly authorized this run after the isolated fixes. The exact
`4e764b64…` executable passed **9/9 automated hardware assertions**:

- Mario launched fullscreen on the selected DP-2 Game Mode workspace. The monitor
  capture shows a centered, complete image with CRT-Royale and the Mario-only
  GLCore/VSync override, including the right border and bottom copyright text.
- Omakade stayed alive during the game. Controls and confirmation appeared in
  2.5 to 2.8 seconds including screenshot/OCR overhead, not pure UI latency.
- Cancel retained Mario and Game Mode. Confirmed Stop Games and Leave closed the
  emulator, removed the session journal, and kept Omakade responsive.
- No Not Responding dialog remained. The default audio output and notification
  state returned to their previous values. Omakade subsequently exited with code 0.

Cleanup restored the previous workspaces and focus. A separate readback found no
Omakade or RetroArch windows, no recovery journal, DP-2 on workspace 1 and HDMI-A-1
on workspace 2. The normal installed launcher remains unchanged.

This was a short title-screen/stop check, not extended gameplay or a tearing/audio
quality assessment. The prior hardware problem is no longer reproduced by this
workflow. No new release or publication approval is implied by permission to test.

## Remaining acceptance

The maintainer must approve publication of the exact final candidate. There are
no remaining automated blockers from the stop/freeze investigation. Changes remain
uncommitted in the local release-candidate worktree.

No TV, HDMI audio, physical cable unplug, panel-off behavior, real controller,
Heroic/Lutris game or audible-speaker observation was verified. Whole-desktop audio
is accepted. Gamescope, HDR and per-game audio routing remain outside this version.

## Evidence

- `build/game-mode-hardware/verified-final-results.json`: 9/9 final hardware assertions.
- `build/game-mode-hardware/verified-final-*.png`: monitor image, controls, confirmation
  and return to the library on the corrected candidate.

- `build/game-mode-hardware/stop-fix-tests.log`: final 19-test devbox run.
- `build/game-mode-hardware/stop-fix-full-suite.log`: 318-test final suite.
- `build/game-mode-hardware/stop-fix-relinked-tests.log`: 15 rebuilt unit targets.
- `build/game-mode-hardware/optimized-stop-results.json`: isolated result manifest.
- `build/game-mode-hardware/isolated-omakade-bt.txt` and
  `isolated-retroarch-bt.txt`: debugger stacks of the reproduced stalls.
- `build/game-mode-hardware/isolated-freeze-before.png`: original paired symptom.
- `build/game-mode-hardware/optimized-stop-*.png` and `optimized-stop-app.log`:
  responsive controls, Cancel, graceful stop, and exit with the VSync workaround.
- `build/game-mode-hardware/optimized-hung-*.png`: original RetroArch stall retained,
  with Omakade successfully confirming, force-closing the game and leaving.
- Isolated environment: Omarchy `4.0.0.r6693.gf45461a-1`, theme Osaka Jade,
  virtual 2560x1440 output with 1.25 compositor scale, software Mesa, no audio or
  physical display. This does not validate physical fractional scaling or the CRT shader.

- `build/game-mode-hardware/workaround-results.json`, `workaround-game-mode.png`:
  seven passing hardware checks and the corrected picture through Game Mode.

- `build/game-mode-hardware/renderer-results.json`, `renderer-glcore-crt.png`,
  `renderer-vulkan-plain.png` and matching logs: temporary renderer comparison.

- `build/game-mode-hardware/compare-results.json`, `compare-direct.png`,
  `compare-game-mode.png`, `compare-menu.png`: final hardware comparison.
- `build/game-mode-hardware/isolated-stop-menu.png`, `isolated-stop-confirm.png`:
  actual isolated emulator stop flow.
- `build/game-mode-hardware/polish-full-suite.log`: complete 318-test run.
- `build/game-mode-hardware/candidate-focused.log`: final 17-test run.
- `build/game-mode-hardware/steam-direct-results.json`: direct Steam comparison.
- `build/game-mode-hardware/games-polish-results.json`: final hardware driver,
  including unsuccessful stop/leave assertions.
- `build/game-mode-hardware/games-polish-interrupted*-results.json`: interrupted runs.
- `build/game-mode-hardware/final-desktop.json`: cleanup state.
- `build/game-mode-hardware/polish-*.png`, `candidate-4k-controls.png`: UI evidence.
- `tools/game-mode-acceptance.sh`: isolated compositor/audio acceptance driver.
- `docs/assets/game-mode-demo.mp4`: 24-second isolated-desktop preview, not hardware
  evidence. Recorded before the last header text-scaling adjustment.
