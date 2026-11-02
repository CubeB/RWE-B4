# ProTA 4.8 integration plan

Status: proposal, 2026-11-02. Documentation only. This plan changes no code,
test, CMake file or accepted ADR, and **no ProTA feature is implemented in RWE
today**. RWE references below are pointers to existing code or explicit
proposals.

## Evidence boundary
ProTA evidence is third-party and **not** byte-tactics. It comes from Nanolathe's
documentation, read in full for this plan: `D:\tmp\ai-research\nanolathe\docs\PROTA_SUPPORT.md`,
`...\modconfigs\prota-4.8\nanolathe-mod.json` and
`...\research\extensions\prota-engine.md`. The engine document covers the ProTA
**4.8 release bundle dated 29 August 2025** and records an authorized
shipped-binary audit (22 and 24 September 2026) plus a **separate** reading of
the MIT TADR source at `dcff5ddeb6bd1030e3f452c0f16e5f005850f62f`; the current
`prota` build profile must not be substituted for the shipped 4.8 behaviour.
Nothing here was decompiled for RWE, and `D:\bytetactics` was not opened.

RWE's standing rule still applies: retail behaviour needs a byte-tactics
finding, which covers ProTA's **content** running on a retail-equivalent engine.
The **DLL-provided engine deltas** have no byte-tactics counterpart; they can
only be an RWE-original ruleset documented against prose, exactly as ADR-0006
treats the Community Patch. No original-game parity is claimed, and every
adopted rule needs its own finding plus a divergence-register entry.

## 1. What ProTA 4.8 is
ProTA 4.8 is a Total Annihilation 3.1 rebalancing distributed as a content
package (`ProTA.gp3`) together with a patched-looking runtime. It is **not a
patched executable**:

- The shipped `TotalA.exe` is byte-identical to the reference 3.1 image
  (SHA-256 `3b9c0fad...`, version resource `v3.1`); every engine change reaches
  the process in memory.
- `dplayx.dll` validates the host image and patch bytes, applies the "`.exe`
  hacks", loads `tdraw.dll`, redirects two call sites, and forwards DirectPlay
  to `tplayx.dll`; a mismatch exits with *Incompatible game files detected*.
- `tdraw.dll` is the renderer/session extension (megamap, whiteboard, selection,
  drawing hooks, challenge/response, ten-player handling); `tplayx.dll` is the
  TA Demo Recorder lineage; `win32.dll` is the imported music proxy, with
  `tmusi.dll` and `wgmus.dll` as alternative backends.

**RWE cannot load any of these DLLs, and must not try.** They patch a specific
Windows executable, use DirectDraw/DirectPlay and a Windows ABI, and the
provenance of the shipped builds is not established. RWE takes ProTA's
**content and documented effects only**; it does not reproduce the loader, the
patch gate, the CRC/challenge-response, or the DLL ABI.

## 2. Content layout and limits
ProTA renames its content roots. This is the one part of the package RWE can
adopt with no new engine mechanism.

| Item | Documented by the ProTA evidence | RWE decision | Where |
| --- | --- | --- | --- |
| Content roots | `downloadP`, `gamedatP`, `guiP`, `unitpicsP`, `weaponP`; empty `downloadP` | Per-mod resource-directory mapping, not hard-coded roots | `resourceDirectories` in `rwe_mod.json`; `launcher/src/common/mods.ts`; `launcher/src/launcher/rwe.ts`; `src/rwe/PathMapping.{h,cpp}` |
| Shared roots | `units`, `ai`, models, scripts, textures, sounds, anims keep retail names | Unchanged defaults | `PathMapping.h` |
| `guiP` | Front-end windows **and** builder pages | Map both `guis` and `builderGuis` to `guiP` | same |
| 16 000 unit / weapon definitions | ProTA raises both identifier limits | No fixed table exists: definitions are keyed by name and weapon ids are `unsigned int`. No limit to raise; measure load time | `GameSimulationLoader.cpp`; `WeaponDefinition` |
| 64 MiB TNT | Documented map-size budget | No byte cap. `TntArchive` streams and bounds by dimensions (8192, 65536); verify the largest ProTA map | `src/rwe/io/tnt/TntArchive.cpp` |
| 8 MiB LOS | Documented LOS budget | RWE parses `gamedata/los.tdf` into ray fans; no 8 MiB image. Verify table count and radii, and the generated fallback (`DefaultLosTableCount` 9) | `src/rwe/sim/LosTables.h` |
| Unit limit 1500 | ProTA config, shown on its loading screen | A **map/profile value**, not a pool size: OTA `maxunits` becomes the AI request limit (default 200). Never a hard-coded constant | `src/rwe/io/ota/ota.cpp`; `GameSimulationLoader.cpp`; `AiSeat.h` |
| 3 000 projectiles / 3 000 explosions / 1 000 debris; SFX 20 480; path cycles 66 650 | ProTA engine defaults | No located fixed pool or shared budget in RWE; stress-test targets, not constants to port | `src/rwe/sim/Projectile.h`; effect, sound and pathfinding paths |

`ModManifest` format 1 has **no limits, no resource-directory map and no
presentation block**; those live in the legacy `rwe_mod.json` fields the
launcher reads, and adding a limits block would change the resolved identity (a
MOD-02 revision, not an ad-hoc key). The Nanolathe config schema 2 is **not**
RWE's manifest; do not copy it or treat it as a second format.

## 3. Features: documented effects and RWE work
Classification: **(a)** already available in RWE's Community Patch
(`docs/COMMUNITY-PATCH-MODE.md`, ADR-0006); **(b)** a ProTA profile or parameter
over existing code; **(c)** new engine work or new simulation state; **(d)**
presentation only. "Already available" means the mechanism exists, **not** that
it has been checked with ProTA content.

### 3.1 The nine historical 4.8 switches
These are Nanolathe's switch names, **not** an RWE API. RWE should not adopt
nine user-visible toggles: ADR-0006 deliberately chose one named ruleset.

| Switch | Documented effect | Class | RWE integration note |
| --- | --- | --- | --- |
| `aiDifficultyIncome` | AI production and feature-reclaim income x0.5 / x1 / x4 | **b** | Scale at RWE's per-unit production/refund boundary using the per-seat difficulty. Runtime rule, so a pre-match Lua transform cannot express it. `GameSimulation` economy pass, `ResourceSettler`, `Energy.h`, `Metal.h` |
| `aiStockpileProducts` | AI queues nuke/anti-nuke rounds for stockpile producers | **c** | Stockpile state and firing exist, but the AI builder has no producer for a counted stockpile order. New planner task; `UnitWeapon.h` |
| `targetLockRelease` | A weapon drops an unreachable unit target and finds another | **c** | RWE stores a typed target per weapon, not retail's three-slot scan. Needs an explicit retention/reacquisition rule, not a patch port |
| `aiApplianceEnergy` | AI low-energy shutdown picks appliances by energy use | **b** | Replace the AI's metal-maker selector with an `energyuse` threshold. The documented bit test is an artifact, not a rule to copy |
| `aiBuilderStopThreshold` | AI builder placement stops at ten, not five | **b** | Count gate in the AI construction task. The documented asymmetry (placement 10, repair patrol 5) is a quirk decision |
| `workingWeaponsAutonomous` | Builders keep firing while building, assisting, repairing, reclaiming, capturing | **c** | RWE has `UnitFireOrders` and per-weapon state, not the retail release/inhibit pair. Define the effect in RWE's order model, with explicit VTOL and `RepairUnitNoMove` exclusions if wanted |
| `attackSingleSlotTake` | An attack order takes one weapon; the others keep targeting | **c** | Verify RWE's current attack order first; it may already be close. Then decide the rule and test multi-weapon units |
| `mapFeatureOwnerEleven` | Map walls stay drawn in explored cells outside LOS | **c** | Needs a placer/owner value on `MapFeature`, save/hash additions, `nodrawundergray` parsing and a tall-feature draw rule: `MapFeature.h`, `GameScene_render.cpp`. High cost, low gameplay value; defer |
| `resurrectionTextFix` | Failed resurrection reads *Resurrection failed* | **d** | One caption string in RWE's own text path |

None of the nine is already available: three are parameter/policy changes in
existing AI and economy code, five need new order or simulation state, one is a
string.

### 3.2 The broader Community 3.9 table
`nanolathe-mod.json` sets these under `rules.communityFeatures`; the list and
values are ProTA's choices, not RWE's.

**(a) Already available in RWE's Community Patch.** `weaponTargetKeys`
(`nottoair`, `nottounderwater`, `notoverwater`, `notoverland`, `surfacefire`),
`veterancy`, `schemaUnits` (skirmish `[units]`), `transportedExplosions`,
`guardingBuildersHold`, off-map aircraft margin (one tile, matching ProTA),
`areaDamageOverflow` (stacked aircraft), `gridClaimTieBreak` (RWE's occupancy
writes are already deterministic), and weapon ids above 255 (already RWE in
both modes). `airCorpseFall` is available but ProTA's config leaves it **false**.

**(b) ProTA profile over existing code.** `unitLimit` 1500, no repair
multiplier, `aiDifficultyIncome`, `aiApplianceEnergy`, `aiBuilderStopThreshold`,
and the projectile/explosion/debris/SFX budgets (validation and performance).
`mexSnap`/`wreckSnap` radii 3/1 are input preferences in RWE's model, not
simulation rules; make them local settings, never networked ruleset state.
`buildWeaponSlotGuard` false, `healTimeBitmask` false and
`aiBuilderPlacementLimit` 0 need no work.

**(c) New engine work needed.** `aiStockpileProducts`,
`workingWeaponsAutonomous`, `attackSingleSlotTake`, `mapFeatureOwnerEleven`,
`constructionKickout` (no build-under-unit rule), `structureRotation` (no
rotate-build key or rotation state), `patrollingBuilderFilters` (RWE has the
patrol-reclaim threshold, not the hold/maneuver/roam presets), and
`reclaimToggleKeepsBuild`, `areaDamageDedupCap`, `antinukeCircularCoverage`,
`alliedJammingIgnored`, `resurrectionFinalization` (no RWE counterpart; each
needs a finding before code). `scriptPorts` would be engine work, but ProTA's
compiled scripts make **no extended-port reads**, so it is not required for 4.8
content.

**(d) Presentation only.** `resurrectionTextFix`, the main-menu `4.8` label,
ProTA palettes and pink/slate colours, build pictures, `Icon/iconcfg.ini`,
victory cue, counters, clock/weather readout, megamap and whiteboard. RWE has
**no** megamap, whiteboard or `iconcfg.ini` reader today; megamap/whiteboard are
not part of this plan. Reload bars and the "Veteran" label already exist.

## 4. Phased delivery
Each phase is independently reviewable and can stop. No phase may claim ProTA
compatibility beyond what it tested.

### P0 - Pin the target and write the findings
Choose **historical 4.8** or **current TADR `prota`** as the rules target; the
evidence says they differ. Record per-feature findings against the Nanolathe
document, the ProTA artifact hashes, and divergence-register entries. Exit: one
approved feature list with each row classified.

### P1 - Package, manifest and launcher entry
Create a package directory whose `rwe_mod.json` satisfies **both** readers: the
launcher requires a non-empty `shortname`; format 1 requires `id` and `version`
and takes `kind` and `api`.

```json
{
  "manifestVersion": 1, "id": "prota", "version": "4.8.0", "kind": "content",
  "api": 1, "shortname": "ProTA", "name": "ProTA 4.8",
  "author": "<as published>",
  "resourceDirectories": {
    "downloads": "downloadP", "gamedata": "gamedatP", "guis": "guiP",
    "builderGuis": "guiP", "unitpics": "unitpicsP", "weapons": "weaponP"
  }
}
```

- `version` must be `4.8.0`, because format 1 accepts only `major.minor.patch`;
  keep "4.8" as the display name or an option, never the version field.
- `kind: content` is a content package, not a mutator. It needs no `entry`
  script and no `features`; no transform is applied.
- `requires` and `features` stay empty: ProTA needs no other package and no
  transform or runtime feature.
- The package id and author are **not** ours to invent; see the questions.
- Without `shortname`, the manifest is ignored and the mod is listed under its
  folder name.

Work: place the package in managed storage, show its detected name and version,
and confirm `rwe_setup --validate-mod` and `--resolve-mods` produce the same
identity on repeated runs. The resolved identity is the SHA-256 of the canonical
set (manifest bytes, options, order), deliberately excluding the folder path, so
a moved or renamed package still loads its saves (MOD-SDK-003). The full Library
UI is `docs/MOD-AND-MAP-MANAGEMENT-PLAN.md`. Exit: the package is listed,
selectable, resolvable and fingerprintable.

### P2 - Load the content
Launch with the ProTA data path **first** and the base install second. RWE's VFS
returns the first match (`CompositeVirtualFileSystem::readFile`), and the
launcher already orders active mods that way for TAESC. The directory options
travel through `mods.ts`, `rwe.ts` and `PathMapping`.

Record a census on first load: unit definitions, weapon definitions, builder GUI
pages, maps, and resolved `SIDEDATA`/palette paths. Pin it as a fixture; do not
claim a count from the ProTA documentation. Exit: a ProTA skirmish map loads
with ProTA units, weapons, menus, pictures and palettes, and no DLL or `.exe` is
read or executed.

### P3 - Rules profile
Add a **named** ProTA rules profile beside the Community ruleset. This is a
proposal: today the ruleset is the single boolean
`GameParameters::communityPatch` / `GameSimulation::communityPatchMode`, carried
by the lobby's `community` setup option and by saves and replays. Moving to
`off | community | prota` changes `GameParameters.h`, the lobby option, save and
replay headers and launcher argument serialization, and needs an ADR-0006 review
because ADR-0006 chose one ruleset rather than per-mod switch sets.

Implement (b) features first, then (c) one at a time, each with a unit or AI
scenario and a hash/save check. Exit: the profile is off by default, required
for a ProTA game, carried to every peer and recorded in saves and replays, and
no stock game changes.

### P4 - Runtime hooks (only if P3 needs them)
The nine switches are runtime policy, not pre-match definition transforms, so
the Lua mutator surface is the wrong layer. `ModRuntime` exposes only a damage
fold and a per-tick event, not a place to hide engine rules. Prefer native
ruleset code; use `ModStateStore` only if a rule needs persisted per-unit state,
hashed and saved normally. No runtime hook is planned for the first release.

### P5 - Acceptance on real ProTA 4.8 data
| Check | Method | Pass condition |
| --- | --- | --- |
| Package identity | `rwe_setup --inspect-package`, `--validate-mod`, `--resolve-mods` twice | Same id, version, layout and resolved identity; no absolute path in the identity |
| Load, census, menus | ProTA map, ProTA path first; ARMCOM/CORCOM and Core directional shipyards | Counts match the pinned census; `SIDEDATA`, `guiP` pages and palettes resolve from ProTA roots; twelve-slot pages, overlays and portraits draw; `CORSYE`/`CORSYW` buttons work despite empty CANBUILD lists |
| Skirmish, save, replay | One AI seat; mid-game save, resume, replay | Builds and fights without script or asset errors; hashes agree between uninterrupted, resumed and replayed runs; mod identity recorded and mismatches refused |
| Multiplayer fingerprint | Two peers with identical package bytes | Identical digest and launch; a changed archive refused. A changed **manifest only** is refused only once manifest hashing is wired (see risks) |
| No DLL execution | Process, log and loaded modules | No `dplayx`/`tdraw`/`tplayx`/`win32`/`tmusi`/`wgmus`; nothing from the package executed |
| Quirk probes | Build `CORSILO` and `CORAMPH`; fire anti-missiles | Both build despite the trailing script piece their models lack; `toaironly=1` unread matches the shipped package and is recorded |

## 5. Non-goals and risks
**Explicit non-goals**

- Loading, unpacking, executing or emulating any ProTA DLL or installer; no
  `.exe` patch-site emulation, no "compatible game files" gate.
- ProTA's CRC reports, challenge/response, `.exereport`/`.crcreport`, or network
  interoperability with original 3.1/ProTA clients.
- TA demo, TAD, ten-player POV replay, or importing original-game saves.
- Megamap, whiteboard, ctrl-F2 menus and TA-Hook internals; at most later
  presentation work, and RWE has none today. ProTA campaigns are also out of the
  first release: the package overlays campaign files, but RWE's campaign support
  is not part of this plan.
- Any claim of original-game or ProTA parity, or that a ProTA feature "works".

**Risks**

- **Target ambiguity.** The historical 4.8 binaries, the current TADR source and
  the Nanolathe config are three different things; mixing them matches nothing.
- **Content-only looks wrong.** Without the AI and order switches, ProTA's
  balance and AI behave differently from the package's intent; say so.
- **Manifest identity gap.** `CONTEXT.md` defines the mod fingerprint as the
  archives and their hashes, and `launcher/src/common/archives.ts` on this
  branch hashes archives only. The manifest-hash step recorded in
  `MODDING-SDK-FINDINGS.md` MOD-SDK-006 is in open PR #969, which must land
  alongside this plan's engine side before multiplayer is advertised. Until
  both are merged, two peers could agree on archives while launching with
  different directory mappings; close this before advertising multiplayer, and
  note that every new rule also changes save, replay and hash surfaces.
- **Script tolerance and scale.** ProTA ships scripts whose trailing piece is
  absent from the model; if RWE rejects that, `CORSILO`/`CORAMPH` fail. Also,
  1500 units, large maps and many definitions stress AI, pathfinding and memory
  in ways RWE has not measured.
- **Licensing and provenance.** The shipped 4.8 DLL provenance and licence are
  not established; the current TADR tree is MIT but is not the distributed
  artifact. RWE takes prose only and ships no third-party binaries.

## 6. Blocking questions
1. **Target:** historical 4.8 package behaviour, or the current TADR `prota`
   profile? A mixture is not a target.
2. **First release:** which of the nine switches are required, is content-only
   (P1+P2) acceptable first, and is multiplayer in scope? Multiplayer raises the
   manifest-identity gap to a blocker.
3. **Package form and identity:** do users supply the ProTA folder with its exe
   and DLLs, or a content-only `ProTA.gp3`; what package id and author may RWE
   use; and may RWE ship a ProTA manifest, hashes or profile entry at all?
4. **ProTA UI:** are the "4.8" label, `Icon/iconcfg.ini` and ProTA
   palettes/team colours acceptance items or optional?
5. **Limits and quirks:** is a 1500-unit game on ProTA's large maps a
   performance target, and where documented 4.8 behaviour is probably a defect
   (asymmetric builder threshold, megamap placement), reproduce it or record a
   deliberate RWE departure?
