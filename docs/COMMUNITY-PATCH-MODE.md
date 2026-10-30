# The Community Patch mode (issue #934)

RWE can run a curated subset of the **TA Community Patch** rules, which is the
`tdraw.dll` work maintained at <https://github.com/tanvanman/TADR> (release
2026.10.6 read on 2026-10-28; its own feature list is
`src/DDraw/tdraw.txt`). The mode is opt-in, is not a claim of Total Annihilation
parity, and is deliberately smaller than TADR: only features that RWE can honour
with a real effect are included.

**Status**: first slice implemented (`--community-patch`: the TADR weapon
flags). This document is the map for the rest.

## What the mode is

- A **ruleset flag** carried in `GameParameters` and copied to
  `GameSimulation::communityPatchMode`. It changes targeting and firing, so it
  is part of the game's rules, not a local preference.
- **Off by default.** A normal game, save, replay or arena run is unchanged.
- **RWE-original.** TADR is a community patch with no byte-tactics comparison:
  there is no "original" behaviour to match, only TADR's written rules. Every
  adopted feature is therefore a recorded RWE choice, and any place RWE picks a
  different rule is a deliberate divergence, not a parity bug.
- **One ruleset, not per-feature toggles.** The point of the mode is that a
  mod or a group can say "play with the community rules" and know what that
  means. Individual UI preferences (snap radii, chat, counters) stay local
  preferences rather than joining the simulated ruleset.

## Slice 1 (implemented)

`--community-patch` sets `GameParameters::communityPatch`, the loader copies it
into the simulation, and the weapon TDF keys below are honoured:

| TADR key | Effect in RWE |
| --- | --- |
| `nottoair` | never target or fire at a flying unit |
| `nottounderwater` | never target or fire at a fully submerged unit |
| `notoverwater` | the shot is held while the firer is over water |
| `notoverland` | the shot is held while the firer is over land |
| `surfacefire` | a `waterweapon` may also engage surface (non-flying) targets |

The position flags *hold* the shot: the target and reload are kept, no
ammunition or per-shot resource is spent, and it fires as soon as the unit is
over the right ground. TADR also stops the turret aiming while held; RWE does
not model that half yet.

Limits of this slice, all deliberate:

- **Network and recorded games.** The flag travels in the lobby's setup
  options (`community=1`), so the host's `/community` reaches every guest with
  the rest of the rules and the launch uses it. Replays carry it (header
  version 8; version 7 loads as mode off). A TA demo is the original's format
  and cannot carry it, so recording one is refused. A direct command-line
  network launch must pass `--community-patch` on every peer; the lobby is the
  supported path because it agrees the rule before launch.
- **Saves carry it** (`SaveFile` writes and reads `communityPatch`).
- **Not hashed.** No simulation hash field was added, because that would change
  every existing hash; the mode is a launch-parameter invariant, agreed by the
  lobby or set identically on the command line.

## Off-map aircraft (implemented)

TADR 2026.10.6 says "off-map aircraft targetable, in tiles past the map edge",
with a per-build value of 1 tile for ProTA/OTA and 32 for Escalation/Mayhem.
RWE has one ruleset rather than per-mod builds, so it uses the conservative
mainline value of **one tile**. The reference is TADR's written feature list,
not a byte-tactics comparison: this is RWE-original ruleset work under
ADR-0006.

A flying unit whose origin is wholly outside the map by at most that margin
is treated as visible from the nearest edge vision cell when the ordinary
visibility test runs. The target scan, weapon range, `nottoair`, cloak, sonar
and submerged rules still apply, and ground units are unaffected. The change
only alters derived visibility; it adds no saved or hashed state.

## Percentage share thresholds (implemented)

TADR 2026.10.6 adds a `%` suffix to `+setsharemetal` and `+setshareenergy`:
the threshold is that percentage of current storage and keeps tracking it as
storage grows. A plain number remains the absolute threshold it always was.
This is RWE-original ruleset work under ADR-0006, not a byte-tactics
comparison.

RWE keeps the flag and the 0..100 value beside each player's absolute reserve.
It hashes, saves and dumps them because they steer the every-two-seconds
sharing pass; the value is sent with the ordinary share command (a new
backwards-compatible protobuf field), and older saves load with the absolute
form. Percentages outside 0..100 are clamped.

## Transported explosion overrides (implemented)

TADR 2026.10.6 adds optional `TransportedExplodeAs` and
`TransportedSelfDestructAs` unit FBI keys. A unit that dies while loaded in a
transport uses the named weapon instead of `ExplodeAs` or `SelfDestructAs`;
the two keys are independent, and an omitted one keeps the ordinary key. A
unit outside a transport is unchanged. This is RWE-original ruleset work
under ADR-0006, not a byte-tactics comparison.

The simulation selects the weapon through one helper and the scene's death
blast uses the same selection, so the visual and the damage agree. Keys are
parsed whatever the mode and only honoured under it. The loader resolves
missing weapon names the same way it already does for the ordinary keys.
## Patrol reclaim thresholds (implemented)

TADR 2026.10.6 gives each player `+setreclaimmetal` and
`+setreclaimenergy`, as `<0-100>[%]`. A patrolling construction unit looks for
reclaim only while metal is below the metal level or energy is below the
energy level; it assists only while energy is at or above the energy level.
The default is 20% for both. `0` never counts as low (so a patrol does not
reclaim and always assists), and `100` counts as low unless the store is
exactly full. The levels belong to the player who types the line and track
storage as it grows; with no argument the command prints the current level.

RWE stores both levels per player, hashes, saves and dumps them, and carries a
change in the issuer's command stream so every peer applies the same level to
the same player. They are inert outside the Community Patch and in playback.
Air constructors use the same gate as ground ones under the mode; outside it,
the original's fixed fifth and the air constructor's unconditional reclaim
scan are unchanged. This is RWE-original ruleset work under ADR-0006, not a
byte-tactics comparison.
## Silent map weapons (implemented)

TADR 2026.10.6 adds `nomapweaponalert` for zero-damage map weapons. A
projectile with no firing unit from a weapon carrying the flag, whose damage
entries are all zero, no longer raises the under-attack message or voice. The
damage event itself is still emitted and scored; only the alert is dropped.
Weapons fired by a unit, and weapons with any positive damage, keep the
ordinary alert. RWE has no minimap or megamap projectile alert dots, so there
is no second surface to suppress. This is RWE-original ruleset work under
ADR-0006.
## Map units in a skirmish OTA (implemented)

TADR 2024.10.27's "neutral unit and/or per-player additional unit spawn" adds a
`[units]` block under a skirmish map's `[Schema 0]`, holding repeated
`[unitN]` records. RWE reads the block through the same mission-unit reader it
already had, but under the mode a skirmish map's records are spawned by
`spawnMapUnits` after the start-position deal; a campaign mission still uses
`spawnMissionUnits` and is unchanged.

- **`Player` is a map position.** 1..10 names the map's `StartPos<n>`; the
  owner is whichever seat the deal gave that position, so Random start
  locations move the units with their player. `Player=11` is the last filled
  seat and only takes the units when that seat is a computer, exactly as TADR
  says. Every other `Player` value makes nothing.
- **The keys.** `UnitName`, `XPos`, `ZPos` and `Player` are the required ones.
  `Ident` and `InitialMission` work as they do for a mission's units (the same
  `MissionScripts` interpreter, so waits, patrols, guards and names resolve).
  `YPos` and `HealthPercentage` are ignored: the unit stands on the terrain and
  is made at full health. `Angle` is still read by the shared record reader and
  applies if a map writes it. The other mission flags are read and ignored as
  the mission spawner ignores them, except `Immunity`, which it copies.
- **Numbering.** Records are read as `[unit0]`, `[unit1]`, ... until the first
  missing index, exactly as the mission reader reads them. TDF keeps only the
  last block of a given name, so a map that literally repeats `[unit0]` loses
  the earlier one; number the records distinctly.
- **No commander.** A player who receives at least one map unit is not given a
  commander. A map that wants one lists a commander among that player's units.
  A player whose listed units all fail to spawn still gets the ordinary
  commander, because they received nothing.
- **Deterministic.** The records are spawned in file order, after the seeded
  start-position deal, so every peer reaches the same unit ids. The units are
  ordinary units; the mission scripts they may carry are already hashed, saved
  and dumped, so the mode adds no simulation state of its own.
- **Where it is off.** Only the Community Patch spawns them. Without the flag
  a skirmish `[units]` block is parsed and ignored, as before.

Deliberate departures from TADR, all because RWE's model differs:

- **`CreationCountdown` is parsed but not acted on.** RWE has no delayed
  spawn queue, and its mission spawner ignores the field too; a unit the map
  gives a countdown appears at tick 0 rather than after that many seconds.
- **There is no neutral owner.** TADR leaves a unit with an unoccupied
  position, or `Player=11` when the last seat is not an AI, to TA's neutral
  slot (player 0xA). RWE has no neutral player, so those records are skipped
  with a `Map unit not placed` log line. A map that means them as scenery
  keeps its per-player units but loses the neutral ones. Giving RWE a neutral
  seat is a larger change than this slice and is not attempted here.
## Construction units stay put while guarding a factory (implemented)

TADR 2025.5.18 lists "Improved behaviour of con units when guarding a factory
- they stay put after finishing a build". The 2025.7.12 entry turns that into
an opt-in behaviour with three choices -- stay put, cavedog or scatter -- and
every build configuration except `tdraw-ota` enables it. RWE adopts the
stay-put rule for the Community Patch and has no cavedog/scatter selector.
This is RWE-original ruleset work under ADR-0006, not a byte-tactics
comparison: TADR's written feature list is the reference, not a decoded
routine.

RWE's ordinary guard path stops assisting when the frame is released, then
falls through to "stay close": a guard more than 200 world units from the
guarded factory's centre is told to walk in. A construction unit can finish a
build from farther out than that, because assisting only requires it to be
within its BuildDistance of the factory's *footprint*, so the completed build
could be followed by a walk towards the factory's centre. Under the mode a
ground construction unit guarding a factory stays where it is once it is
already within working reach and there is no frame to assist. It still walks to
a factory it cannot reach from where it stands, so a guard ordered from across
the map still arrives and assists. Aircraft guards are unchanged: a
construction aircraft's guard is the VTOL circuit described above, which TADR
does not touch.

The rule adds no simulation state. It is a condition on the guard's current
position and its build reach, both already saved and hashed, so there is
nothing new to hash, save or dump. Outside the mode the walk is untouched.

Uncertainty: TADR's written list describes the effect but not whether the
choice is stored per player or resolves some other way in the DLL. RWE applies
the rule for the whole mode, matching the "One ruleset, not per-feature
toggles" decision. The cavedog and scatter variants are not implemented
because RWE has no setting that selects among them.

## Build-ghost model override (implemented)

TADR 2026.5.15 adds a `PreviewObject3D=` unit FBI key that names an
alternative 3DO for the nanoframe ghost preview. RWE parses the key whatever
the mode and copies it into `UnitDefinition::previewObjectName`. The mode is
the only thing that decides whether it is read.

RWE had no model at the build cursor before this change -- only the footprint
box and its placement sweep -- so the key is given effect by drawing the same
green construction wireframe a placed nanoframe gets, centred on the build
cursor and at the height `computeBuildHeight` will give the finished
structure. The ghost appears only while the Community Patch is on, so a
normal game's cursor is unchanged; it is presentation only and never writes
simulation state, footprint, collision, placement height or the real unit's
model. A finished unit still draws `Objectname`.

`PreviewObject3D` is a 3DO base name without an extension, loaded from
`objects3d/<name>.3do` beside the unit's real model at load. When the named
model is absent or unreadable, load logs one warning and the ghost falls back
to the unit's ordinary `Objectname`; the fallback is decided per unit and never
per frame. If the model is missing from the database for any other reason, the
same fallback applies silently.

Deliberate departures from TADR, all because RWE's model and presentation
differ:

- **The ghost is gated on the ruleset.** TADR treats the nanoframe preview as
  a local preference. RWE has one ruleset and no separate preview setting yet,
  so the ghost and the key arrive together under `--community-patch`; outside
  the mode neither is drawn.
- **Rotation is fixed at the unit's default facing.** RWE has no rotate-build
  key yet, so there is no facing for the ghost to follow. `BuildAngle` is the
  settle arc the simulation applies when the building goes up, not a player
  choice, and is not used for the ghost.
- **The key applies to the model only.** TADR's `PreviewPieces`,
  `PreviewPieces<S|E|N|W>` and `PreviewFaceOpponent` keys are not implemented;
  RWE draws every piece of the override model at rest. A mod that relies on a
  piece whitelist will see more of the preview model than TADR would show.
- **The ghost is a wireframe.** It uses RWE's construction wireframe and
  build-cycle green rather than TADR's animated fill, since the existing
  renderer already has that shape and colour.

No original-game comparison applies: this is RWE-original presentation work
under ADR-0006, and TADR's written feature list is the reference.

## Where it is toggled

- **Launcher, single player:** the Play page has a Community Patch switch,
  saved in `launcher.json`, which passes `--community-patch` to the launch.
- **Command line:** `rwe --map "<map>" --community-patch`.
- **Multiplayer:** the host types `/community` in the battleroom; the rule
  travels with the setup and every guest launches with it.
- **Saves and replays** carry the flag as described above.

## The subset, by decision

### Adopted or planned for the mode (gameplay)

| TADR feature | RWE plan |
| --- | --- |
| weapon flags (`nottoair`, `nottounderwater`, `notoverwater`, `notoverland`, `surfacefire`) | **done** (slice 1) |
| allied projectile pass-through | **done**: a shot passes through units whose owner the firer currently treats as an ally; splash, interception and enemy rules unchanged |
| aircraft wrecks fall to the ground | **done**: a wreck created above the terrain falls straight down under gravity; ground and wet wrecks keep the original's behaviour |
| explosions damage every stacked aircraft, contested-cell tie-break | **already RWE**: the blast sweep walks every flying unit in range, not just the one occupancy cell, so stacked aircraft are all damaged; occupancy writes are deterministic in unit order, so no tie-break is needed |
| off-map aircraft targetable | **done**: a flying target wholly outside the map by at most one tile is visible from the nearest edge cell; the weapon still has to reach it and pass its ordinary hit rules |
| construction units stay put while guarding a factory | **done**: under the mode a ground guard already within working reach stays put after a build; out-of-reach approach and aircraft circuits are unchanged |
| patrol reclaim thresholds and `+setreclaimmetal`/`+setreclaimenergy` | **done**: per-player 0..100 levels of storage, default 20%, 0 never low, energy also gates assisting, air constructors follow the same rule |
| percentage share thresholds (`+setsharemetal 50%`) | **done**: a percentage tracks storage, a plain number stays absolute, and both are saved/hashed |
| repair-rate exploit fix | planned: the many-cheap-repairers fault is a real economy rule |
| share-abuse guard / `.take` refusal | evaluate against RWE's own sharing and rejoining rules; may stay RWE-original |

### Data/mod extensions (mode-gated where they change outcomes)

| TADR key | RWE plan |
| --- | --- |
| `VeterancyThresholds`, `VeterancyAccuracyBuffRate` | planned: RWE has veterancy; the per-unit keys are additive |
| `TransportedExplodeAs`, `TransportedSelfDestructAs` | **done**: the two keys replace the ordinary death and self-destruct weapons for a carried unit, independently |
| `PreviewObject3D` | **done**: the build-cursor ghost draws the named 3DO under the mode; a missing model falls back to the unit's ordinary model |
| neutral units and per-player spawn units in the map OTA | **done for per-player/AI seats**: `[units]` spawns under the mode; neutral owners are skipped because RWE has no neutral slot |
| weapon IDs above 255 | check RWE's parser first; likely already unrestricted |
| `nomapweaponalert` | **done**: a zero-damage, no-firer map weapon no longer raises the under-attack message or voice; RWE has no projectile alert dots to suppress |
| `reloadbar` | later: presentation with a clear RWE hook |

### Local preferences, not the ruleset

These change only what the local player sees or clicks, so they should be
client settings even in a community-patch game:

- mex/wreck snap radii and the alt override;
- building rotation, nanoframe ghost preview, move-queued-orders;
- unit status counters (stockpile/transport), chat drawer, `.mute`,
  accessible chat;
- the clock and weather report (`+clock` already exists; the wind/tidal
  readouts are a later HUD addition);
- megamap, whiteboard, ctrl-F2 menus and other TA-Hook UI.

### Not applicable to RWE

- TA Hook / DirectPlay / TAF / CRC and version checks: RWE has its own lobby,
  build identity and protocol.
- Memory limits, fixed-pool crashes, COB jump-table and other patches to
  `TotalA.exe` internals: RWE's engine is its own.
- GOG music port, tenth-replay-player POV and replay camera: RWE has its own
  music and replay/spectator UI.
- Lag-switch mitigation, vote-to-reject, `.take`: RWE's network model and
  drop/rejoin rules already cover these differently; they are RWE-original
  designs, not TADR ports.
- Megamap and whiteboard: RWE has no counterpart and no plan to copy one.

## Contracts for later slices

- A gameplay feature joins the **simulation** only through the ruleset flag;
  it must be deterministic, applied identically on every peer, and covered by
  the hash/save/dump rules that apply to the state it touches.
- The lobby must carry the flag before network games may use it; it does so as
  the `community` setup option, toggled by the host with `/community`, and the
  replay header carries it from version 8 (version 7 loads as the mode off).
- Local preferences never change orders or hashed state on their own.
- Each adopted feature cites TADR's own written rule (release and feature
  list) and states where RWE departs, because there is no original-game
  comparison to cite.
