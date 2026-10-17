# Weapon definitions, flags, asset binding and damage tables parity audit

Spec-room finding, 2026-10-16. Like `BT-ORDERS-UNIT-ORDERS-PARITY.md`,
`TA-SYSTEMS-PARITY.md` and `NAVAL-SYSTEMS-PARITY.md`, it allocates no section number
in the main register. This is the finding that implementation work cites for how a
weapon definition is read from `Weapons/*.tdf`: discovery and ids, the numeric fields
and their defaults, the flag word, model sharing, explosion art, sound binding and the
damage table, per `AGENTS.md` and ADR-0004. What a definition *does* once a unit fires it
is `BT-WEAPONS-WEAPONS-PARITY.md`; the explosion art's runtime choice is
`BT-WEAPONS-EXPLOSIONS-PARITY.md`. This audit restates none of them.

The register already holds the loader's key table (`TOTALA-EXE-DATA.md`, "Weapon TDF key
names are compared through `0x42E484`-`0x42EFC3`", with every offset, scale and default)
and the flag word (bits 0 to 30, same section). The chapter is a prose account of the same
function, and **its numeric tables agree with the register on every row**; so this audit is
almost entirely about where RWE's loader differs from the table both of them give. The
differences are real but, on the shipped data, mostly latent; the census says which.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\weapons-weapon-types.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none of its
  source links were followed. Addresses are the ones the chapter names; rules are
  restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only, so that
  rows already resolved there are not raised again. No row concerns the weapon loader.
- **Evidence class.** A **static reading** by that project: nothing in it was executed.
  Weaker than `TOTALA-EXE*.md`, which were read from the retail executable.
  Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle was
  run. The Gaps table says which items an original check settles.
- **The RWE half** is a read-only audit of `src/rwe/io/weapontdf`, `src/rwe/io/tdf` and
  `src/rwe/LoadingScene_util.cpp` (`parseWeaponDefinition`, `parseWeaponMediaInfo`) at the
  working tree of 2026-10-16. Nothing was built, run or changed, and tests were not run;
  a MATCHES below means the code reads as the rule says. Another agent had uncommitted
  edits in the tree while this was written, so every line number below is "about".
- **A census of the shipped weapon TDFs** (retail install through `tools/hpi.py`: 184
  distinct weapon names across `totala1.hpi`, `ccdata.ccx` and the `.ufo` patches, scratch
  script, last definition wins) supplies the "what would this change on shipped data"
  figures. It is a count of keys in files, not a play result.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Discovery, ids and the 256-record table
2. Numeric fields, units and defaults
3. The flag word
4. Speed, lifetime and the physics collapse
5. Models, explosion art and the lava alternative
6. Sound binding
7. The damage table
8. Parsed and unused keys; keys not parsed
9. Gaps to fix

---

## 1. Discovery, ids and the 256-record table

**Reference (section 1).** The game embeds **256** weapon records and assigns each its
byte index. Every `Weapons/*.tdf` is enumerated, every top-level section is a weapon, and
the section's **`id` key (default -1) indexes the embedded array directly** with no
0..255 validation and no automatic allocation; a missing or out-of-range id can index
outside the array and a duplicate id silently overwrites the same record. The section name
becomes the 32-byte internal name by an unbounded copy and the `name` key the 64-byte
description. A unit's weapon name is resolved by `FindWeaponByName`: the **first**
case-insensitive match walking the records **in id order**. A weapon whose id is zero is,
for the slot enable test, indistinguishable from "no weapon" (`BT-WEAPONS-WEAPONS-PARITY.md`
rule 3).

**RWE.**

- **Keyed by name, not by id: DIFFERS by design.** `parseWeaponTdf` (`WeaponTdf.cpp`
  about 144) returns one entry per section and the loader stores them by name; `id` is read
  (about 27) into `taWeaponId` (`parseWeaponDefinition`, about 160), an unsigned value that
  defaults to zero when the key is missing (the original's -1), and is used only by the
  demo and TA-network paths. There is no 256 cap, no id collision and no out-of-range index.
  A name lookup that finds two definitions with the same name resolves by load order (the
  later archive wins, per `TA-SYSTEMS-PARITY.md` rule 32), where the original resolves by the
  lowest id: **UNCLEAR** which is the same when two mods ship the same name with different
  ids. Mod data only.
- **What shipped data does.** The 182 sections that name an id use 1 to 227 with no
  duplicate and no zero; two sections (`damage`, a stray block, and `vtol_foomissile`) name
  none and no unit references either. So the original's id-indexed behaviours are unreachable
  on shipped content. The `id=0` quirk and the duplicate-overwrite rule are not worth
  porting; RWE's name keying is the safer design and the security audit's rule (a fault in
  content costs that content) is already satisfied.
- **The embedded 256 limit: not applicable.** With 184 shipped names the limit is far off.
  A mod with more than 256 weapons would crash the original; RWE loads it.

## 2. Numeric fields, units and defaults

**Reference (section 2, 3).** The chapter's table and the register's agree, field by field:

| Field | Original (chapter and register) | RWE | Verdict |
| --- | --- | --- | --- |
| `weaponvelocity`, `startvelocity` | integer times 65536/30 (16.16 per tick), default 0 | `value / 30` as a float scalar (`LoadingScene_util.cpp` about 168 and 226) | **MATCHES** |
| `weaponacceleration` | times 65536/900, default 0 | `value / 900` (about 227) | **MATCHES** |
| `range` | signed integer, **default 32767** | unsigned, **default 0** (`WeaponTdf.cpp` about 30, `maxRange` about 163) | **DIFFERS** |
| `coverage` | signed integer, default 0 | unsigned, default 0 (about 85) | **MATCHES** |
| `reloadtime` | seconds times 30, unsigned short | float seconds, converted by `deltaSecondsToTicks` (truncating, `GameTime.cpp`) | **MATCHES** (no 16-bit narrowing) |
| `energypershot`, `metalpershot` | float, default 0 | float | **MATCHES** |
| `areaofeffect` | unsigned short, default 0 | unsigned, the radius is half (about 267) | **MATCHES** (half radius per the register) |
| `edgeeffectiveness` | float, default 0 | float | **MATCHES** |
| `weapontimer`, `burstrate`, `duration`, `randomdecay`, `smokedelay`, `flighttime`, `holdtime` | seconds times 30, unsigned short, default 0 | float seconds times 30 truncated; `duration` **default 1/30** | **DIFFERS** for `duration` |
| `turnrate` | unsigned short, input divided by 30 | `tdf.turnRate / 30u` for a self-propelled weapon (about 228); the **raw** value for a `tracks` weapon without `selfprop` (about 241) | **MATCHES** for the shipped cases |
| `minbarrelangle` | float radians, default -11.25 degrees | parsed and never read (`WeaponTdf.cpp` about 91) | **MATCHES** the register (inert) |
| `burst`, `sprayangle` | unsigned short | unsigned | **MATCHES** |
| `accuracy` | signed short | unsigned, then `SimAngle` | **MATCHES** for the shipped non-negative values |
| `tolerance`, `pitchtolerance` | unsigned short; zero means the 2000 or 150 default | **default 256**, pitch defaults to the heading figure (about 74, 79) | **DIFFERS**: `BT-WEAPONS-WEAPONS-PARITY.md` rule 4 (74 turret weapons affected) |
| `firestarter`, `rendertype`, `color`, `color2` | bytes | `fireStarter` clamped 0 to 100; others as parsed | **MATCHES** |
| `shakemagnitude`, `shakeduration` | integer; integer seconds times 30 | unsigned; float seconds times 30 | **MATCHES** |

- **`range` default: DIFFERS, latent.** A weapon that names no `range` has the reach of the
  whole map in the original and **none** in RWE (a range of 0 against a flat-range test).
  The census finds four sections without it: `damage` (a stray block), `vtol_foomissile`
  (unreferenced), and the two meteors `earthquake` and `hailstorm`, which no unit fires. A
  mod weapon that leaves `range` out, which a quick test weapon often does, would work in
  TA and be silently unusable in RWE.
- **`duration` default: DIFFERS, presentation.** RWE defaults it to a thirtieth of a
  second (one tick) and then **doubles** it for the laser draw (`LoadingScene_util.cpp`
  about 700 and 757, with the comment "duration seems to match better if doubled"). The
  original's default is zero, and the chapter's account of a beam (section 9 of the weapon
  chapter: the beam's start stays fixed for `duration` ticks and then advances with the head)
  makes `duration` the length of the beam in ticks of travel. Seventeen shipped weapons set
  `duration`; the other beam weapons take the default. This is a hand-tuned departure with an
  undocumented reason; **UNCLEAR** what the original draws for a zero duration (probably a
  beam one tick of travel long, which is what the doubling approximates for the shipped
  weapons that name none). Presentation only.
- **Truncation of timer fields: MATCHES** (float times 30, truncated). A case worth
  knowing: `.1 * 30` in a float is just over 3 and truncates to 3, `.04 * 30` is 1, and
  `.2 * 30` is 6, so the shipped burst rates are 3, 1 and 6 ticks in both.
- **Malformed numbers: MATCHES.** `13O` in `armvtol_advmissile2`'s `weaponacceleration` (a
  letter O) reads as 13 under the original's C conversion and under RWE's stream
  extraction (`tdfTryParse`, `TdfBlock.h` about 14-27), both stopping at the first non-digit.
  A value with *no* numeric prefix fails in RWE and gives the default where the original
  gives 0 (`TA-SYSTEMS-PARITY.md` rule 34): a mod-data difference only.

## 3. The flag word

**Reference (section 4).** Thirty-one flags, "each assigned to a one-bit member using the
**low bit** of the parsed integer; a nonzero even value is not equivalent to normalised
true". Bit 31 is unassigned.

**Register.** `TOTALA-EXE-DATA.md` lists the same bits 0 to 30 in the same order.

**RWE.**

- **Low bit versus nonzero: DIFFERS, latent.** `tdfTryParse<bool>` (`TdfBlock.cpp`) is
  `value != 0`. A key written `lineofsight=2` is true in RWE and false in the original; `-1`
  is true in both. The census finds **no shipped flag with a value other than 0 or 1**. Mod
  data only.
- **Two flags are not parsed.** `shellweapon` (bit 2) and `meteor` (bit 5) have no member
  in `WeaponTdf`. `meteor` belongs to the meteor shower, recorded as not implemented
  (`TOTALA-EXE-WEAPONS.md` section 7); `shellweapon` is named by the chapter and by the
  register's table with no consumer ("shell-style behaviour"). Census: no consequence is
  known.
- **Parsed and never read by the simulation or the scene:** `noradar` (bit 6),
  `propeller` (bit 21, the projectile's rotating child bank in the render type 1 draw),
  `beamweapon` (bit 3, which the register's section 92 calls dead code for the D-gun
  and which only drives the drawn beam's start point), `aimrate` and `holdtime` (below),
  `weapontype2`, and the weapon block's `energy` and `metal` keys (read into the TDF
  record and never used). `noradar`'s effect in the original is the
  chapter's "weapon radar-related policy" with no detail; **UNCLEAR**, and it is not
  referenced by any shipped weapon the census could connect to a rule.
- **`aimrate` is not a key: MATCHES the register** (the string is absent from the
  executable; RWE parses and ignores it, which has the same effect).
- **`holdtime`: MATCHES.** Parsed, unused. The register (section 82 correction) says the
  field holds the camera on the impact for the followed projectile; nothing in the
  simulation reads it, and the follow camera is presentation.

## 4. Speed, lifetime and the physics collapse

**Reference (sections 4, 6, 7 of the weapons chapter; section 4 here).** The fire handler is
chosen from the *completed* flag word. A projectile's scalar speed comes from one rule shared
by the three spawn families: **explicit `startvelocity` if nonzero, otherwise the maximum
speed when `weaponacceleration` is zero, otherwise zero**. Its expiry comes from another:
**range times 65536 divided by the speed when the speed is nonzero and `noautorange` is
clear, otherwise `weapontimer`**.

**Register.** Section 7 gives both rules (`0x49C980`, `0x49C920`) and confirms them against
the demo corpus for self-propelled weapons.

**RWE.**

- **Self-propelled: MATCHES** (`createProjectileFromWeapon`, `GameSimulation.cpp` about
  2480-2518): the speed pick is the register's, the burn is `range / weaponvelocity` ticks
  unless `noautorange`, in which case `weapontimer` (`autoRange` at `LoadingScene_util.cpp`
  about 236).
- **Line-of-sight and ballistic: the speed rule is not applied.** For every other family
  RWE sets the projectile velocity to the direction times `weaponvelocity` (about 2419) and
  never looks at `startvelocity` or `weaponacceleration`. The original applies the pick to
  all three spawn routines. **One shipped weapon is affected: `vtol_emg`**, the Arm Brawler's
  gun (`armbrawl`, weapon 1): `lineofsight`, `weaponvelocity=450`, `startvelocity=150`,
  `weaponacceleration=200`. On the chapter's reading its rounds leave at 150 per second (5
  world units a tick) where RWE flies them at 450 (15 a tick); a line-of-sight round does
  not accelerate, so the acceleration plays no part. The lifetime rule's speed term is not
  stated precisely enough to say whether the round then covers its whole 370 range
  (lifetime from the leaving speed) or dies short (lifetime from the maximum). **UNCLEAR**,
  needs the register's decode of `0x49C9C0`, and an original check of what the Brawler's gun
  does.
- **`noautorange` on a line-of-sight weapon: DIFFERS, latent.** RWE derives a line-of-sight
  round's life from `range / velocity` unconditionally and uses `weapontimer` only for the
  other families (about 2447-2461). The original honours `noautorange` there too. The
  census: `noautorange` appears on 13 of the 61 self-propelled weapons and on **no** other
  weapon, so the difference is unreachable on shipped data. A mod that sets it on a laser
  would see the laser fly to `weapontimer` in TA and to its range in RWE.
- **The physics collapse: DIFFERS.** `parseWeaponDefinition` (about 219-262) collapses the
  flags into one variant: selfprop, tracks, lineofsight, dropped, ballistic. The original
  keeps the flags and dispatches differently for firing, flight and receiving. See
  `BT-WEAPONS-WEAPONS-PARITY.md` rule 2 for the consequences; for the loader, the thing to
  know is that a weapon is classified once at load and that the order differs from all three
  of the original's.
- **A ballistic weapon with a lifetime: MATCHES.** `weaponTimer` is set only when nonzero
  (about 270) and a zero value leaves the round flying until it collides, which is the
  chapter's "zero lifetime keeps the ballistic flight going until collision".
- **`randomdecay`: MATCHES in shape.** `randomBelow(rng, randomDecay + 1)` minus half the
  decay (`createProjectileFromWeapon`, about 2458) jitters the expiry about the base, as
  the chapter says; it draws from the simulation's generator, which is the right stream.

## 5. Models, explosion art and the lava alternative

**Reference (sections 5, 6).** A `model` key scans the records with a **lower id** for a
matching model name and borrows that pointer, otherwise loads `objects3d/<model>.3DO`
(`FatalError` if missing), mirrors it and binds textures; ownership is tracked by a
non-empty name on the owning record. The ordinary explosion needs **both** `explosiongaf`
and `explosionart`; its animation entry has byte +2 zeroed. The second effect is the
splash: **a nonzero mission field at `+0xd44` selects `lavaexplosiongaf`/`lavaexplosionart`,
otherwise `waterexplosiongaf`/`waterexplosionart`**, one pair only, chosen at load time. A
missing string leaves that effect null with no substitution.

**RWE.**

- **Model sharing: not applicable.** The scene loads projectile models by name through
  the media database (`parseWeaponMediaInfo`, about 705-760); ownership and teardown are RAII.
  A missing model leaves the projectile drawn as nothing (`ProjectileRenderTypeNone`)
  where the original aborts: **DIVERGENCE (by the security rule)**, and the better design.
- **Ordinary explosion art: MATCHES** (both strings required, about 778-781).
- **The lava alternative: MISSING.** RWE reads only `waterexplosiongaf`/`waterexplosionart`
  (about 784-787) and uses them for every water impact (`GameScene_util.cpp` about 76). The
  OTA's `lavaworld` key is parsed (`ota.cpp` about 46) and read by nothing in the engine.
  Every shipped weapon that has a water splash has a lava pair next to it (for example
  `vtol_emg`: `lavasplashsm`), and **44 of the 272 shipped maps set `lavaworld=1`**
  (16 percent). On those maps the original splashes lava; RWE splashes water. Presentation
  only; the reader is `BT-WEAPONS-EXPLOSIONS-PARITY.md` rule 4 for how the choice is made at
  runtime.
- **A water sound or art with no ordinary pair:** the chapter says nothing is substituted;
  RWE agrees (`GameScene_audio.cpp` about 608-615 plays nothing without a water sound;
  `computeWeaponImpactEffects` takes the water animation or none).

## 6. Sound binding

**Reference (section 7).** `soundstart`, `soundhit` and `soundwater` each resolve through the
sound loader; absent names store `0xffff` and nothing falls back (a missing water sound is
not replaced by the hit sound). `soundtrigger` is a timing flag, and loading a start sound
does not play it.

**RWE: MATCHES.** The three names are optional strings (`parseWeaponMediaInfo`, about
667-678); an impact plays the matching one or nothing (`GameScene_audio.cpp` about 590-620).
`soundTrigger` is carried to the media info. The `soundtrigger`-driven burst playback (the
chapter's "optional soundtrigger playback" on each child) is a presentation question that
was not traced.

## 7. The damage table

**Reference (section 8).** The `DAMAGE` subrecord's `default` is an **unsigned short**
(default 0); every other key is a **signed integer** override keyed by name. The table is
allocated on the first nondefault entry. The sorted lookup orders **case-insensitively**
but decides equality **case-sensitively**, so two spellings of one unit name can both be
inserted. Only the default is narrowed to 16 bits.

**RWE.**

- **Parse: DIFFERS in type, latent.** `parseWeaponDamageBlock` (`WeaponTdf.cpp` about 6-22)
  reads every value, the default included, as an **unsigned int**. A negative value does not
  fail: the stream wraps it (`-5` reads as 4294967291), which `getDamage` then treats as an
  enormous hit. A value with no numeric prefix throws and rejects the weapon file. The
  census has **no negative damage value** and no default above 30000 (the disintegrators),
  so nothing differs on shipped data. The original's per-name entries are signed, so a negative override is
  representable there and its effect is the damage consumer's (`BT-WEAPONS-WEAPONS-PARITY.md`
  rule 10); in RWE it would be an enormous hit.
- **Default narrowing to 16 bits: DIFFERS, latent.** A default over 65535 wraps in the
  original and not in RWE.
- **Case handling: UNCLEAR.** RWE upper-cases every key and inserts with replace
  (`parseWeaponDefinition`, about 262), so two spellings of one name collapse and the later
  wins. The original keeps both entries and the runtime lookup decides; the chapter says the
  decision is in the consumer. The census parser lower-cased its keys, so a case-variant
  duplicate inside a block was not looked for.
- **Lookup by unit name: MATCHES** (`Projectile::getDamage(unitType)`).
- **Stockpile build time from `reloadtime`: MATCHES.** The stockpile progress total is the
  weapon's `reloadtime` (`updateWeaponStockpile`, `UnitBehaviorService.cpp` about 994), and
  not a separate key.

## 8. Parsed and unused keys; keys not parsed

For completeness, the weapon block keys RWE reads against the chapter's list:

- Not read at all: `shellweapon`, `meteor` (flags); everything else in the chapter's tables
  is read.
- Read and unused by the simulation: `noradar`, `propeller`, `beamweapon`, `aimrate`,
  `holdtime`, `minbarrelangle`, `weapontype2`.
- Read and used only by the scene: the sounds, `startsmoke`, `endsmoke`, `smoketrail`,
  `smokedelay`, `rendertype`, `color`, `color2`, `model`, `shakemagnitude`,
  `shakeduration`, `duration` (laser length), the explosion arts.
- Parsed for the sim: the rest.

---

## 9. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play or capture
run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | `vtol_emg` (the Arm Brawler's gun) ignores `startvelocity`; a line-of-sight round should leave at the speed pick, not at `weaponvelocity`. Rule 4 | `LoadingScene_util.cpp` (`parseWeaponDefinition`), `sim/GameSimulation.cpp` (`createProjectileFromWeapon`) | **Yes.** Shoot a target with a Brawler and time the round; read the register's decode of `0x49C9C0` for the lifetime's speed term. One weapon. |
| 2 | The lava splash pair is never loaded; 44 of 272 maps are lava worlds. Rule 5 | `LoadingScene_util.cpp`, `game/WeaponMediaInfo.h`, `GameSimulationLoader.cpp` (about 423), `GameScene_util.cpp` | No (register section 7 and the chapter agree on the choice at load); a map screenshot confirms. Presentation. |
| 3 | `range` omitted should mean 32767, not 0. Rule 2 | `io/weapontdf/WeaponTdf.cpp` | No. Mod data only; one line. |
| 4 | `noautorange` is ignored for a line-of-sight round's life. Rule 4 | `sim/GameSimulation.cpp` (about 2447) | No. Mod data only; shipped data cannot show it. |
| 5 | Flags read as nonzero rather than the low bit; negative damage wraps; the default is not narrowed to 16 bits. Rules 3, 7 | `io/tdf/TdfBlock.cpp`, `io/weapontdf/WeaponTdf.cpp` | No. Mod data only. The security rule favours rejecting a negative damage rather than wrapping it. |
| 6 | `duration` default 1/30 and doubled for the laser draw. Rule 2 | `LoadingScene_util.cpp` (about 700, 757), `io/weapontdf/WeaponTdf.cpp` | **Yes**, a screenshot of a beam weapon with no `duration` against TA's. Presentation. |
| 7 | Keying by id (duplicate ids overwrite, lowest id wins a name). Rule 1 | `LoadingScene.cpp` weapon loading | No. Not worth porting. |

### Documented divergences this audit leaves alone

Weapons keyed by name rather than by id; a missing projectile model drawn as nothing rather
than aborting; `minbarrelangle` and `aimrate` read and ignored (the register says both do
nothing); the half-radius `areaofeffect`.

### Conflicts with existing RWE findings, in one place

1. **None between the chapter and the register on the loader.** Every numeric row, offset
   and flag position agrees with `TOTALA-EXE-DATA.md`.
2. **A stale line in the code** (not a reference conflict): `parseWeaponDefinition`'s comment
   says shipped bombs set both `ballistic` and `dropped`; the census has them setting
   `dropped` alone (`BT-WEAPONS-WEAPONS-PARITY.md` rule 2).
3. **A line the register should carry** (not a conflict): `vtol_emg` is the only shipped
   non-self-propelled weapon that names `startvelocity` and `weaponacceleration`, which makes
   it the one shipped test of the speed pick on a line-of-sight round.
