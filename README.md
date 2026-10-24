# RWE: B4

**Total Annihilation, running on an open-source engine.**

RWE: B4 is a fork of [Robot War Engine](https://github.com/MHeasell/rwe)
that runs Total Annihilation's maps, units, scripts, interface and media
from your own game installation. It brings together documented original
behaviour, two skirmish AI engines, multiplayer, saves and replays.

*Bot bot boom boom — four B's, so B4 for short.*

[![Build](https://github.com/CubeB/RWE-B4/actions/workflows/build.yml/badge.svg?branch=revival)](https://github.com/CubeB/RWE-B4/actions/workflows/build.yml)
[![Downloads](https://img.shields.io/badge/download-pre--release-blue)](https://github.com/CubeB/RWE-B4/releases)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)

[**Download**](https://github.com/CubeB/RWE-B4/releases) ·
[Get started](#get-started) · [Playing](#playing) · [Build](#build) ·
[Documentation](#documentation) · [Report a bug](https://github.com/CubeB/RWE-B4/issues)

> **Bring your own game data.** RWE includes no Total Annihilation assets.
> You need your own copy of the game. This is an actively developed project;
> builds are pre-releases and compatibility work is ongoing.

## What you can do

| Feature | What to expect |
| --- | --- |
| **Play with original game data** | The engine reads TA archives, terrain, models, sprites and definitions directly, and executes the shipped COB unit scripts. |
| **Choose your opponent** | Use **Original TA**, the default computer player implemented from documented original behaviour, or **RWE AI**, a separately developed opponent with its own tactics and transport planning. |
| **Play multiplayer** | Deterministic lockstep games, LAN discovery and invite-code connections across routers. Use the same build and game data on every machine. |
| **Save and return** | Save and load the simulation, including running unit scripts, projectiles and the random generator. Record and play back RWE replays. |
| **Use the original front end** | Game-data-driven menus, campaign selection, movies, sound effects and situational music. |
| **Explore the engineering** | Behaviour findings, demo analysis, profiling tools and automated tests are kept alongside the implementation. |

The goal is to reproduce the original's behaviour with evidence. Intentional
changes are recorded separately from unfinished work. See the
[compatibility guide](docs/compatibility.md) and
[roadmap](docs/ROADMAP.md) for the details.

## Get started

### 1. Download a build

Open the [releases page](https://github.com/CubeB/RWE-B4/releases) and choose
a package for your system:

| Platform | Package |
| --- | --- |
| **Windows** | Installer (`.exe`) or portable archive (`.zip`) |
| **Linux** | AppImage (`.AppImage`) |

The **nightly** release follows development on `revival`. Dated **weekly**
releases provide snapshots you can return to. Read the release notes for the
build revision and included changes.

### Install from a terminal

Install the nightly build for your user account. **No Python, package manager,
compilation, or administrator access is needed.** Published builds support
x86-64 Windows and Linux.

**Windows (PowerShell):**

Paste this command into PowerShell:

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/CubeB/RWE-B4/revival/install.ps1)))
```

The installer opens the game setup assistant. Choose automatic detection or
browse to your Total Annihilation folder, import the files, and play. To retry
setup later from the terminal:

```powershell
rwe-setup --from "C:\GOG Games\Total Annihilation"
rwe
```

Uses built-in Windows PowerShell 5.1 or newer. Installs in `%LOCALAPPDATA%\RWE`,
adds that directory to your user PATH, and creates a Start menu shortcut.

**Linux:**

```bash
curl -fsSL https://raw.githubusercontent.com/CubeB/RWE-B4/revival/install.sh | sh
~/.local/bin/rwe-setup --from "/path/to/Total Annihilation"
~/.local/bin/rwe
```

The Linux installer also runs setup: it opens the folder picker when available,
or tries automatic detection in the terminal. If detection fails, use the
`rwe-setup --from` command above.
If RWE's data folder already contains both base-game archives, setup uses them
without needing the original Total Annihilation installation. An explicit
`rwe-setup --from` still imports additional files and skips matching files.

Uses standard Linux shell tools and `curl` (or download with `wget` and run
`sh install.sh`). Installs in `${XDG_DATA_HOME:-$HOME/.local/share}/rwe/program`
and puts launch commands in `~/.local/bin`. Add `~/.local/bin` to PATH if needed.
AppImages require FUSE; without it, run
`APPIMAGE_EXTRACT_AND_RUN=1 ~/.local/bin/rwe`. A graphical desktop and OpenGL
are required to play.

Both installers check release checksums. Run the same command again to update;
previous versions, imported game data, and saves are retained. Replace the
game-data folder with your own copy of Total Annihilation.

On Linux, you can also run `~/.local/bin/rwe-update` (or `rwe-update` when it is
on PATH). It remembers the installation directories and release channel,
checks for an updated release, and skips game-data setup. Already downloaded
AppImages are reused after verifying their checksum. To switch releases, run
`RWE_RELEASE=nightly ~/.local/bin/rwe-update`.

To choose another release or directory on Windows:

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/CubeB/RWE-B4/revival/install.ps1))) -Release weekly-2026-10-19 -InstallDir "$env:LOCALAPPDATA\RWE"
```

On Linux, set `RWE_RELEASE`, `RWE_INSTALL_DIR`, or `RWE_BIN_DIR` on the `sh`
command, for example:

```bash
curl -fsSL https://raw.githubusercontent.com/CubeB/RWE-B4/revival/install.sh | RWE_RELEASE=weekly-2026-10-19 sh
```

From a checkout, use `& .\install.ps1` or `sh install.sh`.

To install and import in one step, add `-From "C:\GOG Games\Total Annihilation"`
to the Windows install command, or set `RWE_TA_DIR="/path/to/Total Annihilation"`
on the Linux `sh` command. For updates that do not need game setup, use
`-SkipSetup` on Windows or `RWE_SKIP_SETUP=1` on Linux. Setup always uses your
own game data; it does not download Total Annihilation.

### 2. Import your Total Annihilation data

On Windows, leave **Open game setup** checked at the end of the installer.
Choose **Import game files** to search automatically, or use **Browse** to
select your Total Annihilation folder. When setup finishes, choose **Play now**.
You can reopen **Import Game Data** from the Start menu. With the portable
ZIP, extract it first and double-click **Import Game Data.cmd**.

Use **Play Robot War Engine** for the game and **Multiplayer Lobby** to arrange
multiplayer matches. New installers use a stable install folder; imported data and
saved games remain in your user data directory.

The command-line setup is also available from the game directory:

```powershell
.\rwe_setup.exe
.\rwe.exe
```

The setup tool searches common GOG, Cavedog, and Steam installation locations.
Steam detection includes `Steam/steamapps/common/Total Annihilation`, additional
libraries registered in `libraryfolders.vdf`, and the game's app manifest.
On Linux it also checks the normal Steam, Steam Deck, and Flatpak Steam folders.
If it cannot find your copy, point it at the directory containing `totala1.hpi`:

```powershell
.\rwe_setup.exe --from "C:/GOG Games/Total Annihilation"
```

On Linux, run setup through the AppImage. With Zenity installed on a graphical
desktop, running `setup` without extra options opens a folder picker and
shows import progress. Explicit options continue to use the command-line tool:

```bash
chmod +x Robot_War_Engine-*.AppImage
./Robot_War_Engine-*.AppImage setup
./Robot_War_Engine-*.AppImage
```

Keep one matching AppImage in the directory when using these wildcard commands.
For terminal setup with an explicit source, add
`setup --from "/path/to/Total Annihilation"` instead.
Setup imports the archives, movies and soundtrack. Running it again copies
only files that are missing or incomplete.

| Setup option | Purpose |
| --- | --- |
| `--dry-run` | Preview the import without changing files |
| `--link` | Use hard links where supported, avoiding duplicate data on the same volume |
| `--to <path>` | Choose a different data directory |

If you import to a different directory, start the engine with
`rwe --data-path <path>`.

<details>
<summary><strong>Data locations and manual installation</strong></summary>

The default data directories are:

| System | Directory |
| --- | --- |
| Windows | `%APPDATA%/RWE/Data` |
| Linux | `${XDG_DATA_HOME:-$HOME/.local/share}/rwe/Data` |

An existing legacy `~/.rwe` directory remains in use until the new location
exists. Put game archives directly in the data directory; the engine mounts
`.hpi`, `.ufo`, `.ccx`, `.gpf` and `.gp3` files. Put GOG's `Data/*.zrb`
movies in `Data/movies/` and the soundtrack's `.mp3` files in `Data/music/`.
Filename lookups are case-insensitive.

</details>

## Playing

### Pick an AI

Computer seats default to **Original TA**. In game setup, cycle a player slot
to **RWE AI** to use RWE's Balanced personality instead. You can mix the two
engines in one match. The selected engine is recorded in saved games.

RWE AI is developed and tuned independently; its improvements are not claims
about how the original computer player behaved. For the design and tuning
process, see [AI architecture](docs/ai-architecture-proposal.md),
[AI tuning](docs/AI-TUNING.md) and the
[AI engine decision](docs/adr/0005-the-original-computer-player-is-the-default-and-rwes-is-an-option.md).

### Play with friends

Use the in-game multiplayer screens for LAN games. For an internet game,
the host can create an invite code with `/invite` in the battleroom. Share
that code with the guest; the host accepts a waiting player with `/accept`.
Some router configurations need a reply-code exchange. Follow the
[connection guide](docs/NAT-PUNCH.md) for the complete flow.

The separate [Electron launcher](#the-launcher) provides another lobby path.
Both players should use the same engine build and matching maps and mods.

### Essential controls

| Input | Action |
| --- | --- |
| Left click / drag | Select units |
| Right click | Give a contextual order |
| Arrow keys | Scroll the map |
| `+` / `-` | Change game speed |
| `Pause` | Pause or resume |
| `Tab` / `F2` | Open the game menu |
| `Esc` | Close a menu, cancel the current order mode or clear selection |
| `Ctrl`+`A` | Select all your selectable, completed units |
| `Ctrl`+`S` | Select your units on screen |
| `Ctrl`+`Z` | Select units matching the selected types |
| `Ctrl`+`C` | Select and track your commander |
| `Ctrl`+`D` | Toggle self-destruct for the selection |
| `Ctrl`+digit | Assign a control group |
| digit / `Shift`+digit | Recall a group / add it to the selection |
| `Ctrl`+`Shift`+digit | Add the selection to a control group |
| `T` | Track the selection |
| Grave accent (`` ` ``) | Toggle health bars |
| `F1` | Help overlay |
| `F9` | Network overlay |
| `F10` / `F11` | In-game / global debug window |

Order shortcuts come from the loaded game's GUI data. For the shipped TA
interface, use plain `S` for Stop; `Ctrl`+`S` is a selection shortcut.
The [keyboard reference](docs/TOTALA-EXE-KEYBOARD.md) documents the bindings
and remaining differences.

## Build

```bash
git clone --branch revival --recurse-submodules https://github.com/CubeB/RWE-B4.git
cd RWE-B4
```

Use a **Release** build for playing and measuring performance. Use **Debug**
when developing with assertions and diagnostics.

SDL3 and its mixer are vendored as submodules and built with the project, so
there are no SDL packages to install.

CI covers Linux (gcc-14, clang-18), Windows MSVC 2026 and Windows MinGW64, in
both Debug and Release. **MSYS2/MinGW64 is what this fork is developed and
tested against daily.**

<details>
<summary><b>Windows — MSYS2 / MinGW64</b> (the recommended route)</summary>

<br>

Install [MSYS2](https://www.msys2.org/), then in the terminal it opens:

```bash
pacman -S git make unzip autoconf automake libtool \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-zlib \
  mingw-w64-x86_64-libpng
```

Close it, and open the **MSYS2 MinGW64** terminal (start menu, under MSYS2).
Build protobuf once:

```bash
cd /path/to/rwe/libs && ./build-protobuf.sh
```

Then the project:

```bash
cd /path/to/rwe
cmake -S . -B build-release -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j2
```

Run `build-release/rwe_setup.exe` once to import your game data, then
`build-release/rwe.exe`. Run tests with `build-release/rwe_test.exe`.
Increase `-j2` if your machine has enough memory for more compiler jobs.

</details>

<details>
<summary><b>Windows — Visual Studio</b></summary>

<br>

Fetch the prebuilt MSVC library bundle (or double-click the script in
Explorer):

```
python fetch-msvc-libs.py
```

That leaves a `libs/_msvc` folder. Then open the folder in Visual Studio
(`File > Open > CMake...`, selecting `CMakeLists.txt`), pick an `x64` build
configuration, and build the `rwe.exe` target. CI builds this configuration
with Visual Studio 2026.

</details>

<details>
<summary><b>Windows — Visual Studio Code</b></summary>

<br>

Install the **C/C++ Extension Pack** from Microsoft, which brings CMake Tools
with it. `File > Open Folder` on the repository root.

At the bottom of the window you should see `CMake: [Debug]: Ready` and
probably `No Kit Selected` — click that to choose a compiler. If none are
listed, install Visual Studio or MSYS2 first. CMake then configures itself,
and `F7` builds.

</details>

<details>
<summary><b>Linux and macOS</b></summary>

<br>

**Devbox** is the quickest way to a working environment, using Nix to supply
the dependencies:

```bash
curl -fsSL https://get.jetify.com/devbox | bash   # one-time
devbox shell                                      # from the repo root
mkdir build && cd build
cmake .. -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
./rwe_test
./rwe
```

A Nix-provided environment can leave the engine unable to find your video
drivers. If launching gives `Could not get EGL display` or another OpenGL
error, point it at the system ones:

```bash
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH ./rwe
```

**Native**, on Ubuntu 24.04 (what CI uses):

```bash
sudo apt-get install -y \
  gcc-14 g++-14 cmake \
  libgl-dev libglx-dev libegl-dev libx11-dev \
  zlib1g-dev libpng-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev

cd libs && ./build-protobuf.sh && cd ..
mkdir build && cd build
export CC=gcc-14 CXX=g++-14        # or clang-18 / clang++-18
cmake .. -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

`devbox.json` lists the dependencies if you are on another distribution.

</details>

## The launcher

The optional multiplayer launcher is an Electron application built with
TypeScript, React and Redux. It talks to the engine through `rwe_bridge`.

<details>
<summary><strong>Build and run the launcher</strong></summary>

Use Node.js 22, as in CI. In the `launcher` directory:

```bash
npm ci
npm run server
```

In a second terminal, set `RWE_HOME` to the directory containing `rwe` and
`rwe_bridge`, then start the launcher:

```bash
export RWE_HOME=/path/to/engine/build
npm start
```

For local development, run its master server in a third terminal from the
same `launcher` directory:

```bash
npm run master-server
```

| Command | Purpose |
| --- | --- |
| `npm run tsc` | Type checking |
| `npm test` | Jest tests |
| `npm run lint` | ESLint |
| `npm run package` | Package the launcher |

</details>

## Documentation

| Start here | What it covers |
| --- | --- |
| [Status page](docs/index.html) | Landed work and remaining gaps |
| [Roadmap](docs/ROADMAP.md) | Development priorities |
| [Compatibility](docs/compatibility.md) | Preserved quirks and deliberate differences |
| [Original behaviour findings](docs/TOTALA-EXE.md) | Evidence and references behind the implementation |
| [Demo recordings](docs/TA-DEMOS.md) | TA demo formats and conformance fixtures |
| [Multiplayer connections](docs/NAT-PUNCH.md) | LAN and invite-code setup |
| [Profiling](docs/PROFILING.md) | Measuring simulation and rendering performance |
| [Architecture decisions](docs/adr/README.md) | Decisions that guide development |
| [Development guide](CLAUDE.md) | Builds, tests, conventions and repository workflow |

### Contribute or report a bug

Report problems in [GitHub Issues](https://github.com/CubeB/RWE-B4/issues).
Include the build revision, operating system, map, mods and steps to reproduce.
For an in-match problem, attach a save or replay when possible. For a crash or
performance problem, include the relevant log and describe when it happened.

Contributions target `revival` through pull requests. Read the
[development guide](CLAUDE.md) before changing code. Original-behaviour work
follows the documented [spec-room boundary](docs/adr/0004-a-spec-room-stands-between-the-decompilation-and-the-code.md):
reference research produces prose findings, and implementation is written
from those findings. Static analysis, RWE tests and executed original-game
checks are recorded as different evidence.

## Credits and license

Robot War Engine was created by [Michael Heasell](https://github.com/MHeasell)
and its contributors. This fork builds on their engine, parsers, COB virtual
machine and deterministic simulation. Visit the
[upstream repository](https://github.com/MHeasell/rwe) and its
[TAUniverse development thread](https://www.tauniverse.com/forum/showthread.php?t=45555).

The engine is distributed under the [GNU GPL v3](LICENSE). Total Annihilation
and its game assets belong to their respective rights holders and are not
included in this repository or its builds.
