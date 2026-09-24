# RWE: B4

> **Bot bot boom boom** — four B's, so B4 for short.

A real-time strategy engine that plays Total Annihilation's own data as it
ships. A fork of [Robot War Engine](https://github.com/MHeasell/rwe) by
[Michael Heasell](https://github.com/MHeasell), which is a lovely bit of work.

[![Build](https://github.com/CubeB/RWE-B4/actions/workflows/build.yml/badge.svg?branch=revival)](https://github.com/CubeB/RWE-B4/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/CubeB/RWE-B4?include_prereleases&label=download)](https://github.com/CubeB/RWE-B4/releases)
[![Upstream](https://img.shields.io/badge/upstream-MHeasell%2Frwe-lightgrey)](https://github.com/MHeasell/rwe)

**[Download](#download) · [Install](#install) · [Controls](#controls) ·
[Build](#build) · [Documentation](#documentation)**

---

## What it is

The engine reads the original game's files directly — HPI archives, 3DO
models, GAF sprites, TNT terrain, FBI and TDF definitions, and COB unit
scripts running in a virtual machine written for them. It runs a deterministic
lockstep simulation in fixed-point maths so peers stay in step across
platforms, and it builds its interface out of the game's own GUI files rather
than by eye.

**No game data ships here.** You supply your own copy of Total Annihilation.

## What is different in this fork

B4 works on the layer above: taking behaviours the engine already reproduces
and pinning them to what the original executable actually does.

Most of that work was **read out of `TotalA.exe` rather than guessed at**.
`docs/TOTALA-EXE.md` indexes over a hundred numbered findings across thirteen
subject documents — the flight model, fog of war and line of sight, the damage
pipeline, target selection, the economy, the nanolathe, the interface,
transports, the music system, and the renderer's own rasteriser — with the
probe scripts that produced them in `tools/exe/`.

That method earns its keep by being checkable, because several plausible
readings of that binary turn out to be wrong. Only replaying the arithmetic
against real unit data catches them: the radar rule had a cap that made the
altitude bonus dead code on every shipped unit, and the D-gun's trail turned
out to be `noexplode` letting a round detonate without being consumed, rather
than anything to do with `beamweapon`.

Beyond that:

| | |
|---|---|
| **Full save and load** | The whole simulation round-trips — units with their COB virtual machines mid-thought, projectiles mid-flight, spreading fires, the economy, the RNG — proven by a hash harness rather than by inspection. |
| **A skirmish AI** | Runs inside the deterministic tick and emits ordinary player commands, so it is part of the simulation rather than bolted beside it. |
| **The front end** | The movies (the GOG release ships them as `.ZRB`, which are Smacker files — RWE grew its own decoder), situational music on the original's own thresholds, the in-game menus, options and window modes. |
| **Performance** | Eight hundred units went from 8 fps to 36 by measuring the simulation rather than the renderer, then from 36 to the 60 Hz vsync cap by profiling the frame. Both passes are documented with the numbers, because in both the obvious suspect was the wrong one. |

## Download

Pre-release builds are on the [releases
page](https://github.com/CubeB/RWE-B4/releases). Every tag carries a Windows
installer, a Windows zip and a Linux AppImage.

They are pre-releases in the plain sense: cut from `revival` when a tag goes
up, rather than off a stable line. Take them as somewhere between the roadmap
and a finished game.

## Install

You need your own copy of Total Annihilation. Then:

```
rwe_setup
rwe
```

`rwe_setup` finds the installation, copies the archives, the films and the
soundtrack into the data directory the engine reads, and tells you what it
did. It looks in the GOG and Cavedog registry entries and the usual install
locations. If it cannot find yours, point it at the directory holding
`totala1.hpi`:

```
rwe_setup --from "C:/GOG Games/Total Annihilation"
```

The Linux AppImage is a single file with a single entry point, so there
`rwe_setup` is reached through it — the word `setup` first, then its own
arguments:

```
./Robot_War_Engine-*.AppImage setup --from "/path/to/Total Annihilation"
```

| Flag | What it does |
|---|---|
| `--dry-run` | Says what it would do and changes nothing. |
| `--link` | Hard-links instead of copying — instant, and saves about a gigabyte when the data and the installation share a volume. |
| `--to <path>` | Writes somewhere other than the default. |

Running it twice is safe: it copies only what is missing or half-written.
Start the game with `rwe`. If the data is missing it says so and points you
back here, rather than failing with a filesystem error.

<details>
<summary><b>What it does, if you would rather do it by hand</b></summary>

<br>

The engine reads one data directory — `%AppData%/RWE/Data` on Windows,
`$HOME/.rwe/Data` elsewhere — and mounts every `.hpi`, `.ufo`, `.ccx`, `.gpf`
and `.gp3` it finds directly inside it. Two subdirectories are read by name:
`movies` for the films, which the GOG release ships as `.ZRB` (Smacker) files
under its own `Data` folder, and `music` for the soundtrack. So:

- every archive from the installation's root, flat in the data directory;
- `<install>/Data/*.zrb` into `<data>/movies/`;
- `<install>/music/*.mp3` into `<data>/music/`.

The lookups are case-insensitive, so the mixed casing the GOG release ships
(`1.ZRB` beside `2.zrb`) needs no renaming.

```bash
mkdir -p "$HOME/.rwe/Data/movies" "$HOME/.rwe/Data/music"
cp /path/to/totala/*.hpi /path/to/totala/*.ufo /path/to/totala/*.ccx \
   /path/to/totala/*.gpf /path/to/totala/*.gp3 "$HOME/.rwe/Data"
cp /path/to/totala/Data/*.[zZ][rR][bB] "$HOME/.rwe/Data/movies"
cp /path/to/totala/music/*.mp3 "$HOME/.rwe/Data/music"
```

`rwe --data-path <dir>` overrides the location at runtime.

</details>

## Controls

**Camera and game**

| Key | Action |
|---|---|
| Arrow keys | Scroll the map |
| `+` / `-` (or keypad) | Game speed |
| `Pause` | Pause |
| `Tab` or `F2` | Game menu |
| `Esc` | Close the menu, else cancel the cursor mode, else deselect |
| `T` | Track the selection |
| `` ` `` | Health bars |

**Units**

| Key | Action |
|---|---|
| Left click | Select. Right click to move. |
| `Ctrl`+`A` | Select everything on screen |
| `Ctrl`+`S` | Stop |
| `Ctrl`+`F` | Attack mode |
| `Ctrl`+`Z` | Attack-ground mode |
| `Ctrl`+`W` | Guard mode |
| `Ctrl`+`P` | Move mode |
| `Ctrl`+`D` | Self-destruct |
| `Ctrl`+`C` | Select and track the commander |
| `Ctrl`+digit | Bind a control group |
| digit | Recall a group (`0` is the tenth) |
| `Shift`+digit | Add the selection to a group |

**Debug**

| Key | Action |
|---|---|
| `F1` | Help overlay |
| `F10` | In-game debug window — spawning units and the like, in a loaded game |
| `F11` | Global debug window, in or out of a game |

`docs/TOTALA-EXE-KEYBOARD.md` has what the original binds and where RWE still
differs.

## Build

```bash
git clone https://github.com/CubeB/RWE-B4.git
cd RWE-B4
git submodule update --init --recursive
```

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
  mingw-w64-x86_64-glew \
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
mkdir build && cd build
cmake .. -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

Run it from the top level: `build/rwe.exe`. Tests: `build/rwe_test`.

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
  libglew-dev zlib1g-dev libpng-dev \
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

The multiplayer lobby is a separate Electron application in TypeScript, with
React and Redux. It talks to the engine over JSON IPC through `rwe_bridge`.

It is separate on purpose. The lobby is not performance-critical, and the
long-term goal is to manage installed mods and make sure every player in a
game has the same ones enabled before the engine launches. That cannot live
inside RWE, because RWE is driven by the supplied game data — which dictates
what screens exist at all. An interface that manages mods has to work when no
particular mod is loaded, and original TA data is itself just one more mod
that may or may not be present.

<details>
<summary><b>Building and running it</b></summary>

<br>

```bash
cd launcher
npm ci
npm run server      # webpack dev server, serves the app with hot reload
```

In a second terminal, point `RWE_HOME` at a directory holding `rwe` and
`rwe_bridge`, then start the app:

```bash
export RWE_HOME=/my/installed/rwe/dir
npm start
```

The launcher expects a master server managing the list of public games, and in
development it looks for one on the local machine. In a third terminal:

```bash
npm run master-server
```

Running launchers connect to it automatically.

| Command | |
|---|---|
| `npm run tsc` | Type check |
| `npm test` | Jest tests |
| `npm run lint` | ESLint |
| `npm run package` | Bundle for release, into e.g. `rwe-launcher-win32-x64` |

</details>

## Documentation

| | |
|---|---|
| [`docs/index.html`](docs/index.html) | Status page — what has landed, and what has not |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | The plan |
| [`docs/TOTALA-EXE.md`](docs/TOTALA-EXE.md) | Index to the findings read out of the original binary |
| [`docs/compatibility.md`](docs/compatibility.md) | Where this engine and the original part company, and where they deliberately do not |
| [`docs/TA-DEMOS.md`](docs/TA-DEMOS.md) | The `.tad`/`.ted` demo format, and the conformance corpus built from it |
| [`docs/PROFILING.md`](docs/PROFILING.md) | The per-phase tick and frame timers, and how to read them |
| `CLAUDE.md` | Working guide to the codebase and its hazards |

## Credit

Robot War Engine is by [Michael Heasell](https://github.com/MHeasell) and its
contributors. The engine, the format parsers, the COB virtual machine and the
deterministic simulation all come from there, and it is worth a look:
<https://github.com/MHeasell/rwe>

Upstream posts progress to a [thread on the TAUniverse
forums](http://www.tauniverse.com/forum/showthread.php?t=45555).

Total Annihilation is Cavedog Entertainment's. This fork ships none of it.
