# Game Mode

Game Mode turns the computer into a console for a while. It opens Couch Mode on the
display you choose, sends sound there, silences notifications, and puts everything back
when you leave. It works with every source Omakade supports, not only Steam.

## Use it

Open **Settings → Controls** and look under **Game Mode**.

- **Display** is where Couch Mode opens. **Current display** uses the display Omakade is
  already on. A display listed as **off until Game Mode** is turned on when Game Mode
  starts and turned off again when it ends.
- **Sound** is the output used while Game Mode is on. **Current sound output** leaves
  sound alone.
- **Notifications in Game Mode** silences Omarchy notifications for the session.

Choose **Start Game Mode**, or run:

```bash
omakade --game-mode
```

That works whether or not Omakade is already open. To bind it to a key on Omarchy, add a
line to `~/.config/hypr/bindings.lua`:

```lua
o.bind("SUPER + SHIFT + G", "Game Mode", "omakade --game-mode")
```

The desktop entry also carries a **Game Mode** action for launchers that show actions.

To leave, press Start on the controller or F11 on the keyboard and choose **Leave Game
Mode**, or run `omakade --game-mode-exit`. Closing Omakade leaves Game Mode too.

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
| Omakade moves to that workspace in Couch Mode | It returns to its workspace and its previous mode |
| The chosen sound output becomes the default | The previous default returns |
| Notifications are silenced | They are unsilenced |
| The screen is kept awake | Idle behaviour returns to normal |

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
- **A game is still running when you leave.** Settings offers **Stop Games** first. If
  you leave anyway, the game keeps running and moves to your desktop.

## Limits

- Choosing a display and the dedicated workspace need Hyprland with a Lua configuration,
  which is what Omarchy 4 ships. On other setups Game Mode uses the current display and
  still handles sound.
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
  computer's output; the TV has to be listening on it.

## Hardware acceptance

Display, sound and notification changes cannot be exercised in automated tests, which run
without real outputs or a sound server. The session logic is tested against stand-ins,
and the Hyprland commands were checked on a virtual display. Before a release, check on a
real machine:

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
8. With the display powered off, start Game Mode. It reports that the display did not
   turn on and leaves nothing changed.
