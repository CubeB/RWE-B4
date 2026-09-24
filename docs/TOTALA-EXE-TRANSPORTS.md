# What the original executable does: transports

Split out of [TOTALA-EXE.md](TOTALA-EXE.md) on 2026-09-21, which had reached
14,000 lines. **The section numbers are the stable identifier and do not**
**change** -- every `§n` reference in the tree, the commit log and the other
documents still resolves. This file holds §31, §32, §33, §34, §35, §36, §37, §38, §39, §40, §41.

The full index across every file, and §88 (where RWE deliberately differs)
and §91 (decoded but not ported), are in [TOTALA-EXE.md](TOTALA-EXE.md).

## Index

31. [Load eligibility -- `0x489A90`, `CanLoadUnit(transport, candidate)`, in full](#31-load-eligibility----0x489a90-canloadunittransport-candidate-in-full)
32. [Capacity and size -- the six transports, as the 3.1 exe sees them](#32-capacity-and-size----the-six-transports-as-the-31-exe-sees-them)
33. [Which mission an order produces](#33-which-mission-an-order-produces)
34. [`Ground_Pickup` -- `0x406780`, the crane flow (sea *and* hover)](#34-groundpickup----0x406780-the-crane-flow-sea-and-hover)
35. [`Ground_Unload` -- `0x406900`](#35-groundunload----0x406900)
36. [`VTOL_Pickup` (`0x4111B0`) and `VTOL_Unload` (`0x411560`)](#36-vtolpickup-0x4111b0-and-vtolunload-0x411560)
37. [The drop-legality test `0x47DB70`, and `ATTACH_UNIT` / `DROP_UNIT`](#37-the-drop-legality-test-0x47db70-and-attachunit--dropunit)
38. [The carried state, damage, and dying with the transport](#38-the-carried-state-damage-and-dying-with-the-transport)
39. [The hover transport flow -- what the Bear and Turtle actually are](#39-the-hover-transport-flow----what-the-bear-and-turtle-actually-are)
40. [Implementation spec for RWE](#40-implementation-spec-for-rwe)
41. [Loose ends](#41-loose-ends)

## 31. Load eligibility -- `0x489A90`, `CanLoadUnit(transport, candidate)`, in full

Called from every order-builder arm that can produce a pickup (`0x43F70C`,
`0x43F980`, `0x43FB3D`) and from the two cursor/default-action choosers
(`0x43E7E0`, `0x43EAAF`). This is the *only* reader of `transportcapacity`
anywhere in the binary.

```
489a90  ebx = candidate, esi = candidate def
489a9d  mov eax,[esi+0x245]; shr eax,0x13; test al,1
        -> cantbetransported set: reject
489ab2  edi = transport def
489ab8  mov eax,[edi+0x245]; test ah,0x1
        -> canload clear: reject
489acb  walk transport+0x8A (next +0x8E), count entries whose +0x86 == transport
489aec  mov cl,BYTE [edi+0x22b]          ; transportcapacity
489af2  cmp edx,ecx; jl ok               ; count >= capacity: reject
489afe  cmp DWORD [ebx],0x0              ; candidate's mover
        -> null (a building): reject
489b0b  movzx dx,BYTE [edi+0x22a]        ; transportsize
489b13  cmp WORD [esi+0x14a],dx          ; candidate footprint X
        -> footprintX > transportsize: reject
489b24  mov eax,[ebx+0x110]; and eax,3; cmp al,2
        -> airborne: reject
489b39  mov eax,[edi+0x241]; test ah,0x8 ; transport canfly?
489b42  jne 489b56                       ; an aircraft skips the next check
489b44  cmp WORD [esi+0x1c0],0x0; jl 489b56
        -> ground/hover/sea transport and candidate minwaterdepth >= 0: reject
489b56  ecx = candidate y (unit+0x6E) + candidate def+0x16E (model height)
489b71  cmp ecx, seaLevel<<16 (byte [map+0x1427F])
        -> top at or below the waterline: reject
489b7d  fld [ebx+0x104]; fcomp 1.0f      ; build fraction
        -> not exactly complete: reject
489b98  accept
```

In words, a transport may load a unit iff:

1. the candidate is not `cantbetransported`;
2. the transport has `canload=1`;
3. it currently carries **fewer than `transportcapacity` units** -- a flat
   headcount. **Nothing anywhere weighs a unit's size against capacity: a big
   unit consumes exactly one slot.** `transportsize` never touches the count;
4. the candidate is mobile (has a mover -- buildings are out). TOTALA-EXE.md's
   existing note on this routine reads `0x489AFE` as "candidate must still
   exist" and concludes "nothing asks whether it is mobile"; `unit+0x00` is
   the mover pointer (TOTALA-EXE-MISSIONS.md S:2), so this *is* the mobility
   check, and that sentence should be corrected;
5. the candidate's **footprint X** <= the transport's `transportsize` (both
   sides in footprint cells; the candidate's footprint comes from its
   movement class when it names one);
6. the candidate is not airborne;
7. **a transport that cannot fly** additionally requires the candidate's
   `minwaterdepth < 0` -- i.e. sea and hover transports refuse anything that
   *needs* water (ships, subs). Air transports skip this;
8. the candidate's top -- position y plus model height -- is **above sea
   level**. This is what excludes submerged submarines and a Triton crawling
   the seabed, for every transport type;
9. the candidate is fully built.

The existing partial decode stops at (7); (8) and (9) are new.

Note what is *not* here: no ownership or alliance test, no `floater`, no
`canhover`, no mass, no check that the candidate is an aircraft or not.

> **Corrected 2026-09-10** (§103). This used to add "(the UI only offers the
> cursor on the player's own units)", which is not true of the original: none
> of the five `CanLoadUnit` call sites applies an ownership test either, and
> the LOAD cursor arm at `0x43E7D3` will hand back `cursorpickup` over an
> enemy. The own-units rule is RWE's, and is recorded in §88 as such.

### Why the folklore comes out the way it does

With the shipped data (`transportsize=3` on all six transports):

- **Ships**: every surface ship is `BOATS4`/`BOATS5`/`BOATS6` (footprint 4-6)
  -- too big for any transport, air included; and their positive
  `minwaterdepth` locks them out of sea/hover transports regardless.
  "Air transports cannot pick up ships" is footprint, not a special case.
- **Submarines**: `BOATD3` is footprint 3, so a sub passes the size test for
  an *air* transport -- but a submerged sub fails (8). A sub whose top pokes
  above the surface in shallow water is genuinely liftable by the exe's rules.
- **Hovercraft**: `TANKHOVER3` (Anaconda, Snapper, Skimmer, Scarab...) is
  footprint 3, minwaterdepth -10000, and floats with its top above water -- so
  hovercraft on open water are loadable by air transports **and** by the
  Bear/Turtle. `TANKHOVER4` (the hover transports themselves) is footprint 4:
  too big for anything.
- All ordinary vehicles/kbots (footprint 2-3) fit everything.

---

## 32. Capacity and size -- the six transports, as the 3.1 exe sees them

Effective values under the GOG install (`rev31.gp3` overriding `ccdata.ccx`
overriding `totala1.hpi`; `transportmaxunits` is unparsed):

| Unit | Name | transportsize | transportcapacity | dead `transportmaxunits` | Effective |
|---|---|---|---|---|---|
| ARMATLAS | Atlas | 3 | **5** | (1.0 file had none) | **1** -- see below |
| CORVALK | Valkyrie | 3 | 1 | 1 | 1 |
| ARMTSHIP | Hulk | 3 | **20** | 20 | 20 |
| CORTSHIP | Envoy | 3 | **5** | 24 | **5** |
| ARMTHOVR | Bear | 3 | **5** | 6 | **5** |
| CORTHOVR | Turtle | 3 | **5** | 6 | **5** |

Three corrections to the folk numbers the task brief carried:

- **Bear and Turtle load 5, not 6.** The 6 lives in the dead key.
- **The Envoy loads 5, not 24** (the Hulk really is 20). Both `rev31` and
  `ccdata` agree; whether nerf or typo, it is what the parsed key says.
- The Atlas FBI says capacity **5**, but an air transport can never hold more
  than one: `VTOL_Pickup`'s preamble aborts the mission outright while
  anything is attached (`0x41121E`: `mov ecx,[esi+0x8a]; test ecx,ecx; jne`
  -> return 8). Capacity is only consulted at order time by `0x489A90`;
  the **1-at-a-time rule for aircraft is hard-coded in the mission**, not
  data. (Under the 1.0 exe the Atlas presumably keyed off its absent
  `transportmaxunits`; under 3.1 the FBI was patched to 5 and the hard-coded
  rule does the limiting.)

A transport with no `transportcapacity` key parses as capacity 0 and can never
be ordered to load (0 < 0 fails) -- mods beware. RWE departs from this on
purpose (§88): it reads `transportmaxunits` when `transportcapacity` is
absent, which is what an install without `rev31.gp3` needs for the Hulk,
falls back to six for a ship and one for an aircraft when neither key is
there, and warns at load in both cases.

---

## 33. Which mission an order produces

In the order-type dispatcher (jump table `0x4401EC`, same function as S:3 of
TOTALA-EXE-MISSIONS.md; `esi` = orderer's def, `edi` = target unit):

**LOAD** (`0x43F701`): requires a target unit and `0x489A90` to pass, then

```
43f719  canfly (def+0x241 bit 11) ?  "VTOL_PICKUP"  : "GROUND_PICKUP"
```

**UNLOAD** (`0x43F735`):

```
43f735  if canload && canfly && target unit is an isairbase (targetdef+0x241 bit 9)
             -> "VTOL_LANDING"           ; landing on a carrier/pad, not an unload
43f764  if !canload -> no mission
43f76f  canfly ? "VTOL_UNLOAD" : "GROUND_UNLOAD"
```

The right-click default-action arm (`0x43F962`-`0x43F99F`) does the same
CanLoad -> pickup selection, so right-clicking a friendly unit with a loaded
cursor is the same order. **Hover transports are simply the `canfly=0` branch:
Bear and Turtle run `Ground_Pickup`/`Ground_Unload`, exactly like the Hulk and
Envoy.** There is no hover-specific mission.

Ground mission table rows (base `0x4FC490`, 25-byte records):

| # | Name | Handler | Display |
|---|---|---|---|
| 14 | `BeCarried` | `0x402FC0` | "Being transported" |
| 34 | `Ground_Pickup` | `0x406780` | "Loading" |
| 35 | `Ground_Unload` | `0x406900` | "Unloading" |

VTOL table rows (from TOTALA-EXE-MISSIONS.md S:1): `VTOL_Pickup` -> `0x4111B0`
("Loading"), `VTOL_Unload` -> `0x411560` ("Unloading").

---

## 34. `Ground_Pickup` -- `0x406780`, the crane flow (sea *and* hover)

Handler args (unit, mission, flags); jump table `0x4068E8`, six states.
Preamble: no target, or `flags & 8` (target lost) -> announce "Transport
mission failed" (`0x5016D8`), return 8.

```
state 0 (0x4067b1)  transport def canload required (else return 7)
                    movzx cx,BYTE [transportdef+0x22a]   ; transportsize
                    cmp WORD [target+0x7e],cx            ; target footprintX
                    -> too big: say "Unit is too large to transport", return 8
                    announce-once "Loading unit", return 1
state 1 (0x40685d)  0x438730(unit, mission, 8): while COB busy flag
                    (unit+0x10F bit 1) is set, sleep on wake mask 8|4; clear -> advance
state 2 (0x40680f)  start COB "TransportPickup"(targetUnitId)   ; 0x4B0A70, arg = WORD [target+0xa8]
                    play sound 0xc; mission+0x36++ (attempt count)
                    wake timer 15 ticks (0x439E80), return 1
state 3 (0x40685d)  wait for the script's busy flag to clear again
state 4 (0x40686f)  if target+0x86 != 0  -> attached: return 5 (done, delete mission)
                    if mission+0x36 >= 3 -> return 9 (park: rand(30)+30 tick timer, restart)
                    else install a ground move goal at the target's position,
                    tolerance 0 (0x438930(mission; &target+0x6a, 0)),
                    wake mask = 0xE8, return 1
state 5 (0x4068b1)  cancel the goal (0x4388D0(0)), return 0 -> state 0
```

The shape of it: **try the crane first; if the script could not reach, walk at
the target and try again on arrival (or on move failure -- mask 0xE8 wakes on
either), up to three attempts per cycle.** The engine never moves the
passenger itself -- the COB script does everything (see 10), and the actual
attachment happens when the script executes `ATTACH_UNIT`. The engine merely
polls `target+0x86`.

The footprint check here and in `VTOL_Pickup` reads the *instance* copy
`unit+0x7E`, which `0x485AAA` fills from `def+0x14A` at spawn -- the same
number `0x489A90` checks.

---

## 35. `Ground_Unload` -- `0x406900`

Jump table `0x406A88`, four states. Preamble: `flags & 8` -> announce
"Unloading process is proceeding non-optimally", return 8.

```
state 0 (0x40693e)  canload required
                    0x489690(&mission+0x12, [transport+0x8a]):
                        target := current head of the passenger list
                    no passenger -> return 5 (done)
                    announce-once "Unloading"
                    packed = (mission+0x22 & 0xffff0000) | (mission+0x2A >> 16)
                             ; (intX<<16)|intZ of the ordered drop point
                    start COB "TransportDrop"(passengerId, packed)
                    mission+0x36++; wake timer 15; return 1
state 1 (0x4069de)  wait for the busy flag to clear (0x438730 mask 8)
state 2 (0x4069f0)  if passenger+0x86 != transport -> dropped: sound 0xd, return 5
                    if mission+0x36 >= 3 -> return 9
                    tol = (transportdef+0x241 bit 12, canhover)
                          ? int(WORD [transportdef+0x180] * 1.5)   ; footprintZ*16*1.5
                          : 0
                    install move goal at mission+0x22 with that tolerance,
                    wake mask 0xE8, return 1
state 3 (0x406a76)  return 0 -> restart
```

Two things worth staring at:

- **`canhover` is bit 12 of `def+0x241`** (parser `shl eax,0xc` at
  `0x42C727`, keyed by the `canhover` push at `0x42C701`) -- a new S:30 entry.
  A hover transport approaching its drop point is allowed to stop
  **1.5 x footprintZ x 16 world units short** (96 for the 4-footprint
  Bear/Turtle) instead of reaching the exact spot -- it is a hovercraft parked
  on water reaching over the beach with its boom. Sea transports and anything
  else use tolerance 0. The 1.5 lives as a double at `0x4FC958`.
- One successful drop ends the mission (return 5). Unloading a full Hulk is
  the order layer re-issuing: the `Standby` default mission (`0x405FE0`)
  re-executes the unit's current order (`0x43B700`/`0x43B1F0`) each time the
  mission list drains, and an empty transport's next `Ground_Unload` returns
  5 immediately from state 0. (How the order is finally marked consumed was
  not traced.)

The engine does **not** test drop legality here -- `DROP_UNIT` does (08).

---

## 36. `VTOL_Pickup` (`0x4111B0`) and `VTOL_Unload` (`0x411560`)

### VTOL_Pickup, jump table `0x41153C`, six states

Preamble, every tick: no target or `flags & 0x10048` -> "Transport mission
failed", return 8. Then the **submersion re-test** (`0x4111D5`-`0x4111F9`):
`targetY + targetdef+0x16E <= seaLevel<<16` -> failed, return 8 -- a target
that dives after the order is aborted mid-mission. Then `transport+0x8A != 0`
(already carrying) -> return 8.

```
state 0 (0x411241)  canfly required; footprintX vs transportsize
                    -> too big: "Unit is too heavy to transport" (the message
                       says heavy; the test is the footprint), return 8
                    announce "Loading"; weapons to mission control (0x4898B0(3));
                    if sitting on a pad (unit+0x86) detach (0x48AAC0(u,0,-1,2));
                    activate (0x48B090(1,1));
                    if the mover is landed (mover+0x2E & 3 == 1): set mode 2
                    (take off), goal at own position, altitude cruisealt/2,
                    wake mask |= 0xE0
                    return 1
state 1 (0x41132f)  goal at the target unit (0x44E190), altitude = cruisealt,
                    tolerance 0x30 (48 wu); wake mask 0x100E8; return 1
state 2 (0x41138a)  announce "Preparing for transport"; mission+0x36 = -1;
                    call COB "QueryTransport"(&mission+0x36) (0x4B0BC0)
                    -> the script returns the grab piece; wake 0x100E8; return 1
state 3 (0x4113cb)  h = targetdef+0x16E                  ; target model height
                    start COB "BeginTransport"(h)        ; script drops its hook by h (10)
                    (0x456200: broadcast the same call to remote players - display only)
                    resolve the piece's offset (0x43DEF0(unit, mission+0x36));
                    goal at the target, altitude = -(piece y-offset)
                    -> descend until the hook piece sits at the target's top;
                    wake 0x100EA; return 1
state 4 (0x411479)  if flags & 0x42 -> run "EndTransport", return 8 (aborted)
                    0x48AAC0(target, transport, mission+0x36)  ; ATTACH at the piece
                    sound 0xc; goal at own position, altitude cruisealt,
                    wake |= 0xE0; return 1                     ; climb away loaded
state 5 (0x4114fe)  return 5 - done
```

### VTOL_Unload, jump table `0x411828`, four states

Preamble: empty passenger list -> return 5.

```
state 0 (0x41159d)  canfly required; target := head of passenger list (0x489690);
                    announce "Unloading";
                    goal at mission+0x22 (the ordered point), altitude cruisealt,
                    tolerance 0x140 (320 wu); wake 0xE8; return 1
state 1 (0x411635)  drop-legality test (see below) at mission+0x22 for the
                    passenger's def; fail -> "Unable to unload unit", return 9
                    goal at mission+0x22, altitude = WORD [passengerdef+0x170]
                    (the passenger's model height - descend until the slung
                    unit touches the ground); wake 0xE8; return 1
state 2 (0x4116f2)  flags & 0x40 (move failed) -> return 9
                    re-run the same legality test; fail -> "Unable to unload
                    unit", return 9
                    run COB "EndTransport" (0x4B0940, fire and forget)
                    0x48AAC0(passenger, 0, -1, mode 1)   ; detach, landed
                    sound 0xd; goal at own position, altitude cruisealt;
                    wake 0xE0; return 1                  ; climb away
state 3 (0x4117fe)  sound 0xd; return 5
```

The legality test call, both states (`0x41163B`-`0x41168F`):

```
cellX = (mission.x - (footprintX << 19) + 0x80000) >> 20  ; centre the footprint,
cellZ = likewise                                          ; round, to cell coords
0x47DB70(passengerDef, 0, packed cells, 1)
```

---

## 37. The drop-legality test `0x47DB70`, and `ATTACH_UNIT` / `DROP_UNIT`

`0x47DB70(def, occupantTag, packedCellXZ, mode)` -- "may a unit of this
definition stand here". Mobile units (`bmcode` != 0; buildings divert to the
build-placement test `0x47D2E0`) walk every footprint cell (map cell records,
stride 13 bytes) and require, per cell:

```
47dc78  word cell+0x08 occupancy: 0xFFFF free; 0xFFFE -> resolve the building
        root cell (bytes +0xA/+0xB) and require it free; else a feature id ->
        its record's byte +0xFE bit 6 (blocking) must be clear
47dcfd  word cell+0x00 (occupying unit id stamp) must be 0 or == occupantTag
47dd10  byte cell+0x06 (low corner)  >= seaLevel - maxwaterdepth   ; not too deep
47dd24  byte cell+0x05 (high corner) <= seaLevel - minwaterdepth   ; deep enough
47dd31  slope = high - low; if slope > maxslope (def+0x228):
            legal only if the cell is underwater (low < seaLevel) AND
            slope <= maxwaterslope (def+0x229); else reject
```

All four numbers come from the movement class (with the 10000/-10000/255
defaults of 01), so:

- a **tank** (`TANKSH2`, maxwaterdepth 12) can be dropped on land or in water
  up to 12 deep, and nowhere deeper -- "tanks no";
- a **hover tank** (`TANKHOVER3`, defaults) can be dropped on any water and on
  land up to slope 12 -- "hovercraft yes";
- a **ship or sub** could be dropped only where the water is at least its
  minwaterdepth -- "ships/subs yes" -- though nothing in the shipped data can
  actually carry one;
- an **amphibian** (Triton/Crock, maxwaterdepth 100+) goes anywhere shallower
  than that.

`floater` and `canhover` are **not consulted**; the water rules are entirely
maxwaterdepth/minwaterdepth/slopes. The air path calls this test directly
(07). The ground path gets it for free: the COB `DROP_UNIT` handler
(`0x4813B0`, vtable slot +0x3C off the dispatch at `0x4B1B58`) refuses to
release a unit onto an illegal cell:

```
4813e0  unit must be alive, and attached to this unit
4813fd  0x47DB70(passengerDef, passengerId, passenger+0x76 (its current packed
        cell - i.e. wherever the boom has swung it), 1)
481419  fail -> do nothing (the unit stays attached; the mission retries)
48141b  ok   -> 0x48AAC0(passenger, 0, -1, mode 1)   ; detach, landed
```

`ATTACH_UNIT` (slot +0x38, body ending `0x4813A6`) resolves the unit, requires
it alive and either unattached or already attached **to this unit** (that is
what lets a script re-attach the same unit to a different piece), then calls
`0x48AAC0(unit, self, piece, mode)`.

### `0x48AAC0` -- attach/detach, the real state change

Validates (unit alive and not dying; **a unit that is itself carrying
something cannot be attached** -- `0x48AAE3`; carrier alive, not the unit, and
not itself carried), then broadcasts a 7-byte type-0xA network message and
applies it locally at `0x48AB70`:

```
48ac57  detach path (carrier == 0): clear +0x86/+0x8E, clear hidden bit 17,
        re-insert into the spatial grid (0x47CB40)
48ac70  attach path: unit+0x86 = carrier; push onto the head of the carrier's
        +0x8A list (LIFO); unit+0xF9 = piece;
48ac99  bit 17 of unit+0x110 (hidden) := (piece == 0xFF)
            ; attach-unit to piece -1 is how sea/hover transports hide cargo
            ; inside the hull; the Atlas attaches to a real piece, so its
            ; cargo dangles visibly
48ace1  mover mode bits (mover+0x2E & 3) := the mode argument
            ; pickup-detach passes 2 (airborne), drop-detach passes 1 (landed)
48acf3  if the unit's player is human/remote and the carrier is NOT an
        isairbase (def+0x241 bit 9): 0x4384A0(unit)
            ; flush the mission list and hand the unit "BeCarried"
            ; (pads skip this - aircraft on a carrier deck keep their orders)
```

On the attach path the unit was first removed from the spatial grid
(`0x47CB00` at `0x48AC5D`) if it was unattached.

`BeCarried` (`0x402FC0`) is two states: take all weapons under mission control
(`0x4898B0(3)` -- weapons hold), then sleep in 10-tick pokes until `unit+0x86`
clears, whereupon it returns 5 and the unit falls back to its default mission.

---

## 38. The carried state, damage, and dying with the transport

While carried, a unit is: off the spatial grid, hidden if attached to piece
-1 (the render loops at `0x459423` etc. skip `unit+0x110` bit 17), weapons
held, missions flushed to `BeCarried`. Because every weapon-target search and
splash collection runs over the spatial grid (TOTALA-EXE.md S:6/S:12), a
carried unit **cannot be hit by anything** while aboard -- the reasoned
consequence of the grid removal; no per-check "is carried" test exists or is
needed.

When the transport dies, the kill handler (the death-message path around
`0x4867B0`):

```
4867ba  if the dying unit is itself attached: detach (0x48AAC0(u,0,-1,1))
4867d0  while the passenger list is non-empty:
4867fe      0x489BB0(killer, passenger, 0x7530, deathKind, 0)
                ; 30000 damage through the normal pipeline - at or over
                ; 0x7530 armour is ignored (TOTALA-EXE.md S:6), so this
                ; kills anything the game can carry
486818      0x48AAC0(passenger, 0, -1, 1)     ; then detach it
```

So: **passengers are not damageable while carried, and all of them die when
the transport does** -- killed by 30000 armour-piercing damage, credited to
the transport's killer, then detached (their wrecks land at the death spot).

---

## 39. The hover transport flow -- what the Bear and Turtle actually are

`ARMTHOVR.COB`/`CORTHOVR.COB` are structural copies of the sea transports'
scripts (`ARMTSHIP.COB`): pieces `boom1..4`, `magnet`, `link`; functions
`BoomCalc`, `BoomExtend`, `BoomReset`, `BoomToPad`, **`TransportPickup`**,
**`TransportDrop`**. There is no `QueryTransport`/`BeginTransport`/
`EndTransport` -- nothing of the Atlas in them. Decompiled (Bear, functions 11
and 12; get-value ids 9 = UNIT_XZ, 10 = UNIT_Y, 11 = UNIT_HEIGHT,
16 = GROUND_HEIGHT, set-value 6 = BUSY):

```
TransportPickup(u):
    BoomCalc(UNIT_XZ(u), UNIT_Y(u) + UNIT_HEIGHT(u))   ; aim the crane; sets a
    if !static4: return                                ; success flag - out of
    BUSY = 1                                           ; reach fails silently
    BoomExtend()
    move link piece to y = -UNIT_HEIGHT(u); ATTACH_UNIT(u, link, 0)
    BoomToPad(); ATTACH_UNIT(u, -1, 0)                 ; swing in, hide in hull
    BoomReset(); BUSY = 0

TransportDrop(u, packedXZ):
    BoomCalc(packedXZ, GROUND_HEIGHT(packedXZ) + UNIT_HEIGHT(u))
    if !static4: return
    BUSY = 1
    BoomToPad(); move link to y = -UNIT_HEIGHT(u); ATTACH_UNIT(u, link, 0)
    BoomExtend()                                       ; swing out to the point
    DROP_UNIT(u)                                       ; engine checks 0x47DB70
    BoomReset(); BUSY = 0
```

The `BUSY` value is the engine's `unit+0x10F` bit 1 -- exactly what
`0x438730` polls between mission states. The Atlas COB, for comparison:
`QueryTransport` returns piece 1 (`link`); `BeginTransport(h)` is one
instruction, `MOVE_NOW link y -> -h`; `EndTransport` folds the arms back.

So the answer to "how do Bear/Turtle load" is: **crane-style, via
`Ground_Pickup`/`Ground_Unload` and COB `TransportPickup`/`TransportDrop`,
identically to the Hulk/Envoy** -- the only hover-specific behaviour in the
whole path is the 1.5x-footprint unload arrival tolerance of 06 (`canhover`
bit) and, of course, that a hovercraft's mover can park on water next to its
cargo. If RWE's hover transports do nothing today, the missing piece is not a
new mechanism: it is (a) hover movement getting the transport within boom
reach, and (b) the same crane flow the sea transports already run.

FBI notes: Bear/Turtle are `canload=1`, `transportsize=3`,
`transportcapacity=5`, `canhover=1`, `MovementClass=TANKHOVER4`,
`DefaultMissionType=Standby`.

---

## 40. Implementation spec for RWE

Definitions used below: `fpX(u)` = footprint X in cells (movement-class
override included); `height(u)` = model max-Y; `seaLevel` from the map;
`minWD(u)`/`maxWD(u)` = the movement parameters with defaults
**maxWD = 10000, minWD = -10000** when neither the FBI nor the class says
otherwise; `carried(t)` = number of units attached to `t`.

### Data

| Transport | capacity | size | notes |
|---|---|---|---|
| Atlas | 5 (data) -> **1 effective** | 3 | air rule below |
| Valkyrie | 1 | 3 | |
| Hulk | 20 | 3 | |
| Envoy | **5** | 3 | not 24 |
| Bear | **5** | 3 | not 6 |
| Turtle | **5** | 3 | not 6 |

Parse `canload` (the button/eligibility gate -- do not key off
`transportCapacity > 0`) and `cantbetransported`. Ignore `transportmaxunits`
everywhere.

### Load eligibility (order time and cursor), all transports

```
canLoad(t, u):
    !u.def.cantBeTransported
    && t.def.canLoad
    && carried(t) < t.def.transportCapacity      // flat headcount
    && u is mobile                                // no buildings
    && fpX(u) <= t.def.transportSize
    && u is not airborne
    && (t.def.canFly || minWD(u) < 0)             // sea+hover refuse ships/subs
    && u.position.y + height(u) > seaLevel        // nothing submerged
    && u fully built
```

No team check in the sim; RWE should keep its UI-level own-units-only rule.

### Air transports (canFly): capacity is 1, hard

Refuse to begin a pickup while anything is attached, regardless of
`transportCapacity`. Abort the pickup mission the moment the target's top
sinks to or below sea level. Flow: climb to cruisealt/2 if landed -> fly to
48 wu of the target at cruisealt -> `QueryTransport` (script returns the hook
piece) -> `BeginTransport(height(u))` (script lowers the hook by the cargo's
height) -> descend to altitude = height(u) (equivalently, -hookPieceYOffset)
-> attach at the piece -> climb to cruisealt -> done. RWE's existing Atlas
flow already approximates this; the numbers above (48 wu approach tolerance,
cruisealt/2 takeoff, descend-to-cargo-height) are the original's.

### Air unload legality (per footprint cell, footprint centred on the click)

```
cellFree                                          // no unit, no blocking feature
&& cellLowCorner  >= seaLevel - maxWD(u)          // not too deep for the cargo
&& cellHighCorner <= seaLevel - minWD(u)          // deep enough (ships/subs)
&& (slope <= maxSlope(u)
    || (cellLow < seaLevel && slope <= maxWaterSlope(u)))
```

Expressed in RWE's current fields: a unit with `maxWaterDepth` w may go where
`seaLevel - terrainHeight <= w`; a unit with `minWaterDepth` m >= 0 only where
the water is at least m deep; `floater`/`canHover` play **no part** -- a hover
tank passes because its effective maxWD is the 10000 default, so RWE must not
substitute `canHover` for that default. Test before descending and again
before releasing; on failure say "Unable to unload unit" and back off
(rand 30-60 ticks) rather than cancelling the order. Fly to within 320 wu of
the point at cruise altitude first, then descend to altitude = height(u),
detach in *landed* state, climb away. One unit per mission; re-issue while
cargo remains.

### Ground/hover transports: the crane flow

One mission shape for Hulk, Envoy, Bear, Turtle:

- **Pickup**: check size (announce "Unit is too large to transport" on
  failure); wait for the COB busy flag; run `TransportPickup(cargoId)`; wait
  busy clear; if not attached, path toward the cargo (tolerance 0 -- let the
  mover get as close as it can; wake on arrival *or* failure) and retry, three
  attempts then back off. The script's reach check (`BoomCalc`) is the real
  range gate -- the engine has none.
- **Unload**: pick the head of the passenger list; run
  `TransportDrop(cargoId, packedDropXZ)` with packed = (intX<<16)|intZ; wait
  busy; if still attached, move toward the drop point -- arrival tolerance
  `1.5 x footprintZ x 16` wu if the transport is `canHover`, else 0 -- and
  retry x3. `DROP_UNIT` itself must enforce the same per-cell legality as the
  air unload (at the cargo's current cell, its own id counting as free) and
  silently keep the unit aboard when it fails. One unit per mission; the
  order re-issues until empty.

### The carried state

On attach: remove from collision/targeting, hide iff attached to piece -1
(keep Atlas cargo visible), stop weapons, flush orders to a BeCarried idle
(unless the carrier is a repair pad), set the passenger's mover to
landed/airborne per the detach mode. On carrier death: deal each passenger
30000 armour-ignoring damage credited to the carrier's killer, then detach.
A unit carrying cargo can itself never be picked up.

---

## 41. Loose ends

- The exact mechanism that consumes an UNLOAD *order* once the transport is
  empty was not traced (the `Standby` handler `0x405FE0` re-issues the current
  order via `0x43B1F0`; an empty transport's unload mission returns 5
  immediately, so the loop terminates behaviourally either way).
- Wake-mask bit meanings are used as opaque constants above (0xE8, 0x100E8,
  0xE0; bit 0 = timer, bit 2 = set by the busy-wait, bit 6 = move failed,
  bit 16 = the clear-weapon-targets event of the framework's step 5). A full
  decode of the event word `unit+0xBA` remains open.
- `0x43DEF0` (piece offset resolution) was read only closely enough to see
  VTOL_Pickup negate the piece's y-offset into a goal altitude; the
  BeginTransport arithmetic (hook at -height, goal altitude +height) makes the
  intent unambiguous, but the routine itself was not fully decoded.
- Whether the 1.0 exe read `transportmaxunits` was not checked (no 1.0 binary
  at hand); the 3.1 exe certainly does not.

---
