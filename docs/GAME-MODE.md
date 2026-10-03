# Game Mode

Game Mode turns the computer into a console for a while. It opens Couch Mode on the
display you choose, sends sound there, silences notifications, and puts everything back
when you leave. It works with every source Omakade supports, not only Steam.

[Watch the local Game Mode demo](assets/game-mode-demo.mp4) (isolated desktop).

## Use it

Open **Settings → Controls** and look under **Game Mode**.

- **Display** is where Couch Mode opens. **Current display** uses the display Omakade is
  already on. A display listed as **off until Game Mode** is turned on when Game Mode
  starts and turned off again when it ends.
- **Sound** is the output used while Game Mode is on. **Keep current sound output** leaves
  sound alone.
- **Notifications in Game Mode** silences Omarchy notifications for the session.
- **Keyboard shortcut** adds Super + Ctrl + G, which Omarchy leaves free. It starts Game
  Mode, and pressing it again leaves. If the key is already used for something else,
  Omakade says what and adds nothing.

Choose **Start Game Mode**, or run:

```bash
omakade --game-mode
```

That works whether or not Omakade is already open. The shortcut is one line in
`~/.config/hypr/bindings.lua`, so you can also add it yourself, on any key:

```lua
o.bind("SUPER + CTRL + G", "Game Mode", "omakade --game-mode-toggle")
```

`omakade --game-mode-toggle` starts Game Mode, or leaves it when it is on. With a game
still running, it opens the Game Mode controls over the game instead, so one key press
never stops a game or leaves it behind. **Back to Game** closes them again, as does
Escape or the controller's back button.

If Game Mode launched Omakade, leaving closes it after restoring your desktop. If
Omakade was already open, leaving returns it to its previous window and layout.

The desktop entry also carries a **Game Mode** action for launchers that show actions.

To leave, press the shortcut again, or press Start on the controller or F11 on the
keyboard while Omakade is in front and choose **Leave Game Mode** in the compact
controls. While a game is in front, the shortcut is the way in: Start belongs to the
game. **Back to Library**
keeps the session running. When a game is running, the controls offer **Stop Games and
Leave…** with a separate confirmation, or **Leave with Games Running**. You can also run
`omakade --game-mode-exit`. Closing Omakade leaves Game Mode too.

## Keep a TV for games only

Hyprland turns on every connected display by default, so a TV becomes part of the
desktop. To keep it off except in Game Mode, disable it in `~/.config/hypr/monitors.lua`:

```lua
hl.monitor({ output = "HDMI-A-2", disabled = true })
```

Use the connector name shown in the **Display** choice. A mode, position or scale you
set for that output is kept; Game Mode only switches it on and off.

## What it changes and puts back

| While Game Mode is on | When you leave |
| --- | --- |
| A display that was off is turned on | It is turned off again |
| A display that was already on shows a Game Mode workspace | It shows the workspace it had before |
| Omakade moves to that workspace in Couch Mode | It returns to the same spot in its workspace and its previous mode |
| The chosen sound output becomes the default | The previous default returns |
| Notifications are silenced | They are unsilenced |
| The screen is kept awake | Idle behaviour returns to normal |

While a tiled Omakade window is away, a placeholder window holds its place in the
layout, so the windows beside it keep their size and position. Omakade already
fullscreen in Couch Mode covers its workspace and returns fullscreen.

Game Mode only undoes its own changes. A display that was already on stays on. If you
switch to another sound output during the session, that choice is kept. Notifications
that were already silenced stay silenced. Windows on other displays and workspaces are
not touched.

Starting either completes or undoes itself. If the display does not turn on within ten
seconds, or the sound output is not available, nothing is left changed and Omakade says
what was missing.

## If something interrupts it

- **The display is unplugged or disabled.** Game Mode ends and sound goes back to the
  previous output. Windows that were on the display move to another one, as Hyprland
  does for any display that goes away.
- **Omakade crashes or is killed.** The changes are recorded in
  `~/.local/state/omakade/game-mode.json`. The next time Omakade starts, or when you run
  `omakade --game-mode-exit`, the display is turned off again and sound and notifications
  are put back.
- **A game is still running when you leave.** The controls name running games and offer
  **Stop Games and Leave…**. A failed stop keeps Game Mode active. If you choose
  **Leave with Games Running**, the game moves to the workspace Omakade returns to and
  keeps running there. Its sound follows the desktop's output back.

## Limits

- Choosing a display and the dedicated workspace need Hyprland with a Lua configuration,
  which is what Omarchy 4 ships. On other setups Game Mode uses the current display and
  still handles sound.
- Sound switching requires `pactl` (`libpulse` on Arch) and a running
  PulseAudio-compatible server, such as PipeWire with `pipewire-pulse`. Omarchy
  includes these. Without them, leave **Sound** at **Keep current sound output**.
- Sound is switched for the whole desktop, not only for the game. Music or a call that
  follows the default output moves to the Game Mode output until you leave.
- Games open where Hyprland places new windows, which is the Game Mode workspace while
  it has focus. A launcher that was already open on another workspace can still put a
  game there.
- Game Mode does not run games inside Gamescope and does not manage HDR, variable refresh
  rate or resolution scaling. Configure those in the game's launcher. The optional
  [TV gaming skill](../skills/omakade-tv-gaming/SKILL.md) covers a Gamescope wrapper for
  people who want one.
- Turning the TV itself on or switching its input is not handled. Game Mode enables the
  computer's output; the TV has to be listening on it. Some displays remain visible
  to the computer while powered off or showing another input, so software cannot
  reliably detect whether the panel is showing the game.

## RetroArch picture cropped on a scaled display

If RetroArch's picture is enlarged or cut off, compare a direct launch with Game
Mode first. On a tested 125% Wayland display, Vulkan cropped the picture even with
shaders disabled; OpenGL Core (`glcore`) displayed it correctly with CRT-Royale.
This workaround was verified with Super Mario World using Snes9x. Compatibility
with other cores has not been established.

To limit the change to one game, use a RetroArch
[game override](https://docs.libretro.com/guides/overrides/) under
`~/.config/retroarch/config/<core-name>/<ROM-filename-without-extension>.cfg`:

```ini
video_driver = "glcore"
```

Preserve any existing settings in that file. Remove the added setting to undo it.
Omakade does not change RetroArch's renderer automatically.

If GLCore freezes when the game is covered by another window, a separate
workaround is `video_vsync = "false"` in the same game override. This avoided a
Wayland/EGL wait in an isolated software-rendered comparison, and the game then
closed normally through Stop Games and Leave. The same Mario-only override then
passed a physical DP-2 launch, Cancel and Stop Games and Leave check with CRT-Royale.
Extended gameplay and tearing were not assessed. Disabling VSync may affect tearing. Remove this setting to
restore the previous behavior. Omakade does not apply either override automatically.

## Hardware acceptance

The automated acceptance script exercises Hyprland, notifications, and a private
PipeWire server with virtual displays and null sound outputs. It does not prove
physical display behavior, audible sound, or real game behavior. The following
checks can also be driven automatically on a real machine; record physical
observations separately:

1. One display: start and leave Game Mode from Settings. The previous workspace and
   window layout return exactly.
2. A second display that is normally on: Couch Mode opens there, and leaving restores the
   workspace it was showing and returns focus to the first display.
3. A display that is disabled in `monitors.lua`: it turns on, and turns off when leaving.
4. A chosen sound output: game audio plays there, and the previous output returns.
5. Launch a Steam game, an emulator and one Heroic or Lutris game. Each opens on the
   Game Mode display, and Couch Mode has focus again after quitting.
6. Kill Omakade during Game Mode, then start it again. The display, sound and
   notifications are put back.
7. Unplug or disable the display during Game Mode. Game Mode ends and sound returns.
8. With the display powered off, start Game Mode. If Hyprland cannot enable its
   output, it reports the failure and rolls back. If the display still advertises
   an active connection, Game Mode may start; verify this separately from the
   automated timeout test.
