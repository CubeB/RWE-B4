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

## The subset, by decision

### Adopted or planned for the mode (gameplay)

| TADR feature | RWE plan |
| --- | --- |
| weapon flags (`nottoair`, `nottounderwater`, `notoverwater`, `notoverland`, `surfacefire`) | **done** (slice 1) |
| allied projectile pass-through | planned: projectiles ignore units whose owner the firer currently treats as an ally; both native and off-map collision paths |
| aircraft wrecks fall to the ground | **done**: a wreck created above the terrain falls straight down under gravity; ground and wet wrecks keep the original's behaviour |
| explosions damage every stacked aircraft, contested-cell tie-break | planned: deterministic tie-break is required for lockstep |
| off-map aircraft targetable | planned: a map-edge margin (TADR's per-build radius) |
| construction units stay put while guarding a factory | planned once RWE's guard/assist order is compared with TADR's |
| patrol reclaim thresholds and `+setreclaimmetal`/`+setreclaimenergy` | planned; TADR's percentages are absolute of storage and reset per game |
| percentage share thresholds (`+setsharemetal 50%`) | planned on top of RWE's existing reserves |
| repair-rate exploit fix | planned: the many-cheap-repairers fault is a real economy rule |
| share-abuse guard / `.take` refusal | evaluate against RWE's own sharing and rejoining rules; may stay RWE-original |

### Data/mod extensions (mode-gated where they change outcomes)

| TADR key | RWE plan |
| --- | --- |
| `VeterancyThresholds`, `VeterancyAccuracyBuffRate` | planned: RWE has veterancy; the per-unit keys are additive |
| `TransportedExplodeAs`, `TransportedSelfDestructAs` | planned: RWE has transport deaths, so the keys map directly |
| `PreviewObject3D` | planned as a build-ghost presentation key |
| neutral units and per-player spawn units in the map OTA | planned; RWE reads `[units]` for missions but not for skirmish maps |
| weapon IDs above 255 | check RWE's parser first; likely already unrestricted |
| `reloadbar`, `nomapweaponalert` | later: both are presentation with a clear RWE hook |

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
