# TA demos: the `.tad` format, and what it is good for

Total Annihilation matches recorded with TA Demo Recorder are `.tad` files,
and there is an archive of several thousand of them at
[tademos.xyz](https://tademos.xyz/). This is what is in one, what playing one
back inside RWE would actually take, and the thing they are much better
suited to than playback: serving as a conformance corpus for the simulation.

## Provenance, and how much to trust this

**Everything in the format sections below is transcribed from a third-party
implementation, not read out of `TotalA.exe` and not yet checked against a
real `.tad` file.** The source is [`ta-forever/gpgnet4ta`](https://github.com/ta-forever/gpgnet4ta)
`tademo/` (MIT, so it can be lifted into this GPL-3.0 tree with attribution),
which is itself a C++ port of the original Delphi recorder -- the Swedish
comments survive the port intact, which is how you can tell.

That is a weaker footing than the rest of `docs/`, where a finding has
normally been replayed against real data before it was written down. Treat
the offsets here as a starting map rather than as settled, and expect the
first hour spent with a real demo file to correct something. The subpacket
length table in particular is community-reverse-engineered and carries its own
uncertainty markers: several entries in the shipped source are commented
`?? crash` or `Gives explosions! However, they appear in the wrong place`,
which is the authors telling you they guessed.

## The short version

A `.tad` is a capture of the DirectPlay traffic seen by **one peer**, framed
into length-prefixed records. It is a stream of *state and effects* -- unit
positions, deaths, damage, shots, build events, resource totals -- and not a
stream of orders. There is no player input in it to replay.

That single fact decides everything downstream: you cannot feed a demo into
`GameSimulation`, because there is nothing of the right kind to feed it.

## The container

Every record is prefixed with a `uint16` length **that includes the two
length bytes themselves**, so the payload is `length - 2`. The reader rejects
anything over 16384 or under 3 as a framing error, which is a useful sanity
check when hunting for the start of a truncated file.

The records appear in this order:

| Record | Contents |
|---|---|
| `Header` | `char magic[8]` = `"TA Demo\0"`; `uint16 version` at 8; `uint8 numPlayers` at 10; `uint16 maxUnits` at 11; NUL-terminated `mapName` from 13 |
| `ExtraHeader` | `uint32 numSectors`. **Version 5 only** |
| `ExtraSector` x N | `uint16 sectorType` at 0, payload from **offset 4**. Version 5 only |
| `Player` x numPlayers | `uint8 color`, `uint8 side`, `uint8 number`, then NUL-terminated `name` |
| `PlayerStatusMessage` x numPlayers | `uint8 number`, then the encrypted+compressed 0x20 lobby status packet |
| `UnitData` | one record: the `0x1a` unit-type table, uncompressed |
| `Packet` x many | `uint16 time`, `uint8 sender`, payload from offset 3 |

Versions 3, 4 and 5 are accepted; 5 is what a current recorder writes.
`side` is `0 = ARM`, `1 = CORE`, `2 = WATCH` -- watchers are ordinary entries
in the player table, so anything counting players has to filter them out.

`Player::number` is **not consistent between different players' recordings of
the same game**, which is the single most annoying property of the format and
the reason `gpgnet4ta` carries a whole `GameMonitor` layer to derive stable
army and team numbers from alliance chat.

Sector type 6 is the lobby chat, obfuscated by `XOR 42`. The other sector
types are not documented in the source and are worth a look.

The status message is `decrypt` then `decompress` then **skip 7 bytes**; the
DirectPlay id sits at offset `0x91` of what is left. That id is what `REJECT`
(`0x1b`) commands refer to, so it is needed to attribute a drop or a kick.

`Packet::time` is *milliseconds since the previous packet*, wall clock. It is
not a game clock and must not be used as one -- see the note on ticks below.

## The three transforms

Short, self-contained, and the whole of what stands between a file and its
subpackets.

**`decrypt`** -- for `i` in `3 .. size-5`, accumulate `check += b[i]` and then
`b[i] ^= i`. Bytes 0-2 are a header carrying a type and that checksum. The
reference implementation computes the check and then does not verify it,
which is a hint that at least one recorder version got it wrong.

**`decompress`** -- runs only if the type byte is `0x04`; anything else is
already plain. From index 3, read a control byte, then eight slots: a clear
bit is a literal, a set bit is a little-endian `uint16` where the top twelve
bits are a window offset `a` and the bottom four are a length, copying
`(len & 0x0f) + 2` bytes from **absolute output position `a-1`** onward, with
reads past the end of the output producing zero. `a == 0` terminates. The
first three bytes of the input are prepended to the result with the type byte
rewritten to `0x03` (uncompressed), so the output is a well-formed plain
packet.

Note that this is not a conventional LZ77 back-reference -- the offset is
absolute into the output buffer, not a distance behind the cursor. Getting
that wrong produces output that decodes for a while and then goes wrong in the
middle, which is the worst possible failure mode. Write the round-trip test
first.

**`unsmartpak`** -- undoes the tick-coalescing that the SmartPak mod applies.
It splices a two-byte filler after the type byte, decompresses if needed,
drops the three-byte header (four more on version 3), and then walks the
payload turning `0xfe` / `0xff` / `0xfd` records back into ordinary `0x2c`
packets while maintaining a running packet serial. Demos from before SmartPak
pass through unchanged.

## The subpacket table

After the transforms, a payload is a concatenation of subpackets whose
lengths come from a hardcoded table keyed on the first byte. There is no
length field for most of them: **misidentify one type and the rest of the
packet is garbage**, which is why the table matters more than it looks.

| Code | Len | Meaning |
|---|---|---|
| `0x05` | 65 | chat |
| `0x09` | 23 | build started |
| `0x0b` | 9 | damage |
| `0x0c` | 11 | death / explosion |
| `0x0d` | 36 | shot fired |
| `0x0f` | 6 | piece pose (what makes the commander's torso turn) |
| `0x12` | 5 | build finished |
| `0x16` | 17 | share resources |
| `0x19` | 3 | game speed / pause / unpause |
| `0x1a` | 14 | unit type data |
| `0x1b` | 6 | reject / take command |
| `0x20` | 192 | player status (side, map, maxunits) |
| `0x23` | 14 | alliance |
| `0x28` | 58 | resource statistics |
| `0x2c` | `s[1] \| s[2]<<8` | **unit state + movement**, the bulk of the stream |
| `0xfb` | `s[1]+3` | recorder-to-recorder data |
| `0xfc` | 5 | map position |
| `0xfd`/`0xfe`/`0xff` | var/5/1 | SmartPak, removed by `unsmartpak` |

**The tick clock lives in `0x2c`.** It carries a continuously incrementing
serial, which `GameMonitor` tracks per player as `tick`. That serial, not
`Packet::time`, is the timebase for anything measuring duration -- wall-clock
deltas are contaminated by network jitter, by the `0x19` speed setting and by
pauses, all of which a competitive game has plenty of.

## What the stream is, and why that settles the playback question

TA is not lockstep. Each machine simulates the units it owns and broadcasts
the results, which is why the packet list is full of consequences and empty
of orders. The disassembly in `TOTALA-EXE.md` says this from three
independent directions:

- A COB script call packs a type-`0x10` record -- unit id, script, two
  arguments -- and hands it to the emitter `0x451DF0`. It "is the network and
  replay echo of the call, not a second execution, and it does nothing at all
  in a local game" (§ on `AimPrimary`, around `TOTALA-EXE.md:1348`).
- The damage path is gated on `[victim+0x96][0x73] in {1,2}`, the player-type
  check that "means *this machine owns the simulation of that unit*", and the
  same check gates declaring a unit dead (`TOTALA-EXE.md:4457`).
- Paths are computed locally and replicated: `0x44F4A0` bit-serialises three
  waypoints plus a blocked flag, and RWE "is lockstep and cannot" copy it
  (`TOTALA-EXE.md:8521`).

So the original plays a demo back by *being* TA and pushing the packets in
over loopback, which is a thing RWE structurally cannot do. Three tiers of
"demo support" are actually available:

1. **Parse and analyse.** Players, sides, teams, map, chat, alliances,
   deaths, income curves, build events, result. ~600-900 lines, no engine
   coupling. This is what the archive site does.
2. **Puppet playback.** A spectator scene that loads the map, spawns units
   and drives them straight from the packet stream with the simulation
   switched off. Gated on decoding `0x2c`, which nobody has published;
   further gated on mapping demo unit ids through the `0x1a` table onto RWE
   unit types, and inherently holed by fog of war, since the recording peer
   only saw what was sent to it.
3. **Re-simulate from inferred inputs.** Not achievable, and not for want of
   effort -- see below.

## Demos as a conformance corpus

This is the part worth doing, and it is a different exercise from playback.

### Why inferring inputs is the wrong shape

The tempting version -- recover the orders from the observed behaviour, feed
them to `GameSimulation`, compare -- fails three ways, any one of them fatal:

- **The inference is under-determined exactly where it matters.** A unit
  walked somewhere and stopped. Move order, attack-move that found nothing,
  patrol leg, guard following its target, or a build order whose site was
  taken? Those are different code paths with different failure modes, and you
  would be inferring the thing under test from the output of the thing under
  test.
- **Divergence swamps the horizon.** Different pathfinder (see §87 and the
  replication note above), different target-selection tie-breaks, different
  RNG draws. Two engines part company within seconds even from a perfect
  reconstruction.
- **It tests everything at once, so it tests nothing.** A whole-game
  comparison is one enormous assertion that fails at tick 200 and does not
  say which of forty subsystems drifted first.

The method that has actually worked in this repo is the opposite of that:
transcribe one decoded routine, replay it against real data, compare tick by
tick. `vtolgeom.exe`, the ARMBRAWL gunship ring, the ARMSOLAR shade rows.

### The shape that does work: short bounded episodes

A demo is several thousand recorded runs of the reference implementation
emitting periodic ground truth about its own state. Slice it into episodes
where the input is either explicit in the stream or irrelevant to the
assertion, and where the window is short enough that divergence has not
accumulated. Then each episode is a fixture with real numbers in it.

That is aimed squarely at a hazard `CLAUDE.md` already names: fixtures that
invent plausible-looking numbers hid a bug outright until the real ARM
Thunder values went in. **A demo-derived fixture cannot invent numbers.**

Available with the tier-1 parser alone, no `0x2c` decode required, because
these subpackets are small and fixed-length and the `0x2c` serial gives a
clean tick clock:

- **Build timing.** `0x09` start to `0x12` finish for a known type built by a
  known builder -- a direct check on the nanolathe arithmetic and
  `WorkerTime` over thousands of real observations rather than one worked
  example.
- **Economy trajectories.** `0x28` carries income and stored metal and
  energy, sampled throughout; with the observed build events you have the
  input side too. A systems-level oracle for the economy model including
  stalls, which no unit test covers today.
- **Weapon events.** `0x0d` shot to `0x0b` damage or `0x0c` death gives
  time-of-flight and hit/miss with the shot as the explicit input -- straight
  at the missile motor model and the ballistics work.
- **Death and wrecks.** Death position to feature, which is the
  `TOTALA-EXE-WRECKS.md` material with real cases behind it.

Needing the `0x2c` decode, and the reason to do it: **kinematics.**
Successive positions and headings for one unit give acceleration, top speed,
turn rate and braking profile on known terrain. This is the aircraft
arrival-profile work with a recorded reference instead of a transcription.
Note it does not need the order recovered -- fit the observed velocity
profile, and drive RWE's mover toward the next observed position.

### How to build it without poisoning the test suite

`rwe_test` is hermetic: nothing in it touches the VFS or an HPI, and tests
construct `unitDefinitions` in code (`src/rwe/ai/AiBehaviour.test.cpp:75` is
the pattern). Keep it that way. Do not check in `.tad` files and do not make
a test read one.

1. An **offline extractor** -- `tad_probe`, alongside `ui_probe` and
   `solar_probe` -- reads a corpus and emits **episode JSON**: unit type, the
   real FBI values transcribed inline, the terrain slice, the observed
   trajectory or timings, and provenance (demo id plus tick range) so a
   failure can be traced back to the game it came from.
2. **Check in the episodes, not the demos.** Small, text, diffable, fast,
   hermetic, and they live beside the test that uses them under the existing
   `[Component].test.cpp` convention.
3. **The extractor does the filtering**, which is most of the real work.

### The filters, which are the real work

An episode mined from a competitive game is not a controlled experiment.
Reject an episode if any of these hold inside its window:

- an enemy unit is within weapons range of the subject, or the subject takes
  damage (`0x0b`) -- a unit under fire is not demonstrating its movement
  model;
- a `0x19` speed change or a pause falls inside the window;
- the terrain class under the subject changes, unless the terrain response is
  what is being measured;
- the subject is being built, is building, or is transported;
- the demo is not vanilla TA or Core Contingency. **Most of that archive is
  TA:Esc and other mods**, whose unit data RWE does not have; they are
  identifiable from the `0x1a` unit-type checksum table;
- the subject is not one of the **recording peer's own units**. Enemy state
  is intermittent by construction, since a demo only contains what was sent
  to that peer, so enemy trajectories have holes wherever fog of war closed.

### The hazard to design in from the start

Some conformance failures will be RWE **deliberately** differing. §88 of
`TOTALA-EXE.md` exists precisely so those do not get "corrected" back, and a
demo corpus is an efficient machine for regressing intentional decisions if
it is allowed to. Every demo-derived test needs an explicit expected-
difference annotation -- cheapest version, a test that asserts a *known*
delta with a comment pointing at the §88 entry, rather than one that asserts
equality and gets disabled the first time it is right to fail.

## What this is not

**Not fuzzing.** Fuzzing asks whether random input crashes or violates an
invariant, and the invariants are already available: the simulation never
NaNs, `GameHash` survives a save/load round trip (`sim/saveload.test.cpp`
does the single-shot version), totals stay finite, no unit leaves the map. A
fuzzer spraying random `PlayerCommand`s at a random map is worth building and
needs nothing from this work.

**Not the regression net.** That is RWE's own replays -- the roadmap item at
`docs/ROADMAP.md`, "record the `PlayerCommand` stream + seed; playback
through the same sim" -- which is cheaper now than when it was written, since
`save_util` already serialises `sim.rng` and the whole simulation
round-trips hash-validated. A replay file is a save taken at t=0 plus the
command stream, playback is a `PlayerCommandService` that pops from a file
instead of from the network, and per-tick `GameHash` comparison verifies it
for free.

Demos are the **conformance** net and replays are the **regression** net.
They catch different things and should not share machinery.

## Suggested order

1. Port the container reader and the three transforms; prove them by
   round-tripping `decompress` and by parsing a real demo end to end without
   the subpacket walker desynchronising. Correct this document where it is
   wrong.
2. Build the event oracles that need no `0x2c`: build timing, economy curves,
   weapon events. Roughly a week beyond the parser, and it is real
   conformance coverage against thousands of real games.
3. Decide on `0x2c` once the stream has been stared at. If the decode falls
   out of the binary in a day or two of probing -- pivot on `0x44F4A0`, the
   three-waypoint bit-serialiser, and on the emitter `0x451DF0` -- the
   kinematic corpus and tier-2 playback both open up. If it does not, stop;
   the event oracles already justify the parser.

## Open questions

- The `ExtraSector` types other than 6.
- Whether the `decrypt` checksum is verifiable, or genuinely wrong in some
  recorder versions.
- The `0x2c` layout: field order, bit widths, coordinate scaling, and what
  fraction of a unit's state it actually carries.
- The `0x1a` unit-type table: what the 14 bytes are, and whether the checksum
  in it can identify a mod precisely enough to filter a corpus automatically.
- Whether `0x28`'s 58 bytes carry enough to reconstruct income separately
  from expenditure, which decides how sharp the economy oracle can be.
