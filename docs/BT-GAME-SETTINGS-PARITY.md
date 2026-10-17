# Persistent game options, defaults and mission unit restrictions parity audit

Spec-room finding, 2026-10-17. Like `BT-GAME-PLAYERS-PARITY.md`,
`BT-INGAME-CONTROL-PANEL-PARITY.md` and `TA-SYSTEMS-PARITY.md`, it allocates no section
number in the main register. This is the finding that implementation work cites for
what the original remembers between sessions (the registry values it loads at start and
saves from its menus), the default of each, the immediate side effects of loading, and
the one routine in the same file that is not a preference at all: the mission's
`useonlyunits` restriction. Per `AGENTS.md` and ADR-0004 a change to RWE's options,
`rwe.cfg` keys or the mission unit list cites this audit.

The saved-game container, `settings_campaign.cpp`, is a different chapter and is not
audited here. The registry footprint of the music options is `TOTALA-EXE-MUSIC.md` section
43 and is not restated; the display-options bits are `TOTALA-EXE-KEYBOARD.md` section 76 and
`TOTALA-EXE-RENDER.md` near 1594; the Options screen pages are `TOTALA-EXE-INTERFACE.md`
near 530 and 592. What follows adds the **keys those sections do not list, every default,
and the loading rules**.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-settings.md`; the
  campaign sibling was skimmed only to know its boundary). Nothing else under that tree was
  opened: no source, no decompilation, and none of its source links were followed. Addresses
  are the ones the chapter names; rules are restated in prose. The spec-room discrepancy
  register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; its
  rows BT-32 (unit cap semantics) and BT-30 (language directories) are neighbours and not
  re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. The
  defaults are "this source baseline", and the chapter itself says they are not universal
  for a modified install. Weaker than `TOTALA-EXE*.md`. Disagreements are marked
  **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no registry was read.
- **The RWE half** is a read-only audit of `src/rwe/GlobalConfig.*`, `src/main.cpp`'s
  argument handling, `src/rwe/MainMenuModel.h`, the options pages in
  `game/GameScene_menu.cpp` and `MainMenuScene.cpp`, and the mission list code in
  `game/GameSimulationLoader.cpp` at the working tree of 2026-10-17. Nothing was built, run
  or changed. Another agent had uncommitted edits in the tree, so every line number is
  "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Storage, lifecycle and what loading writes
3. Display, interface and rendering settings
4. Audio settings and the unit-speech thresholds
5. Names, password and output fields
6. Other scalar preferences
7. Mission and skirmish rule defaults, and per-player defaults
8. SaveSettings coverage and its asymmetries
9. Track bytes and the generic key adapters
10. Mission use-only unit restrictions
11. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| 1 storage, lifecycle | `TOTALA-EXE-INTERFACE.md` near 592 (OK saves, Cancel restores), near 772 | agrees; rule 2 |
| 2 display bits | `TOTALA-EXE-KEYBOARD.md` section 76, `-RENDER.md` near 1594 | bits agree; defaults here |
| 3 audio | `TOTALA-EXE-MUSIC.md` section 43 (music keys) | music keys agree; the rest here |
| 4, 5, 6 | none | rules 5, 6 |
| 6, 7 skirmish rules, players | `BT-GAME-PLAYERS-PARITY.md` (commander death) | rule 7 |
| 9 track bytes | `TOTALA-EXE-MUSIC.md` section 43 ("dead", do not implement) | **MATCHES** the register's reading (rule 9) |
| 10 use-only | `TOTALA-EXE-DATA.md` near 1046 (ported, #381) | agrees with two differences (rule 10) |

## 2. Storage, lifecycle and what loading writes

**Reference (section 1).** Settings live under the current user's Software, Cavedog
Entertainment, Total Annihilation key; per-player skirmish values under its Skirmish child.
A registry read that returns "more data" is treated as usable; **no type or length is
validated**. The load needs the game, sound object, options allocation and mission interface
already built, **writes a missing default back as it goes** (so the loader is not read-only),
and immediately applies some audio options to the sound object. The save is called from the
menu, **from console commands and from front-end transitions**; it writes key by key through
void wrappers, with no transaction, no rollback and no failure report.

**Register.** `TOTALA-EXE-MUSIC.md` section 43 gives the HKCU path and the music keys;
`TOTALA-EXE-INTERFACE.md` near 592 records OK as "writes every setting to the registry" and
Cancel as restoring the entry snapshot without touching it.

**RWE: DIVERGENCE (documented, representation).** `INTERFACE.md` near 772 records that RWE's
config file stands in for the registry. `rwe.cfg` is read once at start (`main.cpp`, about
187-215, through the command-line reader: every key is also a switch) and written by
`writeGameOptions` (`GlobalConfig.cpp`, about 262-281) from the OK of either options screen,
leaving every other key in the file alone (`updateConfigFile`).

- **Bounds: DIFFERS, better.** The original stores whatever it reads. RWE clamps every
  numeric key on the way in (scroll speed to 25 to 200, gamma 50 to 133, volumes at most
  100, modes at most their top, camera zoom, UI scale and drop timeout). A hand-edited or
  corrupt value cannot reach the game. Not a gap.
- **Writing a missing default back at load: not applicable.** RWE never writes a default
  into the file unprompted.
- **Save points: DIFFERS.** The original also saves from console commands and state
  transitions; RWE saves only from the options screens' OK. A setting the original changes
  by console (the chapter does not list which) is never written by RWE. Low impact.
- **Settings stored by keys RWE does not have: MISSING** (rules 3 to 7). The largest groups
  are the skirmish setup and the interface-style switch.

## 3. Display, interface and rendering settings

**Reference (section 2).** Defaults when a key is missing (written back unless noted); a
value stored in a one-bit field keeps only its low bit:

| Key | Default | Rule |
| --- | ---: | --- |
| Interface Type | 0 | values above one become one; negative kept |
| DisplaymodeWidth, Height | 640, 480 | stored without any supported-mode check |
| side | 0 | integer |
| Difficulty | 1 | low 16 bits |
| scrollspeed | 32 | truncated to a byte |
| screenchat | 1 | integer |
| damagebars | 0 | one bit |
| Anti-Alias | 1 | one bit |
| Shadows, FeatureShadows, VehicleShadows | 1 | one bit each |
| Shading | 1 | one bit |
| DitheredFog | 0 | one bit |
| Gamma | 12 | a stored **ten becomes twelve**; other values kept |
| SwitchAlt | 0 | one bit |

DisplaymodeDepth is read only for an auxiliary test (256 permits a Games value of one to set a
UI bit); the loader then sets two UI bits and clears a third unconditionally. AllMissions
defaults to zero and is **not** written back.

**RWE.**

- **Interface Type: MATCHES the polarity, MISSING from the screen.** `leftClickInterfaceMode`
  defaults true, the original's default 0 ("Left Click" is stage 0 of the `LEFTCLICK`
  gadget, `INTERFACE.md` near 1679). It is read from `rwe.cfg` (`interface-mode`,
  `main.cpp` about 188) and toggled by a debug checkbox (`SceneManager.cpp` about 445), but the
  options pages **disable** the `LEFTCLICK` gadget (`GameScene_menu.cpp` about 743) and
  `writeGameOptions` does not write it, so a player cannot change or keep it from the real
  screens. A value above one is read as the right-click mode only for the exact word
  `right-click`, which is the same two-state result.
- **Display mode, width, height, depth: DIVERGENCE (documented).** RWE has a window mode
  (`window-mode`), a pixel size, a UI scale and a camera zoom instead of a mode list
  (`GlobalConfig.h`); `TOTALA-EXE-INTERFACE.md` near 530 records the original has no in-game
  resolution change at all.
- **Anti-Alias: MATCHES** (default on; `anti-alias`). **Shading: MATCHES with a documented
  widening**: the original's On is RWE's "buildings only", the default (`GlobalConfig.h`,
  `TOTALA-EXE.md` section 88). **Shadows: MATCHES** (`shadows`, default on) and
  **vehicle shadows: MATCHES** (`vehicle-shadows`, an `rwe.cfg` key only, by design).
- **FeatureShadows: MISSING.** The original keeps a third bit (RENDER.md near 1594); RWE has
  two. Whether feature shadows are drawn or not is not separately switchable. Presentation.
- **DitheredFog, damage bars, screen chat: not found.** No `rwe.cfg` key, no options gadget
  and no global flag for dithered fog or for the persisted damage-bar switch exist in the
  code read, and the screen-chat and damage-bar toggles' persistence was not traced.
  Presentation only.
- **SwitchAlt: MISSING.** `KEYBOARD.md` section 75 records it, and near 386 that RWE's digit
  keys are control-group recall rather than the original's build-page keys. A separate
  decision that section already holds.
- **Scroll speed: DIVERGENCE (representation), UNCLEAR on the default.** The original keeps a
  byte, default 32, on a slider whose range is 65 (`INTERFACE.md` near 546), the middle of the
  track. RWE keeps a percentage from 25 to 200, default 100, which sits at 0.43 of its track.
  The original's value-to-speed mapping is not in the chapter or the register, so whether
  100 percent is the original's default speed, or only "the old fixed rate" (`GlobalConfig.h`),
  is not established. A player who sets the slider to its middle gets a different speed in
  the two games.
- **Gamma: UNCLEAR.** The original stores 12 by default and promotes a stored 10 to 12.
  RWE stores a percentage, default 100, range 50 to 133, and the register records the
  original's range as the same 50 to 133 (`GlobalConfig.h`). Whether 12 is 100 percent
  is not stated; a saved 10 (the old default of an earlier build, plausibly) behaving as
  12 cannot arise in RWE.
- **Difficulty: MATCHES the default (1, "Medium") but is not persisted.** The skirmish model
  starts at 1 every launch (`MainMenuModel.h` about 122).

## 4. Audio settings and the unit-speech thresholds

**Reference (section 3).**

| Key | Missing | Load |
| --- | --- | --- |
| Sound Mode | write current "3D enabled" plus one; mode one | 3D only for exactly two; low three bits kept |
| MixingBuffers | maximum eight | passed straight to the mixer, no clamp |
| RestoreVolume | cleared | low bit |
| WaveOutVolume, CDAudioVolume | unchanged | applied only if RestoreVolume is set |
| unitchat | byte ten | truncated to a byte |
| unitchattext | byte five | truncated to a byte |
| musicmode (on/off bit), cdmode (mode byte) | on, four | low bit; byte |
| ackfx, buildfx, speechfx | on | low bit |
| fxvol, musicvol | 27, 32 | stored |

Unit speech uses the two chat values as **strict priority thresholds** (an utterance is
spoken when its priority exceeds ten minus `unitchat`, and written as text when it exceeds
ten minus `unitchattext`) and `speechfx` as an audio gate (`BT-SOUND-PARITY.md` rule 6).

**Register.** The music keys (`musicmode`, `cdmode`, `musicvol`) and their defaults (on, 4,
32) agree with `MUSIC.md` section 43. The Options page's gadget list (`SOUNDSRT`: `MODE`
Off/Mono/3D, `FXVOL` slider of range 64, `SPEECH` Off/Medium/Full; `SPEEDSRT`: `UNITCHAT`
Off/Medium/Full) is `INTERFACE.md` near 539; **which stored value each stage writes is not
recorded.**

**RWE.**

- **Sound Mode: DIFFERS.** The three stages exist (`SoundMode`), and Off silences effects,
  matching the original's gate (an effect needs a nonzero low-three-bit mode). Mono and 3D
  are the **same** in RWE: only Off changes anything (`GameScene_menu.cpp` about 809, 1155;
  `GameLaunch.cpp` about 324), so the 3D camera-relative placement of
  `BT-SOUND-PARITY.md` rule 4 has nothing to switch.
- **fxvol and musicvol defaults: DIFFERS.** The original starts effects at 27 on a range of 64
  (about 42 percent, if the stored value is the slider position) and music at 32; RWE starts
  both at 100 percent (`GlobalConfig.h`). The scale of `musicvol` is not given anywhere. A first
  launch is louder in RWE than in TA by roughly the ratio of those two figures. Not a
  simulation matter.
- **unitchat 10 and unitchattext 5: DIFFERS / UNCLEAR.** RWE has one three-stage setting,
  `unit-speech` (0, 1, 2), default Full, which gates both the voices and (inconsistently, see
  `BT-SOUND-PARITY.md` rule 6) some of them. The text threshold has no counterpart: the
  `UNITCHAT` gadget and `TXTSCROL`, `MAXLINES` are **disabled** (`GameScene_menu.cpp`
  about 743). If the stage values are 0, 5 and 10 (the defaults suggest it: ten for full voice,
  five for the text setting), then the original's **Medium** passes priority above five. The
  consequence is worked out in `BT-SOUND-PARITY.md` rule 6, and is large.
- **speechfx, ackfx, buildfx: MISSING.** No separate switches; the three kinds' gating is the
  one `unit-speech` stage.
- **MixingBuffers, RestoreVolume, WaveOutVolume, CDAudioVolume: not applicable.** They
  configure the original's DirectSound mixer and the operating system's volume; RWE's mixer
  has its own track pool (`AudioService`, `allocateTracks(256)`, `GameLaunch.cpp` about 325)
  and does not touch system volumes.

## 5. Names, password and output fields

**Reference (section 4).** Password has an 11-byte capacity, nickname 17, game name 17, each
empty on a failed read; a nonempty online-configuration lobby name or a nonempty command-line
host-game name overrides the registry values (the former capped at 16 characters). Full-length
reads are not terminated. The password is stored as plain text; no encryption, no
authentication. The image output directory (256 bytes) falls back to the Windows user name,
else `user_images`, joined to a display-context directory field. Movie output rate defaults to
ten and the directory and rate are written only when their dirty flag is set, the flag
cleared whether or not the write worked.

**RWE.** Names come from the launcher and the lobby (`GameParameters`, the bridge), not from
a stored preference; no password exists in the engine. **Not applicable** for the password. **MISSING (minor)**
for a remembered nickname and game name, which a player retypes each session; the launcher
keeps its own. The movie-recording rate and directory have no counterpart (RWE records demos,
`--record-demo`, not frames).

## 6. Other scalar preferences

**Reference (section 5).** `textlines`, `textscroll` and `mousespeed` default to ten with no
write-back. `gamespeed` defaults to ten (or a loaded 16-bit value), and the effective game
speed is then copied from the speed control. `clock` defaults to a cleared UI bit.
`NumSkirmishPlayers` defaults to four; the "greater than one and at most ten" test
**assigns the original value on both branches, so it clamps nothing**, and later loops index a
ten-record array with it. `PlayMovie` defaults to one and is interpreted by the front end. The
track bytes, `AllMissions` and the player count have their own save helpers.

**RWE.**

- **Game speed: DIVERGENCE.** The original remembers the speed; RWE's is a command in the
  game stream (`PlayerSetGameSpeedCommand`) that starts from the default each game and is
  not written to `rwe.cfg`. The two games start at the same default (ten, normal); a player
  who set a different speed in TA finds it kept next time and not in RWE.
- **Text lines, text delay, mouse speed, clock: MISSING.** `MAXLINES` and `TXTSCROL` are the
  disabled gadgets above; RWE's console keeps eight lines for five seconds
  (`GameScene_audio.cpp`, `printConsole`) as constants. The mouse speed has no counterpart (the
  operating system's pointer speed rules). The clock option is not found in RWE.
- **NumSkirmishPlayers: not applicable.** RWE's skirmish model has ten slots and no player
  count (`MainMenuModel.h` about 127). The original's missing bounds check (an oversized stored
  count indexes past the array) is a defect RWE does not copy.
- **PlayMovie: DIFFERS.** `TOTALA-EXE-DATA.md` near 1690 records that the intro `2.zrb` plays on
  **first launch**, governed by this key; RWE plays the startup logo `1.zrb` on every launch and
  the intro only from the button (`GameLaunch.cpp` about 429). A first-launch intro is
  MISSING; it needs a persisted flag, which RWE has no place for yet. Presentation.
- **AllMissions: UNCLEAR.** The chapter does not say what reads it (the campaign module
  does). RWE has no such flag; `TOTALA-EXE-DATA.md` near 1668 records that in the original
  any listed mission can be started, so RWE's campaign screen already behaves as "all
  missions" and the flag has nothing to unlock.

## 7. Mission and skirmish rule defaults, and per-player defaults

**Reference (sections 6, 7).** The three groups of four rule keys (single, multiplayer and
skirmish: commander death, mapping, line of sight, line-of-sight type) default to **one** and
are written back. `SkirmishDifficulty` defaults to one (low 16 bits kept) and
`SkirmishLocation` to one (it feeds the options' fixed-locations field). `SkirmishMap` is up to
256 bytes; **when missing, the loader switches the mission type to two, refreshes the map list,
copies the current mission name, restores the type to zero and writes the name back**, an
arbitrary map rather than a built-in choice. For each player index below `numSkirmishPlayers`
the keys `Player%dController`, `Side`, `Color`, `AllyGroup`, `Metal` and `Energy` are read, with
missing defaults **0, i modulo two, i, 5, 1000, 1000**, assigned but not written back.

**RWE.**

- **Per-player defaults: MATCHES.** The model starts player `i` with side alternating ARM then
  CORE, colour index `i`, 1000 metal and 1000 energy (`MainMenuModel.h` about 127-138). The ally
  group default of five means "no team" in the original; RWE's team is an optional unset
  (`std::nullopt`), the same meaning. The model differs in that slot 0 is a Human and slot 1 a
  Computer with the rest Open, where the original's default controller is zero for all four of
  its default players; a fresh install's first skirmish is one human against one computer in
  RWE and presumably a different mix in TA. Presentation of a first launch.
- **Rule defaults: UNCLEAR.** The original defaults every rule key to the stored value one. For
  commander death the register reads stored one as "game ends" (`BT-GAME-PLAYERS-PARITY.md`
  rule 7: 0 continue, 1 game ends, 2 respawn), and RWE's default stage 0 is "Game ends"
  (`MainMenuModel.h` about 114): **the same default**. For the others the stored-value-to-gadget
  mapping is not recorded: RWE defaults mapping to stage 0 ("Unmapped", `MainMenuModel.h` about 118),
  start location to stage 0 ("Fixed"), line of sight to stage 1 ("True"), difficulty to stage 1
  ("Medium"). Whether the original's stored one means "Mapped" and "Random" (stage one of those
  buttons) or the opposite cannot be told from these sources. If it is stage one, a fresh
  TA skirmish starts Mapped, Random and RWE's starts Unmapped, Fixed. The key name
  `fixedLocations` for a field defaulting to one points the other way. Settled by one look at
  a fresh TA skirmish screen (an original check).
- **Persistence of all of the above: MISSING.** RWE builds a fresh `MainMenuModel` each
  session: nothing about the last skirmish (map, players, sides, colours, resources, rules,
  difficulty) is remembered. In TA the whole screen comes back as it was left.
- **`SkirmishMap` fallback: not applicable.** RWE has no stored map to be missing.
- **The unbounded player-count read: not applicable** (see rule 6).

## 8. SaveSettings coverage and its asymmetries

**Reference (section 8).** The save writes the display, interface, side, scroll, chat and visual
fields, all rule fields, difficulty, gamma, the input switch, password, nickname, game name,
the sound options and volumes (querying the **live** mixer levels when RestoreVolume is on),
text, mouse, speed, clock, the skirmish rules, location, map and each configured player's six
values, and `PlayMovie`. It writes the fixed-locations field **under two names**
(`FixedLocations` and `SkirmishLocation`) while the load reads only `SkirmishLocation`. It does
not save the display depth test, the UI forced bits, the effective speed, `AllMissions` or
`NumSkirmishPlayers` here. The writers discard failures.

**RWE: DIVERGENCE (by design).** Seventeen keys written (rule 2). The originals' quirks
(`FixedLocations` written but never read, the dirty-flag clearing on a failed write) are
defects RWE does not reproduce and need not.

## 9. Track bytes and the generic key adapters

**Reference (section 9).** `SaveTrackSettings` writes exactly ten supplied bytes as values
`track0` to `track9` with no bounds argument; two thin wrappers read and write raw binary
values under the Total Annihilation key. They are not saved-match objects.

**Register.** `TOTALA-EXE-MUSIC.md` section 43: a routine at `0x42F910` that writes ten values
named `track0` to `track9` has **no call site anywhere in the binary**; it is the leftover of an
older scheme superseded by the `CDLISTS` binary value, and "do not implement it".

**RWE: MATCHES the register.** RWE keeps its per-track types as one comma-separated string
(`music-track-types`) beside the album, and does not write ten fixed values. The chapter says
only who supplies the pointer and does not claim a caller, so it neither confirms nor
contradicts the register's "no call site".

## 10. Mission use-only unit restrictions

**Reference (section 10).** `0x431740` takes the mission's resource slot six name. A null
name, or a file that does not load, **returns before touching anything**. Otherwise it makes
the unit-definition array writable and **clears the availability bit on definitions one
through the last (index zero excluded)**, then for each top-level record name in the file
scans the definitions from index zero case-insensitively and sets the bit on the **first**
unit with that `unitname`, stopping. Unknown names do nothing; the file's record contents are
ignored; duplicates add nothing; an empty list clears every definition but the first. It does
not intersect with any earlier mask, does not destroy existing units and does not rewrite
factory queues.

**Register.** `TOTALA-EXE-DATA.md` near 1046: the bit is `def+0x241` bit 23, set on every unit by
the parser, cleared on every definition and set again on each listed one by `0x431740` (called at
`0x4974DE`), after which `0x42D2E0` **compacts the array, dropping every definition whose bit is
clear**, so an unlisted unit does not exist: nobody builds it, and a mission `[unitN]` naming
one makes nothing. Every shipped mission names a list (14 to 248 units); two name a list that
does not exist (Emerald Seamount, Battle of Chimo Island) and are unrestricted. **Ported (#381).**

**RWE.** `applyMissionUnitList` and `missionUnitListFromTdf` (`GameSimulationLoader.cpp`,
about 901-930 and 1369-1404).

- **Name matching, duplicates, unknown names, contents ignored: MATCHES.** The list is the set
  of top-level block names, upper-cased; an unknown name is ignored; the block contents are not
  read. A name repeated in the list changes nothing (`std::set`), and the retail lists that
  repeat a name (`raid on vetes`, `ice base assault`) repeat only empty blocks
  (`BT-UTIL-TDF-PARITY.md` rule 5).
- **A list that does not open restricts nothing: MATCHES**, including the two retail missions
  that name a missing list. RWE adds one case: a list that opens but fails to parse also
  restricts nothing (the original would end the process on a syntax error).
- **Applied only to missions: MATCHES in effect.** RWE tests `gameParameters.mission`; the
  original's slot six is set only by a mission file.
- **Index zero is exempt in the original: UNCLEAR, possibly DIFFERS.** The chapter's clearing
  loop starts at index one, so whichever definition is stored first always survives and is
  never removed by `0x42D2E0` either. The register's wording ("clears that bit on every
  definition", `0x4317AA` to `0x4317D8`) does not say where the loop starts. RWE excludes
  every unlisted unit, including whichever the original keeps at zero. Which unit is definition
  zero is decided by the load order of the definition array, which was not traced, and
  whether any shipped mission list omits it is the question. A **spec-room read of the loop's
  start** and the load order settle it; the effect is at most one unit type per mission.
- **Intersection with an earlier mask: MATCHES.** The original has none (clear, then set);
  RWE recomputes `excludedByMission` for every definition each time.
- **How "not available" is implemented: DIVERGENCE (documented).** The original removes the
  definitions from the array; RWE keeps them and marks `excludedByMission`, removing their
  buttons from every build menu, leaving them out of the AI's build tree, refusing a build or
  queue naming one on every peer, and skipping a `[unitN]` record that names one
  (`DATA.md` near 1058). Existing units of an excluded type are not touched in either.
- **Unit-cap semantics** are the register's BT-32 and not touched here.

---

## 11. Gaps to fix

Ranked by how much a player or a result would notice; none touches the lockstep simulation
except the unit list.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Skirmish setup is not remembered** (map, players, sides, colours, resources, rules, difficulty), and the rule defaults may differ from TA's (Mapped/Random against Unmapped/Fixed). | MISSING, UNCLEAR defaults | an original check of a fresh TA skirmish screen; then a small persisted model in `rwe.cfg` |
| 2 | **Interface style (left or right click) cannot be changed or kept** from the real options screens; the gadget is disabled and `writeGameOptions` omits it. The mode exists and works from the command line. | MISSING | wire the gadget and add `interface-mode` to the written keys |
| 3 | **Unit-speech stages and the original's thresholds** (see `BT-SOUND-PARITY.md` rule 6): RWE's Medium is not the original's, and its Off does not silence selection and acknowledgement. | DIFFERS, UNCLEAR | an original check of what Medium does; spec-room read of the stage-to-value write |
| 4 | **First-launch intro and a persisted "play movie" flag.** | MISSING, presentation | add the flag to `rwe.cfg` |
| 5 | **Game speed is not remembered** between games. | DIVERGENCE | decide; speed is a command in the stream, so persisting it is a launcher or `rwe.cfg` default for the next game's initial speed |
| 6 | **Mono and 3D sound are the same**; effect volume and music volume start at 100 percent where TA starts at 27 and 32. | DIFFERS, presentation | `BT-SOUND-PARITY.md` rule 4 |
| 7 | **Scroll-speed scale and default**, gamma default (12) mapping. | UNCLEAR | an original check of the scroll rate at the slider's middle |
| 8 | **Index zero exempt from the use-only clearing loop.** | UNCLEAR | spec-room read of the loop start and the load order |
| 9 | Text lines, text delay and unit text acknowledgements (`MAXLINES`, `TXTSCROL`, `UNITCHAT`) are disabled gadgets; a third shadow bit, dithered fog, the damage-bar persistence, `SwitchAlt` and the mouse speed have no counterpart. | MISSING, presentation | one by one when wanted |
| 10 | The original's own defects (unvalidated registry types and lengths, the unbounded skirmish player count, failed writes lost silently, a stored gamma of ten promoted to twelve). | not applicable | none; RWE clamps instead |

No source file was changed.
