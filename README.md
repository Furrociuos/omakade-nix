# Omakade

[![CI](https://github.com/btsouth/omakade/actions/workflows/ci.yml/badge.svg)](https://github.com/btsouth/omakade/actions/workflows/ci.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-8cd3cb.svg)](COPYRIGHT)

**Your games, beautifully together.**

[![Omakade library showing installed games from multiple launchers](docs/assets/library-preview.webp)](https://btsouth.github.io/omakade/assets/omakade-demo.mp4)

[Watch the 18-second demo](https://btsouth.github.io/omakade/assets/omakade-demo.mp4)

Omakade is a Linux game library built for Omarchy. It brings
installed Steam, Lutris, Heroic, Faugus, RetroArch, Battle.net, Epic, GOG, and Amazon games
into one quiet, cover-focused home that follows the active Omarchy theme.

[Project homepage](https://btsouth.github.io/omakade/) ·
[Guide](docs/GUIDE.md) · [Roadmap](PLAN.md) · [Support](SUPPORT.md)

> Omakade is an independent community project. It is not an official Omarchy
> application.

## What you get

- One library for Steam, Lutris, Heroic, Faugus, Battle.net, and GOG games,
  whether the launcher is native or Flatpak
- Emulated games from RetroArch, PCSX2, RPCS3, PPSSPP, Ryujinx, Cemu, melonDS,
  shadPS4, Dolphin, and Xenia, grouped behind one card per console
- The Omarchy palette, font, and transparency, updated live when the theme changes
- Couch Mode, a controller-first fullscreen view with on-screen search
- Game Mode, with a chosen display and sound output, restored when you leave
- Keyboard, mouse, and controller navigation on every screen
- Favorites, collections, tags, completion states, saved filters, and a random pick
- Optional playtime recording with session history, stats, and a Year in Review image
- Local Steam achievements, with optional RetroAchievements, IGDB, SteamGridDB,
  and ProtonDB details
- Versioned save backups for supported emulators
- Sunshine export so Moonlight can start Omakade or any installed game

![Omakade game details showing playtime, IGDB insights, and Steam achievements](docs/assets/game-details.webp)

Play hands each game to the launcher that owns it. Omakade reads launcher data
without modifying it, and the launchers stay responsible for installs, accounts,
updates, cloud saves, DRM, and compatibility tools. Core discovery, browsing,
artwork, and launching work offline with no account or API key.

The [guide](docs/GUIDE.md) covers each source and feature in detail.

## Install

On Omarchy, install Omakade from the Omarchy Package Repository:

```bash
sudo pacman -S omarchy/omakade
```

After that, Omakade updates with normal Omarchy system updates. Launch it from
the application launcher or run `omakade` in a terminal.

The Omarchy repository picks up new releases on its own schedule, so it can be a
few days behind. To install the latest release directly on Omarchy or any Arch
system, download the package and its checksum, verify it, and install it:

```bash
curl -fLO https://github.com/btsouth/omakade/releases/download/v1.14.0/omakade-1.14.0-1-x86_64.pkg.tar.zst
curl -fLO https://github.com/btsouth/omakade/releases/download/v1.14.0/SHA256SUMS
sha256sum -c SHA256SUMS --ignore-missing
sudo pacman -U ./omakade-1.14.0-1-x86_64.pkg.tar.zst
```

For ARM64, replace `x86_64` with `aarch64` in the package filename and download
URL. The same files are under **Assets** on the
[latest release](https://github.com/btsouth/omakade/releases/latest) if you
prefer a browser download.

`pacman -U` upgrades an existing installation in place. Omakade keeps its local
library and settings when the package is upgraded or removed.

## Getting started

Installed games appear without any setup or account. Run `omakade --demo` to
explore the UI with a deterministic fictional library instead.

Use `Ctrl+F` to search, arrow keys to navigate, Enter to open details, Escape
to return, and F11 to enter or leave Couch Mode. The controller Start button
does the same. `Ctrl+M` toggles reduced motion and `Ctrl+D` opens settings and
source diagnostics. `omakade --couch` starts directly in Couch Mode.

Use **Settings → Controls → Start Game Mode** or `omakade --game-mode` for a
dedicated gaming display and sound output. See the [Game Mode guide](docs/GAME-MODE.md)
for setup, recovery, and current limits.

Where to go next in the guide:

- [Include uninstalled Steam games](docs/GUIDE.md#include-uninstalled-steam-games)
- [Set up consoles and emulators](docs/GUIDE.md#consoles-and-emulators)
- [Add a native game manually](docs/GUIDE.md#add-a-native-game-manually)
- [Track play sessions](docs/GUIDE.md#track-play-sessions)
- [Stream with Sunshine and Moonlight](docs/GUIDE.md#stream-with-sunshine-and-moonlight)
- [Back up your library](docs/GUIDE.md#back-up-your-library)

## Build

Requirements:

- CMake 3.24 or newer
- Ninja
- C++20 compiler
- Python 3 for the test suite
- Qt 6.8 or newer with Concurrent, Core, Gui, Network, Qml, Quick, Quick
  Controls, SQL, and Test, plus the SVG and image format plugins
- SDL 3
- libsecret
- libzip

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/omakade
```

## Local data

- Library: `~/.local/share/omakade/library.sqlite3`
- Settings: `~/.config/omakade/config.toml`
- Downloaded artwork: `~/.cache/omakade/`
- Selected custom artwork: `~/.local/share/omakade/artwork/`
- Play sessions: `play_sessions` and `play_baselines` tables in the library

Optional Steam and IGDB credentials are stored by Secret Service, and cached
metadata stays available offline.

See [PRIVACY.md](PRIVACY.md) for retained data and external requests,
[CHANGELOG.md](CHANGELOG.md) for release notes, and the current
[compatibility report](docs/COMPATIBILITY.md) for tested platform layouts.
