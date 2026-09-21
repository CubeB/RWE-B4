# What the original executable does: fog of war, radar, jamming and cloaking

Split out of [TOTALA-EXE.md](TOTALA-EXE.md) on 2026-09-21, which had reached
14,000 lines. **The section numbers are the stable identifier and do not**
**change** -- every `§n` reference in the tree, the commit log and the other
documents still resolves. This file holds §2, §17, §18, §25.

The full index across every file, and §88 (where RWE deliberately differs)
and §91 (decoded but not ported), are in [TOTALA-EXE.md](TOTALA-EXE.md).

## Index

2. [Fog of war and line of sight](#2-fog-of-war-and-line-of-sight)
17. [Jamming, stealth and cloaking](#17-jamming-stealth-and-cloaking)
18. [Where a radar contact is drawn, and where it is not](#18-where-a-radar-contact-is-drawn-and-where-it-is-not)
25. [The detection rings on the minimap](#25-the-detection-rings-on-the-minimap)

## 2. Fog of war and line of sight

### Storage

The grid is **32 world units per cell**, two heightmap samples across. Three
visible states come out of two arrays:

- a **global explored array** of `uint16` used as a bitmask, one bit per LOS
  group, set-only and never cleared;
- a **per-player visible array**, one byte per cell, used as a **reference
  count** of how many of that player's units currently see it.

`UpdateUnitLOS` at `0x4827B0` runs per unit per tick but is **incremental**: it
early-outs unless the unit changed cell — or, in terrain mode, moved its eye by
more than 5 — then subtracts its old stamp and adds the new one.

### The three sight modes

Sight range comes from FBI `SightDistance` at `unitdef+0x202`. The skirmish GUI
exposes three modes, **Permanent / True / Circular**, defaulting to **True**.

**Circular** does not ray-trace at all. It blits a hand-drawn mask sprite from
`anims/vismasks.gaf`, ten frames of radius 5 to 14 cells, so effective sight is
`clamp(SightDistance/32, 5, 14)` cells and saturates at 448 world units.

**True** walks a fixed fan of authored rays read from `gamedata/los.tdf`
(`TABLE1`..`TABLE9`, each a list of stepped `dx,dy` runs) with a running
maximum-slope horizon — shadow casting, not a per-cell retrace. Its radius is
capped at **8 cells / 256 world units**, because the table index is
`min(sight/32, numtables−1)` with `numtables = 9`.

**The quadrant replication is 90° rotations, not sign flips.** Measured on the
real `los.tdf` at radius 8: rotations cover the disc exactly, while sign flips
leave ten holes along the east–west axis. The tables are authored to tile that
way — each fan's first ray runs up the +y axis and its last stops one cell short
of +x.

### Terrain occlusion

A separate grid holds **two heights per cell**:

- `max(seaLevel, (2·max + min) / 3)` decides whether a cell is **revealed**;
- `max(seaLevel, (max + 2·min) / 3)` decides whether it **occludes**.

So it deliberately errs toward revealing. The eye is `clamp(unit Y, ≥ seaLevel+1)`
plus the model's own height — the maximum vertex Y of its 3DO, not an FBI field.
The horizon is only raised on cells the ray actually revealed, so once a ray is
blocked the horizon stops rising. Water is flat ground at its surface.

### Radar

**Radar has no grid at all.** It is a per-tick unit-versus-unit range query.
Terrain never blocks it, and it flags units rather than revealing ground. Its
reach has two halves and only one of them is the obvious one:

```
R = min(RadarDistance + 2 × floor(detector's own y), max(RadarDistance, SonarDistance))
```

The lift is the *detector's* altitude, not the contact's, and the cap is what
the sweep will even offer the test (`0x4675A3` feeding `0x47E9E4`). **No shipped
unit has more sonar than radar while having any radar at all**, so on the real
data the cap always binds and radar reaches exactly `RadarDistance` — which is
what the minimap ring draws. The two agree by arithmetic rather than by design;
a mod shipping a sonar-heavy dish would find the ring under-drawn.

Sonar is flat `SonarDistance`, and a jammer's radius is flat too: the jam
context is a single dword, so a jammer has no per-contact test at all beyond
the sweep's own.

Getting this wrong is visible from the minimap. RWE had the lift without the
cap, which put a Peeper's detection at about 1060 against a 700 ring.

### Drawing

**Marching squares over cell corners**, not per-cell blocks. An overlay grid
records which of the four cells surrounding a corner are dark, and each 32×32
screen tile picks one of 14 authored frames from `anims/fog.gaf` (`Black1..4`
and `Gray1..4`, 14 frames each). The variant is chosen as
`(cellX + cellY + scroll) & 3`, so the ragged edge is fixed in world space and
does not shimmer when the map scrolls. Fully-unexplored tiles are a solid
palette fill.

**The ragged boundary is in the artwork, not in a shader.** RWE previously
approximated it with procedural noise; it now uses the original's frames.

---

## 17. Jamming, stealth and cloaking

Three keys that change who can see or shoot whom. They are one section because
the original answers all three in one routine — the per-tick visibility pass at
`0x467440` — and because all three land in the same place in RWE: the radar
query that `GameSimulation::updateVisibility` builds and `canDetectUnit` reads.

### The field offsets

Read off the FBI parser under the pipeline rule §82 describes — a key's value is
stored *after the next key's push* — and cross-checked against the
definition-copy routine at `0x42B68F`–`0x42B6DF`, which moves the same run of
words with the same widths.

| FBI key | Offset | Pushed at | Stored at |
|---|---|---|---|
| `sightdistance` | `WORD def+0x202` | `0x42C395` | `0x42C3B3` |
| `radardistance` | `WORD def+0x204` | `0x42C3AA` | `0x42C3C9` |
| `sonardistance` | `WORD def+0x206` | `0x42C3C0` | `0x42C3DF` |
| `mincloakdistance` | `WORD def+0x208` | `0x42C531` | `0x42C557` |
| `radardistancejam` | `WORD def+0x20A` | `0x42C3D6` | `0x42C3F5` |
| `sonardistancejam` | `WORD def+0x20C` | `0x42C3EC` | `0x42C40B` |
| `init_cloaked` | `def+0x241` bit 4 | `0x42C466` | `0x42C491` |
| `stealth` | `def+0x241` bit 8 | `0x42C4D8` | `0x42C503` |
| `cloakcost` | float `def+0x1DA` | `0x42C4FE` | `0x42C516` (`fst`) |
| `cloakcostmoving` | float `def+0x1DE` | `0x42C526` | `0x42C542` (`fstp`) |

Two things fall out of that listing that the priorities note did not have.

**`cloakcostmoving` defaults to `cloakcost`.** The `fst` at `0x42C516` leaves the
parsed `cloakcost` on the FPU stack, `0x42C51C` truncates it to an integer, and
`0x42C525` pushes *that* as the default argument for the `cloakcostmoving` read.
A unit with a cost and no moving cost pays the same either way — which is the
Cloakable Fusion Reactor, and it never moves.

**`Cloakable` is not a key.** There is no such string anywhere in the binary.
`0x42CA5A`–`0x42CA96` sets `def+0x245` bit 13 from `cloakcost > 0.0` (the
constant at `0x4FD210` is zero), and that bit is what the Cloak_On mission
handler tests before it will do anything. None of the shipped FBIs writes
`Cloakable` either, so a build that honours only the key gives the button to
nothing. The neighbouring bits in that dword are `canresurrect` 11, `cancapture`
12, `candgun` 14, which settles the ambiguity §B of the priorities note left
there.

`mincloakdistance` defaults to **80** when the FBI is silent: `0x42D135` tests
the stored word for zero and `0x42D13F` writes `0x50`.

### The unit instance

| Field | Meaning | Settled by |
|---|---|---|
| `unit+0xB0` | tick before which cloaking is refused | `0x4676E0`, read `0x4017F8` |
| `unit+0x10E` bit 0 | switched on | `0x48B090` |
| `unit+0x10E` bit 2 | **cloaked** | `0x48B090` |
| `unit+0x110` bits 2–3 | movement rate band, 0 = stopped | `0x43DB2C` |
| `unit+0x110` bit 8 | radar contact | `0x467937` |
| `unit+0x110` bit 9 | sonar contact | `0x4678FF` |
| `unit+0x110` bit 10 | jammed | `0x46796D`, `0x46798D` |
| `unit+0x110` bit 11 | **cloak wanted** | `0x40308E` / `0x4030BE` |
| `unit+0x110` bit 12 | **an enemy is inside `mincloakdistance`** | `0x4676E8` |

Bits 2–3 are the move-rate band rather than anything to do with cloak: the
routine at `0x43DACF`–`0x43DB3D` writes them from the unit's speed band at the
same time as it calls the COB functions `StopMoving` (`0x50523C`), `StartMoving`
(`0x505230`), `MoveRate1` (`0x50520C`) and `MoveRate2` (`0x505218`). Non-zero
therefore means moving, which is how the cloak cost picks between its two
numbers.

### Turning cloak on

Cloak is a **mission**, not a flag the button writes. The ground mission table at
`0x4FC490` holds `Cloak_On` at record `0x4FC4F4` (handler `0x403070`, display
"Cloaking") and `Cloak_Off` at `0x4FC50D` (handler `0x4030A0`, display
"Decloaking"). Both handlers do one thing: check `def+0x245` bit 13 and set or
clear `unit+0x110` bit 11.

A fresh unit is seeded from the definition in the same routine that seeds the
standing orders — `0x485CBC`–`0x485CCF` copies `def+0x241` bit 4 (`init_cloaked`)
into `unit+0x110` bit 11.

### The drain

Inside the per-unit economy routine, `0x4017CB`–`0x401872`:

- the whole block is skipped when the owning player's type byte `player+0x73` is
  3;
- `unit+0x110` bit 11 must be set and bit 12 clear, and `unit+0xB0` must not be
  greater than the current tick (`0x4017F8`, against `[ds:0x511DE8 + 0x38A47]`);
- the cost is `cloakcostmoving` when `unit+0x110 & 0xC` is non-zero and
  `cloakcost` otherwise (`0x401806`), and it is **truncated to a whole number** by
  `0x4E43A0` at `0x401824` before it is spent;
- it is compared against the player's energy stock `player+0x8C` and, if the
  stock covers it, subtracted there and then and added to the unit's own
  energy-used accumulator `unit+0xC0`. If it does not, **nothing is taken and the
  unit simply does not cloak** — there is no partial payment and no stall;
- either way `0x48B090(unit, 4, cloaked)` writes `unit+0x10E` bit 2, which on a
  change fires COB event `0xE` / `0xF` and ORs render flag `0x10000` into every
  piece of the model.

### The visibility pass, `0x467440`

Runs per tick, and computes flags for **one** viewing player — the index it reads
from `ds:0x511DE8 + 0x2A43` at `0x46745B`. Four loops over the unit list.

**Loop 0, `0x467499`.** For every live unit: clear `unit+0x110` bit 12. Then, if
the unit is the viewer's own or an ally sharing vision, set bits 8 and 9;
otherwise clear bits 8, 9 and 10. So the radar picture is rebuilt from nothing
every tick, and a unit you own is never off it.

**Loop 1, `0x467521`.** Each of the viewer's live units that is **switched on**
(`unit+0x10E` bit 0) and names a `radardistance` or a `sonardistance` becomes a
source. It calls `0x47E890(position, max(radar, sonar) << 16, &visitor)`, which
walks a 128-world-unit cell grid over the bounding box and then does a real
circular test — `d² <= r²` in whole world units at `0x47E9E4` — before calling
the visitor. The visitor is `0x467840`, and it:

- skips units belonging to the viewer;
- **skips any unit whose definition sets `stealth`** (`0x467881`–`0x46788C`);
- sets bit 9 when the unit's `y` is at or below sea level and
  `d² <= sonardistance²`;
- sets bit 8 when `y + def+0x16E` (the model's height) is at or above sea level
  and `d² < (radardistance + 2 × the DETECTOR's unit+0x70)²`.

`unit+0x70` is the high word of the 16.16 `y` at `+0x6E`, so it is `floor(y)` in
whole world units — and it is read once per *source* at `0x467565`, before any
contact exists. The visitor never touches a contact's `+0x70`. An earlier
reading of §2 had this as the contact's altitude; it is not.

The lift is also capped, which is easy to miss because the cap is computed in
the caller. `0x46757F`–`0x467593` takes `max(RadarDistance, SonarDistance)`,
shifts it into 16.16, and passes it as the sweep radius, and `0x47E9E4` refuses
to call the visitor outside it. So the effective radius is
`min(lift, max(radar, sonar))` — see §2.

The split explains what sonar is for: a submerged unit is only ever a sonar
contact, a unit standing clear of the water only ever a radar one, and a
half-submerged one can be both.

**Where the two bits are then read**, which is not symmetric and is the whole
of why they are two bits and not one:

- **bit 9 (sonar) is read by `0x465AC0`**, the can-see predicate, at
  `0x465B38`. It is a veto lifted rather than a contact granted: with bit 9
  clear, a unit whose model top (`y + def+0x16E`) is below sea level — the byte
  at `global+0x1427F`, shifted into 16.16 — returns 0 at `0x465B50` without the
  fog grid being consulted. With bit 9 set the unit simply carries on to the
  same line-of-sight test everything else faces. So sonar does not let you
  shoot at something you cannot see; it is what stops a submarine being
  invisible to the destroyer standing over it. Every unit in the shipped data
  that can fight submarines carries sonar reaching at least as far as it can
  shoot — the shortest is `ARMROY`/`CORROY` at 305 against 330 of sight — and a
  sonar station's 1180 buys picture, not targets;
- **bit 8 (radar) is read by nothing in `0x465AC0` at all.** It reaches the
  minimap draw at `0x466DC0`, and it appends to the fallback candidate list in
  `0x40AA40` at `0x40AB3F`, which `0x40AD80` only walks behind the Targeting
  Facility gate — see §17a. A radar contact is a blip; without `ARMTARG` or
  `CORTARG` on the field it is never a target.

**Loop 2, `0x4675EC` — the jammer.** Every live unit **not owned by the viewer**
that is switched on and names a `radardistancejam` calls the same `0x47E890` with
that radius and the visitor at `0x467960`, which clears bit 8 and sets bit 10 on
everything it reaches. `sonardistancejam` does the same through `0x467980` for
bit 9. So:

- a jammer **hides units, it does not fake contacts**. There is no false-blip
  code anywhere near it;
- it hides *everything* inside the bubble, its own side included, not just its
  owner's units — the visitors do not look at who owns what;
- it must be **switched on**; both jammers in the shipped data set `onoffable=1`;
- it does **not** jam its owner's own picture, because the loop skips units owned
  by the viewing player. It does jam an ally's, which reads as a quirk of the
  original rather than a considered decision;
- it runs after the detection loop, so jamming always wins over radar;
- it touches only bits 8 and 9. A jammer does nothing at all to a unit you can
  actually see: line of sight is `unit+0x9C`, and loop 4 sets it afterwards.

**Loop 3, `0x467690` — `mincloakdistance`.** For every live **cloakable** unit
whose owner is of type 1 or 2, `0x40B0D0(owner, &position, mincloakdistance)` asks
whether any live enemy of that owner is within that distance in the map plane. If
one is, `unit+0xB0` is set to the current tick **+ 0x5A (90 ticks, three seconds)**
and `unit+0x110` bit 12 is set. Bit 12 is cleared again at the top of the next
tick's loop 0, so it is the timestamp that does the work: an enemy walking past a
cloaked Commander decloaks it, and it stays decloaked for three seconds after the
enemy leaves. `mincloakdistance` does **not** refuse to cloak in advance and it is
not a range at which anything is revealed — it is a proximity fuse on the unit's
own cloak.

### What a cloaked unit is invisible to

`unit+0x10E` bit 2 is read in exactly five places:

- `0x465AE8` — the "can this viewer see this unit" predicate. Own units pass at
  `0x465AD7`; a cloaked one returns 0 before anything else is looked at;
- `0x40729C` — the computer player's nearest-enemy search (`0x4071F0`), which
  skips cloaked units;
- `0x4390E2`, `0x45937D`, `0x459779` — drawing: the mincloak range ring, and the
  two sprite paths that shade a cloaked unit differently.

It is **not** read by the weapon target scan. Neither `0x40AD80` (gather enemies
in radius), `0x40B7B0` (choose one) nor `0x49ABB0` (can this weapon engage that
unit) looks at it.

An earlier reading of this page concluded from that that "a cloaked unit
standing inside an enemy gun's range is still shot at". **That is wrong, and it
is wrong in the direction that matters.** The scan does not need to read the
cloak bit, because a cloaked unit never reaches it: the list `0x40AD80` walks is
built by `0x40AA40`, which puts every candidate through `0x465AC0` first, and
`0x465AC0` rejects cloak at `0x465AE8` — `test BYTE PTR [ebx+0x10E],0x4`,
return 0 — before it looks at anything else. Cloak hides a unit from your eyes,
from the AI's search *and* from the turret. The three call sites above are
evidence about where the test lives, not about whether it happens.

The bit is confirmed as cloak independently at `0x401872`. The routine there
checks `unit+0x110` bit 11 (cloak wanted) and bit 12 (suppressed by
`mincloakdistance`), picks `CloakCost` `[def+0x1DA]` or `CloakCostMoving`
`[def+0x1DE]` according to `unit+0x110 & 0xC`, tries to debit the player's
energy at `[unit+0xEC]+0x8C`, and calls `0x48B090(unit, 4, paid)` — so
`unit+0x10E` bit 2 is set only when the cloak was actually paid for.

### §17a — the target-acquisition chain

Worth writing down once, because its absence is what produced the near-miss
above and nearly produced a second one:

```
0x40B7B0   weapon auto-acquire: choose a target
  └─ 0x40AD80(playerIdx, &centre, radius, out)   called at 0x40B82F, 0x40B85F
       reads [playerIdx*4 + 0x5119C0], the per-player scratch object
       loop 1 walks the vector at obj+0x5   (begin [obj+0x9], end [obj+0xD])
       loop 2 walks the vector at obj+0x15  (begin [obj+0x19], end [obj+0x1D])
                — only if loop 1 found nothing, and only if [obj+0x79] is set
  └─ 0x40AA40  rebuilds both vectors, per player, every 30 ticks
       (the maintenance entry is 0x40AD20: rebuild when
        world tick [global+0x38A47] >= [obj+0xED] + 0x1E)
       walks the global unit array [global+0x14357]..[global+0x1435B], stride 0x118
       rejects: [unit+0x110] & 0x10000000 clear (not alive), or bit 14 set
       enemy branch:
         push unit; push player; call 0x465AC0    ← 0x40AB11, the can-see gate
         test eax,eax; je skip-append             ← nothing unseen is appended
         ...then, outside that gate, [unit+0x110] bit 8 appends to list B
```

So the candidate set the weapon scan chooses from is **`0x465AC0`-filtered**,
and the "three tests only" in `0x40AD80` is the tail of a longer filter, not the
whole of one.

`0x465AC0(player, unit)` — `stdcall`, two arguments, the *second* push is the
unit — in full:

```
465ACE  if (unit+0x96 == player)            return 1     ; own unit
465AE8  if (unit+0x10E & 0x4)               return 0     ; cloaked
465AFD  def = unit+0x92
        A = unit+0x6A + def+0x15E     ; x
        B = unit+0x6E + def+0x16E     ; top of the model
        C = unit+0x72 + def+0x166     ; z
465B38  if (!(unit+0x110 & 0x200))                       ; not a sonar contact
465B4C      if (B < global+0x1427F << 16)  return 0      ; fully under water
        ; the fog grid, at the four corners of the footprint in turn
465B6A  if (lit(A,          B,          C         )) return 1
465BDB  if (lit(A+def+0x176, B,          C         )) return 1
465C73  if (lit(A+def+0x176, B-def+0x17A, C+def+0x17E)) return 1
465D19  return lit(A,        B-def+0x17A, C+def+0x17E)
```

`lit(x, y, z)` indexes the fog grid at `(x >> 5, (z − y/2) >> 5)`, the usual
cabinet skew, bounds-checked against `[player+0x80]`/`[player+0x84]`. It has
two backends chosen by `global+0x14281 & 2`: the per-player byte grid at
`[player+0x7C]` when set, and otherwise a call to `0x408090`, which reads the
shared word-bitmask grid at `[global+0x14273]` and tests the bit for the
viewing player index at `global+0x2A43`. `0x4658E0` is a sibling predicate for
a rectangle with the same two backends.

Four things follow that are worth stating plainly, because each has been got
wrong at least once:

- **there is no radar term** — `unit+0x110` bit 8 is never read here;
- **cloak is rejected second**, before anything positional;
- **the sonar bit is read**, but only to lift the underwater veto — see "The
  visibility pass" above, and §18, which had this right;
- **the footprint is probed four times**, not once, so a large unit is seen if
  any corner of its extent is lit.

Two details of the second list are easy to miss and both matter:

- **list B is the radar picture, and it is gated on the Targeting Facility.**
  `[obj+0x79]` is cleared at the top of every rebuild (`0x40AA80`) and set at
  `0x40AC06` only when the player owns a live, switched-on unit whose definition
  sets **`istargetingupgrade`** — `[unitdef+0x241]` bit 10, tested at
  `0x40ABF3`/`0x40ABF9`. With no such unit the flag stays zero and `0x40AD80`
  never walks list B at all. That is the Core Contingency Targeting Facility
  (`ARMTARG`/`CORTARG`), and it is the whole mechanism by which the original
  lets units shoot at what only radar can see;
- even with one, list B is a **fallback**: `0x40AE85`–`0x40AEA1` walks it only
  when the output vector is still empty, so anything actually visible is
  preferred over any radar contact.

`istargetingupgrade` is parsed at `0x42C5F7` (string at VA `0x503BB0`, `and
eax,1; shl eax,0xA` into `[unitdef+0x241]`, with `and ch,0xFB` clearing the bit
first). It was at one point thought to be a field the v3.1 engine parses and
never reads — a negative from searching for `test <mem>,0x400` against the
definition. That search could not have found it: the reader loads the dword
into a register first and tests `dh,0x4`. There is exactly one reader in
`.text`, and it is the one above.

### Where the ranges are drawn

`radardistancejam` and `sonardistancejam` are read by three drawing routines
besides loop 2 — `0x4392F8`/`0x439332` (the range display), `0x466FFC`/`0x46703F`
(minimap) and `0x46760D`/`0x467633` (main view) — and `mincloakdistance` by
`0x4390D2`, `0x439205` and `0x4676B9`. The jammed area and the mincloak radius
are both drawn as circles.

### The shipped data

Counted over `rev31/units`: `CloakCost` on four units — `armcom` and `corcom`
200, `armsnipe` 780, `armckfus` 450; `CloakCostMoving` on three — 1000, 1000,
1100; `mincloakdistance=40` on the same three that move. `RadarDistanceJam` on
six: 400, 420, 490, 500, 700, 730. `Stealth=1` on two, the stealth fighters.
**No `SonarDistanceJam`, no `init_cloaked` and no `Cloakable` anywhere.**

### What a cloaked unit looks like

Bit 2 of `unit+0x10E` reaches the screen in one place, and it does not touch the
model at all: it changes the routine that lays the finished unit down.

A unit is drawn by `0x459200`. Everything about it — every piece, the weapons
hanging off it, the brightness passes at `0x4BA1B0` and `0x4B96E0` — goes into a
bitmap of its own at `[this+0x10]`, in the GAF frame layout (`+0` width, `+2`
height, `+4`/`+6` offsets, `+8` the transparent index, `+0x10` the pixels). The
last thing the routine does is blit that bitmap to the screen, and it picks
between two blitters on the cloak flag:

```
459776: mov  edx,[ebp+0xc]              ; the unit
459779: test BYTE PTR [edx+0x10e],0x4   ; cloaked?
459780: jne  0x4597ba                   ;   -> 0x4B8500
459782: mov  eax,ds:0x511de8
459787: mov  cl,BYTE PTR [eax+0x14280]  ; the draw-everything-see-through toggle
45978d: test cl,cl
45978f: jne  0x4597ba
459791: ...  call 0x4B7F90              ; the ordinary blit
4597ba: ...  call 0x4B8500              ; the translucent one
```

`0x45937D` is the same test again on the routine's other path — the one taken
when `[ebp+0x14]` is set — with the same pair of calls. The arguments are
identical on both sides: same bitmap, same position, same destination. The only
difference is which blitter runs.

**The two blitters.** `0x4B7F90` ends in `0x4CBE70`, a plain masked copy —
`if (src != transparentIndex) dst = src`. `0x4B8500` ends in `0x4CBF2C`, which
takes one extra argument, `[gfx+0xC0]` where `gfx` is the graphics global
`ds:0x51FBD0` that `0x4B6220` returns, and does this instead:

```
4cbf99: mov al,[esi]        ; the unit's pixel
4cbf9b: cmp al,[ebp+0x18]   ; the transparent index
4cbf9e: je  0x4cbfae
4cbfa0: mov ebx,eax
4cbfa2: shl ebx,0x8
4cbfa5: mov al,[edi]        ; what is already on the screen
4cbfa7: add ebx,edx         ; edx = the table
4cbfa9: mov al,[eax+ebx*1]  ; table[src * 256 + dst]
4cbfac: mov [edi],al
```

`dst = table[src][dst]`: a 64 KB lookup on the pair of colours. The compressed
path, `0x4CC057`, does the same with the same table.

**The table.** `0x4BA5C0` allocates `0x10000` bytes tagged **"ALPHA TABLE"**
(`0x50A430`) and stores the pointer at `gfx+0xC0`. Its four neighbours are the
SHADE TABLE (`gfx+0xC4`, `0x2000`), the LIGHT TABLE (`gfx+0xC8`, `0x2000`), the
GRAY TABLE (`gfx+0xCC`, `0x100`) and the BLUE TABLE (`gfx+0xD0`, `0x100`).
`0x4BA772` fills the alpha one: for every pair `(i, j)` it averages the two
palette entries a channel at a time —

```
4ba7c0: mov al,BYTE PTR [ebp-0x2]   ; pal[i].r
4ba7c3: mov cl,BYTE PTR [edi-0x2]   ; pal[j].r
4ba7c6: add eax,ecx
4ba7d4: sar eax,1                   ; (r_i + r_j) / 2
```

— and calls `0x4BA9D0`, a nearest-colour search by squared RGB distance over the
256 entries, to snap the average back into the palette. `i == j` is short-cut to
`i` at `0x4BA7A7`.

So a cloaked unit is drawn **exactly halfway between itself and whatever is
behind it**. Not a stipple, not a palette ramp, not a swapped mesh: an even 50%
blend of the whole silhouette, applied once per covered pixel because the unit
was composited into a bitmap of its own before any of it reached the screen.

**Who sees it.** Nobody but the owner, and the owner always does. `0x465AE8`
returns zero for anyone else's cloaked unit before it looks at line of sight, so
such a unit never reaches the draw list — there is no partial reveal at close
range and no shimmer to catch. `mincloakdistance` does not reveal anything: it
drops the cloak, and the unit is then simply solid again. And the blit choice at
`0x459779` has no ownership test in it, because anything that has got that far
and is still cloaked is your own. Cloak is not invisibility; it is invisibility
to everyone else and see-through to you.

**The global at `[0x511DE8+0x14280]`** forces every unit through the translucent
blitter. `0x495703` cycles it 0..4 from a key handler and `0x4915BA` zeroes it
when a game starts, so it is a debug view rather than a setting.

**Nothing happens on the transition.** No sprite, no flash, no puff. What the
original does raise is a notification, and it is **not** a COB event: `0x48B173`
calls `0x47F780(unit, 0xE, 0)` when the flag comes on, `0x48B1A5` calls it with
`0xF` when it goes off. `0x47F780` is gated on the unit belonging to the local
player and looks the event up in a table of 24-byte records whose caption sits
at `[0x5086E8 + N*24]` and whose sound name sits at `[0x5086E4 + N*24]`:

| N | sound | caption |
|---|---|---|
| `0xE` | `cloak` | `Cloaked` |
| `0xF` | `uncloak` | `Visible` |

The COB calls in that same routine belong to other bits of `unit+0x10E`
entirely: `Activate`/`Deactivate` on bit 0 (`0x501280`, `0x501274`) and
`StartBuilding`/`StopBuilding` on bit 3 (`0x5050F4`, `0x505104`), all through
`0x4B0940`, which takes a **name**, not a number. There is no cloak entry, and
no shipped script has one to answer it with — between them `ARMCOM`, `CORCOM`,
`armsnipe` and `ARMCKFUS`, the only four units with a `CloakCost`, name nothing
but the ordinary set.

**And `0x10000` is not a piece render flag.** `0x48B17D`–`0x48B19C` walks
`unit+0xA2`, which is the list of **missions** attached to the unit — each node
is `+0x4` owner, `+0x8` next, `+0xC` the mission, and the mission is built at
`0x43A0C0` with the one-slot vtable `0x4FD2C8` — and calls that one virtual,
`0x438870`, which is nothing but `mission->[0x4E] |= arg`. `+0x4E` is the
mission's pending-event word; the executor reads it at `0x43B805`, and
`0x43B840` tests `0x10000` and answers it by calling `0x48A0F0(unit, 0..2)`,
which clears each of the three weapons' aim and runs the COB `TargetCleared`
(`0x508D58`). Cloaking makes a unit forget what it was aiming at. It has nothing
to do with drawing.

**The one extra thing on screen.** `0x4390D2`–`0x439106`: when the selected
unit's `mincloakdistance` is non-zero **and the unit is actually cloaked**, the
range display draws a circle of that radius around it in colour
`[0x511DE8+0xDDA]` — the distance at which an enemy will break the cloak.

### What RWE does with this

Implemented: the six FBI fields and the derived `cloakable`; `stealth` keeping a
unit off radar and sonar; the jammer clearing radar and sonar contacts inside its
radius when it is switched on and not the viewer's own; the radar/sonar split by
sea level; the `mincloakdistance` proximity fuse with its three-second tail; the
cloak drain, truncated, moving versus still, all-or-nothing, through the ordinary
consumption path; and a cloaked unit dropping out of `canSeeUnit` and
`canDetectUnit`. The CLOAK button, which was drawn and wired to nothing, now
sends a command and lights up while the order stands — it records the request
rather than the cloak, so a unit an enemy has just walked past keeps its order
and cloaks again three seconds later.

A cloaked unit now also looks like one. The renderer's own predicate asks the
same three questions in the same order as `0x465AE8` — whose it is, whether it
is cloaked, and only then whether the ground under it is lit — so someone else's
cloaked unit is not drawn, not shadowed, not clickable and not sprayed at, and
what can be shot at and what can be seen agree. Your own is drawn at the ALPHA
TABLE's even half-blend with what is behind it. And the `cloak` and `uncloak`
sounds, which had been parsed out of `sound.tdf` and preloaded all along without
ever being played, now go off on the transition for the unit's owner.

Deliberately not ported:

- **Cloak being invisible to the eye but not to a gun.** RWE's target scan runs
  through `canDetectUnit`, which is already a deliberate difference from the
  original (§9), so a cloaked unit falls out of targeting as well. Matching the
  original here would mean a cloaked Commander still being shot at by everything
  it walks past, which is not what the key is for.
- **The single-viewer visibility pass.** The original computes these flags for
  the local player only and stores them on the unit; RWE computes them per
  player, which is what a deterministic simulation with more than one human in it
  needs.
- **The `player+0x73 == 3` exemption** on the drain, and the type 1-or-2 gate on
  the `mincloakdistance` loop. Both are about the original's spectator and
  script-driven player types, which RWE does not have.
- **The half-blend is alpha, not a lookup table.** There is no 8-bit palette to
  snap an average back into, so RWE asks the blend hardware for the same
  arithmetic: `SRC_ALPHA`/`ONE_MINUS_SRC_ALPHA` at exactly `0.5`. What it does
  not get for free is the original's guarantee of one average per covered pixel,
  which falls out of compositing the unit into its own bitmap first — blending
  the model straight into the frame would average a pixel again for every
  polygon of the unit stacked over it, and the thick parts of a Commander would
  come out nearly solid. So the cloaked units are held back until every solid
  one is down, then drawn twice: once with colour writes off to lay the depth,
  once with `GL_EQUAL` and depth writes off so only the nearest fragment blends.
  Holding them to the end is also what lets what stands behind them show
  through, which the original gets from painting back to front.

Still unported from this reading:

- **The caption line.** `Cloaked` and `Visible` are what the original puts up
  beside the sound; RWE has nowhere to put a unit notification's text yet, so
  only the sound plays.
- **The mincloak ring** on a selected cloaked unit (`0x4390D2`). RWE has no
  range-circle display for anything yet — the jammer and radar rings are in the
  same position — so this waits on that rather than on anything about cloak.
- **`0x10000` clearing the unit's aim** when it cloaks. It is decoded above and
  it is simulation rather than presentation; RWE's cloaked unit keeps its
  target, which given that RWE also drops a cloaked unit out of targeting
  altogether would only matter to the cloaking unit's own gun.
- **The debug see-everything-translucent toggle** at `[0x511DE8+0x14280]`.
- **Bit 10, "jammed".** The original marks a unit a jammer has erased separately
  from one that was never seen. Nothing in RWE reads it, and nothing in the
  original appears to either beyond the flag itself.
- **The jammer's effect on an ally's radar.** RWE follows the original in
  skipping only the viewer's own jammers, so allies do jam each other; it is
  recorded here in case it ever looks like a bug.

## 18. Where a radar contact is drawn, and where it is not

### The main view never sees one

The world render does not walk the unit list. It consumes a list rebuilt once a
frame and reads it at `0x4697B7` for the Z buckets and again at `0x469C16` for
the health-bar pass; every `0x118`-stride walk inside `0x468CF0`–`0x469D1F` is
over that list. The list lives at `world+0x1435F` with its count at
`world+0x14367`, allocated at `0x485590` under the tag `"HOT UNITS"`
(`0x508BC8`).

`0x48BAE0` builds it. Per unit: skip an empty slot (`0x48BB3E`), cull against
the screen rectangle `world+0x37E27` (`0x48BBD2`–`0x48BC3B`), admit the
viewer's own unit unconditionally (`0x48BC42`), and otherwise

```
48bc56  call 0x465AC0        ; the can-see predicate
48bc5b  test eax,eax / je skip
48bc63  ...                  ; append WORD unit+0xA8
```

`0x465AC0` **never reads the radar bits**. It tests own-unit (`0x465AD4`),
cloak (`unit+0x10E` bit 2, `0x465AE8`), the sonar bit for a submerged unit
(`0x465B38`), and then the line-of-sight grid or the explored bitmask directly
(`0x465B5C`, `0x465C04`, `0x408090`). A unit whose only claim on the viewer is a
radar contact is therefore not in the list, is not drawn, and is not pickable:
the 3D cursor pick `0x48CD80` iterates the same list (`0x48CDBD`–`0x48CE97`).

There is one HUD response, and it is not in the viewport. The info panel's
"unit under cursor" branch `0x46AEE0` calls `0x465AC0` at `0x46AF3D` and on
failure falls to `0x46B6F3`, which picks the prefix `"R: "` (`0x50786C`) or
`"S: "` (`0x507870`) on `unit+0x110` bit 9 and prints `"Unidentified object"`
(`0x507858`). So hovering a blip names it as unidentified rather than showing
nothing — but the hover can only come from the minimap.

### The minimap draws one exactly like anything else

`0x466DC0`, called once a tick for the local player only (`0x465072`, guarded
at `0x46506A`) and again after a fog-mode rebuild (`0x48191F`). The gate is

```
466e64  eax = unit+0x110
466e6a  test ah,0x3          ; bit 8 or bit 9
466e6d  jne draw
466e6f  owner == viewer ? draw : skip
```

and that is the only place that reads those bits raw *for drawing*. It is not
the only reader in the binary: `0x40AA40` reads bit 8 at `0x40AB3F` —
`shr eax,0x8; test bl,al`, a shift-and-test rather than a `test ah,imm`, which
is why a sweep for the latter form missed it — to append to the fallback
candidate list that the Targeting Facility unlocks. See §17a.

The
sprite is the GAF animation `radlogo` (`world+0x147DF`, loaded by name at
`0x42990D`), frame = the owner's colour byte, blitted at `0x466ECC`–`0x466F0B`.
In the shipped `anims/FX.GAF` it is ten frames, every one **4×4 pixels**,
hotspot (1,1): a two-by-two block of the player's colour inside a ring of
palette index 89, with the corners transparent. Nothing between `0x466E4E` and
`0x467135` distinguishes a radar contact from a unit in plain sight — same
sprite, same size, same colour.

Position is `x·mmW/worldW` and `(z − y/2)·mmH/worldH` (`0x466E83`–`0x466EB7`),
the same projection the 3D view uses, so an aircraft's blip sits above its
ground position by half its altitude.

The hovered unit additionally gets `radlogohigh` (`world+0x147E3`, 6×6, hotspot
(2,2)) — a ring around the dot — and a selected unit gets its radar, sonar, jam
and weapon range rings (`0x466F42` onwards).

**The blink is not about radar.** `0x466EB9` skips the blip on alternate phases
only when `unit+0xFA` is non-zero:

```
466eb9  mov al,BYTE [unit+0xfa]
466ec1  je draw                        ; timer zero -> always drawn
466ec3  test BYTE [world+0x142f1],0x1  ; the global blink phase
466eca  je skip
```

`unit+0xFA` is a **just-damaged** timer, set to `0xF0` (240 ticks, eight
seconds) by `0x467950` from the damage applier `0x489CE0` at `0x489D8E`,
decremented per tick at `0x48ADF0` and cleared on unit init at `0x485C12`. The
phase bit `world+0x142F1` is flipped every eight ticks by `0x466580`
(`0x4665A9`–`0x4665BB`), called per tick from `0x4955E5`. So a blip flashes at
roughly two hertz for eight seconds after the unit is hit, whether it is a radar
contact or a unit you can see, and a quiet radar contact does not flash at all.

### Nothing stored says "radar only"

`0x467440` recomputes the picture once a tick for the local player alone
(`0x46745B`) and writes the result into `unit+0x110`: loop 0 clears bits 8–10
for everyone else and sets 8 and 9 for own and vision-sharing units
(`0x4674EF`, `0x4674F6`), loop 1 sets bit 9 for sonar and bit 8 for radar
(`0x4678FF`, `0x467937`), loop 2 lets jammers clear them, and **loop 4 sets bit
8 again from line of sight** (`0x46780E`).

That last one matters: **bit 8 is not "radar contact", it is "on my picture at
all"**. The difference between radar and sight is not stored anywhere — it is
recomputed on demand by calling `0x465AC0`, which is what the render list does.
The AI does the same, building two lists in `0x40AA40`, one gated on can-see
(`0x40AB11`) and one on bit 8 (`0x40AB3F`).

This also corrects §17, which says "line of sight is `unit+0x9C`". There is no
such field: loop 4 biases its cursor by `lea esi,[edi+0x74]` at `0x46770D`, so
its `[esi+0x9C]` is `unit+0x110` and its `or ebp,0x100` at `0x46780E` is setting
bit 8 of the same flags word everything else uses.

### What RWE was doing

`GameScene::renderWorldUi` drew, for every radar-only contact, a coloured
minimap dot and a white box outline in world UI space. That is an RWE
invention with nothing behind it in the original, and it is gone. RWE's minimap
already draws radar contacts through `unitIsDetectableByLocalPlayer`, which is
the right gate, and already rings the hovered one — so the minimap half was
right all along and only the main view needed taking away.

### Not ported

- **The damage flash.** RWE's minimap dots do not blink. The timer and the
  eight-tick phase are decoded above; there is no `unit+0xFA` equivalent in the
  simulation and adding one is a bigger change than the complaint asked for.
- **"R: Unidentified object".** RWE's info panel has no unknown-contact
  state.
- The minimap does **not** test cloak in the original (`0x466E4E`–`0x466ECC`
  never reads `unit+0x10E`, and the radar visitor `0x467840` skips only
  `stealth`), so a cloaked enemy inside your radar range still shows a blip
  there while being invisible in the world. That reads as an oversight rather
  than a design decision and RWE does not copy it.

## 25. The detection rings on the minimap

Sections 25 to 29 read the same part of the game -- what the interface draws
over the world and on the minimap -- and share a piece of notation. `cfg` is
the game-state block whose pointer lives at `0x511de8`: a single
`malloc(0x3924D)` allocated and zeroed at `0x41D920`, so every `cfg+` offset
below is a byte displacement into that one allocation. Unit structures are
`0x118` bytes in a flat array between `[cfg+0x14357]` and `[cfg+0x1435B]`.
World positions are 32-bit fixed point with 16 fractional bits, so `[unit+0x6A]`
is x, `+0x6E` is y and `+0x72` is z, and the *integer* parts are the words at
`+0x6C`, `+0x70` and `+0x74`.


The whole minimap - TA's own code calls it the *radar screen*; the allocation
tags are `"RADAR PICTURE"`, `"RADAR MAPPED"`, `"RADAR FINAL"`,
`"HOT RADAR UNITS"` - is redrawn each frame by `0x466DC0`. That routine walks
the entire unit array once, plots each unit's dot, and, for units that are
selected, draws its detection rings. **The rings are drawn only on the minimap.
There is no ring for `sightdistance`, `radardistance` or `sonardistance` in the
3D view at all**; the 3D view has a different and much smaller set of circles,
described at the end of this section.

### Which ranges get a ring

Four, and a fifth family for anti-missile units. Read straight off the unit
type struct:

```
466f42   mov    edx,DWORD PTR [ebx+0x110]
466f48   shr    edx,0x4
466f4b   test   dl,0x1                       ; unit is SELECTED
466f4e   je     0x46713b                     ; -> no rings at all
466f54   test   BYTE PTR [ebx+0x10e],0x1     ; unit is switched ON
466f5b   jne    0x466f70
466f5d   mov    eax,DWORD PTR [ebx+0x92]     ; unit type
466f63   test   BYTE PTR [eax+0x245],0x4     ; onoffable=1
466f6a   jne    0x46707c                     ; off + onoffable -> skip the four
466f70   mov    ecx,DWORD PTR [ebx+0x92]
466f76   mov    cx,WORD PTR [ecx+0x204]      ; radardistance
466f7d   test   cx,cx
466f80   je     0x466fb3
466f82   mov    eax,DWORD PTR [esp+0x14]     ; the colour table, cfg+0xDCB
466f88   movsx  ecx,cx
466f8b   mov    dl,BYTE PTR [eax+0xa]        ; colour = cfg[0xDCB + 0x0A]
466f8e   movsx  eax,WORD PTR [esi+0x142eb]   ; minimap width in pixels
466f95   imul   eax,ecx
466f98   push   edx                          ; colour
466f9a   idiv   DWORD PTR [esi+0x1422b]      ; / map width in world units
466fa4   push   eax                          ; radius, in minimap pixels
466fa5   push   ebp                          ; centre y
466fa6   push   edi                          ; centre x
466fa7   push   edx                          ; the minimap surface
466fa8   call   0x4c0070                     ; DrawCircle
```

The same block repeats verbatim for three more fields, then a fifth, different
one:

| Field | Type offset | Colour byte | Where |
|---|---|---|---|
| `radardistance` | `+0x204` | `cfg[0xDCB+0x0A]` = `cfg+0xDD5` | `0x466F70` |
| `sonardistance` | `+0x206` | `cfg[0xDCB+0x0A]` = `cfg+0xDD5` | `0x466FB3` |
| `radardistancejam` | `+0x20A` | `cfg[0xDCB+0x0C]` = `cfg+0xDD7` | `0x466FF6` |
| `sonardistancejam` | `+0x20C` | `cfg[0xDCB+0x0C]` = `cfg+0xDD7` | `0x467039` |
| anti-missile `coverage` | weapon `+0xE0` | `cfg[0xDCB+0x0F]` = `cfg+0xDDA` | `0x46707C` |

**`sightdistance` (`+0x202`) does not get a ring.** It is read one slot away
from `radardistance` and is conspicuously skipped. Nor does `mintime`,
`maneuverleashlength`, `builddistance` or anything else. A unit can draw up to
four rings at once - a unit with radar, sonar and both jammers would show four
concentric circles in two colours - and a plain Radar Tower shows exactly one.

Those five field offsets are confirmed twice over: the FBI parser at
`0x42C395`-`0x42C40B` stores them in that order (remembering that the parser's
store lands *after* the next key has been pushed, so `sightdistance`->`+0x202`,
`radardistance`->`+0x204`, `sonardistance`->`+0x206`,
`radardistancejam`->`+0x20A`, `sonardistancejam`->`+0x20C`), and the debug
overlay at `0x439202` independently labels the same offsets with the literal
strings `"sight"`, `"radar"`, `"sonar"`, `"radarjam"`, `"sonarjam"` at
`0x505188`, `0x505180`, `0x505178`, `0x50516C`, `0x505160`. `+0x208` sits in
the gap and is `mincloakdistance`, labelled `"mincloak"`.

### The on/off gate

`[unit+0x10E] & 1` is the unit's activated flag and `[type+0x245] & 4` is
`onoffable=1` (FBI key `onoffable` at `0x503AE4`, inserted at bit 2 of the dword
at `+0x245` by `0x42C8AE`). A radar tower the player has switched off is
`onoffable` with the activated bit clear, and the branch at `0x466F6A` jumps
past all four rings. **Switching a radar off makes its ring disappear.** The
anti-missile coverage ring is not behind that gate - the jump lands at
`0x46707C`, after the four but before the coverage block.

### The anti-missile coverage rings

```
46707c   mov    eax,DWORD PTR [ebx+0x92]
467082   mov    ecx,DWORD PTR [eax+0x241]
467088   shr    ecx,0x1d
46708b   test   cl,0x1                      ; antiweapons=1
46708e   je     0x46713b
467094   lea    edx,[ebx+0x10]              ; &unit.weapon[0], stride 0x1C
467097   mov    DWORD PTR [esp+0x20],0x3    ; three weapon slots
4670a3   mov    eax,DWORD PTR [esp+0x18]
4670a7   mov    ecx,DWORD PTR [eax]         ; the weapon definition
4670a9   mov    edx,DWORD PTR [ecx+0x111]
4670af   shr    edx,0x1e
4670b2   test   dl,0x1                      ; interceptor=1
4670b5   je     0x467121
4670b7   movsx  eax,WORD PTR [esi+0x142eb]
4670be   mov    ecx,DWORD PTR [ecx+0xe0]    ; the weapon's `coverage`
4670c4   sub    ecx,0x200                   ; minus 512
4670ca   imul   eax,ecx
4670ce   idiv   DWORD PTR [esi+0x1422b]
```

`antiweapons` is bit 29 of the dword at `[type+0x241]` (FBI key at `0x503AF8`,
inserted by `0x42C86F`); `interceptor` is bit 30 of `[weapon+0x111]` (weapon
TDF key at `0x5040D4`, inserted at `0x42EAAC`); `coverage` is the dword at
`[weapon+0xE0]` (weapon TDF key at `0x5042C8`, stored at `0x42E540`, confirmed
by the debug label `"weapon %d - coverage"` at `0x5051AC`). The `-512` is
literal and unexplained - I read it as TA shrinking the drawn circle by half a
map square so it does not overstate the guaranteed intercept area, but that is
inference.

If the byte at `[unit.weaponSlot+0xE]` is non-zero the ring goes through
`0x4C01A0` instead of `0x4C0070` - the same circle, but **dashed**: 32 segments
of which alternate ones are drawn, with the starting parity taken from
`[cfg+0x142F1] & 1`, the minimap's blink bit. The dashes therefore swap over on
each blink, giving a chase pattern. The solid form is used otherwise.

### Shape, and how the world radius becomes pixels

A **circle**, not a diamond, and emphatically not the LOS shape. TA's actual
line of sight is the octant-mirrored raster in `gamedata/LOS.TDF` (tables for
radii 1 through 12, `numlines` growing 2, 4, 4, 6, 8, 10, 12, 12, 14, 16, 18,
20); the minimap ring makes no attempt to match it. It is a plain 32-gon:

```
4c0087   mov    ebx,0x800                  ; angle step: 0x10000 / 32
4c008c   lea    ebp,[edi+eax*1]            ; first point = (cx + r, cy)
4c0095   push   edi                        ; r
4c0096   push   ebx                        ; angle
4c0097   call   0x4b7123                   ; -> r*cos
4c00a7   add    esi,ecx                    ; x = cx + r*cos
4c00a5   push   edi
4c00a6   push   ebx
4c00a9   call   0x4b70ef                   ; -> r*sin
4c00c3   add    edi,ecx                    ; y = cy + r*sin
   ... clip, then 0x4CC7AB = 1-pixel Bresenham line, colour = arg5 ...
4c017b   add    ebx,0x800
4c0183   cmp    ebx,0x10000
4c018d   jle    0x4c0091
```

So: **32 straight segments, one pixel wide, solid, closed** (the step runs from
`0x800` to `0x10000` inclusive, bringing the last vertex back to the start).
`0x4CC7AB` is a hand-written Bresenham that pokes single bytes into the
surface; there is no thickness and no antialiasing.

The radius conversion shows it is a circle in *minimap pixel* space, not world
space:

```
radiusPx = range * [cfg+0x142EB] / [cfg+0x1422B]
```

`[cfg+0x142EB]` is the minimap width in screen pixels and `[cfg+0x1422B]` the
map width in world units - the same pair used to place the unit dots
(`0x466E83`) and, in reverse, the view rectangle (`0x466B70`). Every ring uses
the **horizontal** scale, including for its vertical extent; the vertical scale
`[cfg+0x142ED]/[cfg+0x1422F]` is used for the *centre* but never the radius. On
a map whose minimap is not scaled identically in both axes the ring is
therefore slightly wrong vertically, and TA does not care.

The centre is the unit's minimap dot:

```
mx = ux          * [cfg+0x142EB] / [cfg+0x1422B]
my = (uz - uy/2) * [cfg+0x142ED] / [cfg+0x1422F]
```

with `ux`, `uy`, `uz` the integer parts at `[unit+0x6C]`, `+0x70`, `+0x74`.
Note the `z - y/2`: TA's minimap is in the same sheared space as the 3D view,
not a true plan view.

### Which units, and enemies

The loop is over *all* units in the world, and each one that is (a) drawn on
the minimap at all and (b) has `[unit+0x110] & 0x10` set gets its rings. Bit 4
of `[unit+0x110]` is the selection flag - the box-select routine at `0x48DB80`
sets and clears exactly that bit (`or edx,0x10` / `and edx,0xFFFFFFEF`) while
sweeping the unit range. So **every selected unit draws its rings, not just
one.** Ten selected radar towers give ten circles.

Enemy units are gated earlier, at `0x466E5C`, before the position is even
computed: unless the global reveal flag is on, a unit that is not the local
player's and does not have `[unit+0x110] & 0x300` (the visible / on-radar bits)
is skipped entirely. An enemy unit you can see and have clicked on would reach
the ring code, and nothing there re-checks ownership - so in principle
selecting a visible enemy radar shows its coverage. I did not confirm that TA
ever sets the selection bit on a unit you do not own; treat that last step as
unverified.

### The 3D view's own circles - a different thing entirely

`0x4390A0` does draw circles in the world, but not these. It is one of five
handlers hung off the *order overlay* pass (next section), it runs **only while
Shift is held**, and in a retail build it draws just two things:

* `mincloakdistance` (`[type+0x208]`), and only if the unit is currently
  cloaked (`[unit+0x10E] & 4`, the bit the visibility test at `0x465AC0` keys
  off). Colour `cfg+0xDDA`.
* For a `kamikaze` unit (bit 28 of `[type+0x241]`, FBI key `kamikaze` at
  `0x503A20`, inserted at `0x42CB12`), a ring whose radius sweeps outward and
  restarts every 60 ticks:

```
439137   mov    si,WORD PTR [eax+0xd6]        ; the weapon's areaofeffect
43913e   mov    eax,DWORD PTR [ecx+0x38a47]   ; global tick counter
439144   mov    ebp,0x3c
439149   div    ebp                           ; edx = tick % 60
43914b   shr    esi,1                         ; areaofeffect / 2
439157   imul   edx,esi
43915a   shl    edx,1
43915c   mul    edx                           ; \ divide by 30
43915e   shr    edx,0x5                       ; /
439161   cmp    edx,0x8                       ; floor of 8
439168   cmp    ebp,esi                       ; ceiling of areaofeffect/2
```

  i.e. `r = clamp((tick % 60) * areaofeffect / 60, 8, areaofeffect/2)` - it
  expands to the blast radius over the first thirty ticks and then sits there
  for thirty more. Followed by one static ring of `kamikazedistance`
  (`[type+0x218]`) or `sightdistance` depending on a per-unit condition.

Everything else that routine can draw - sight, radar, sonar, both jammers,
mincloak, builddistance, maneuver leash, kamikazedistance and all three weapon
ranges, each with its name printed beside it - is behind `[cfg+0x391BF]`, a
developer flag, and is unreachable in a shipped game. That is where the label
strings `"sight"/"radar"/"sonar"/"radarjam"/"sonarjam"/"mincloak"/"build
distance"/"maneuver"` at `0x505144`-`0x505190` live. They are useful as an
independent check on the field offsets and nothing else.

The world circles are drawn by `0x438EA0`, worth a note because it is *not* the
flat minimap circle: it samples the ground height under every vertex (`call
0x485070`, then `max(unitY, groundHeight)`) so the ring drapes over terrain,
and its segment count is proportional to radius -
`segments = (int)(r * 2*pi * 0.125)` from the doubles `6.28318530717958` at
`0x4FD2B0` and `0.125` at `0x4FD2B8`, i.e. one segment per eight world units of
circumference.

### The colours: what I could and could not read

Every colour above is a byte fetched at runtime from a small table at
`cfg+0xDCB`, indexed as `cfg[0xDCB + n]`. The ones in play are `n = 0x0A`
(radar and sonar), `n = 0x0C` (both jammers), `n = 0x0F` (anti-missile
coverage), plus `n = 0x01`/`0x03`/`0x09`/`0x0A` for the 3D selection brackets.

**Since resolved -- see §50.** The scan found no stores because the writer
addresses the table from a different base: `0x4AC7D0` fills it as
`cfg+0x519+0x8B2`, nearest-matching `GUIPAL.PAL` into the screen palette at
game-screen init. GUIPAL's first sixteen entries are the standard VGA text
palette, so the slots resolve exactly: `0x0A` (radar and sonar) is VGA light
green, landing on palette 233 = (83,223,79); `0x0C` (both jammers) is VGA
light red, landing on palette 211 = (255,71,0) -- the jammer rings are
orange-red, not any green; `0x0F` (anti-missile coverage) is white.

What I can say: `palettes/PALETTE.PAL` puts TA's green ramp at indices 232-239,
`232 = (123,255,119)` bright down to `239 = (11,31,0)` near black, and the
waypoint artwork in the next section is painted from `232`-`236`. A green ring
is consistent with that ramp. The cheap way to settle the exact index is to
break on `0x466FA8` in a debugger and read `edx`.

### Implementation spec - minimap rings

For each selected unit, in the minimap render:

1. Skip unless the unit is drawn on the minimap at all (own unit, or
   visible/on-radar).
2. Centre: the unit's existing minimap dot position.
3. Unless the unit is switched off while being `onoffable`, draw a circle for
   each of `radardistance`, `sonardistance`, `radardistancejam`,
   `sonardistancejam` that is non-zero. Skip `sightdistance`.
4. Radius in minimap pixels is `range * minimapWidthPx / mapWidthWorldUnits`
   for all four, using the horizontal scale even for the vertical extent.
5. Draw a closed 32-segment polygon, one pixel wide, solid. At these sizes the
   flat facets are visible top and bottom of a large ring - that is correct;
   do not substitute a smooth circle if you want to match.
6. Radar and sonar share one colour; the two jammers share a second. Pick two
   greens from the 232-239 ramp until the indices are recovered; radar/sonar
   should be the more prominent.
7. Separately, for a unit with `antiweapons=1`, draw one ring per weapon slot
   whose weapon has `interceptor=1`, radius `(coverage - 512)` scaled the same
   way, in a third colour. Dash it - 16 of 32 segments, parity flipping on the
   minimap blink - to match exactly.
8. Draw nothing in the 3D view for sight/radar/sonar. TA's only 3D circles are
   the cloaked-unit `mincloakdistance` ring and the kamikaze pulse, both gated
   on Shift being held.

---
