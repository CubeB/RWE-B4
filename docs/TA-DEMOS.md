# TA demos: the `.tad` format, and what it is good for

Total Annihilation matches recorded with TA Demo Recorder are `.tad` files,
and there is an archive of several thousand of them at
[tademos.xyz](https://tademos.xyz/). This is what is in one, what playing one
back inside RWE would actually take, and the thing they are much better
suited to than playback: serving as a conformance corpus for the simulation.

## Provenance, and how much to trust this

**The format sections below are now checked against real files.** They were
originally transcribed from a third-party implementation --
[`ta-forever/gpgnet4ta`](https://github.com/ta-forever/gpgnet4ta), `libs/tapacket/`
(MIT, so it can be lifted into this GPL-3.0 tree with attribution; the directory
was `tademo/` when this document was first written), itself a C++ port of the
original Delphi recorder, the Swedish comments surviving the port intact, which
is how you can tell. That transcription has since been ported into
`src/rwe/io/tad/` and run over thirteen real games: **1,913,709 packets and
12,236,992 subpackets, walked end to end with nothing left unaccounted for.**

That run corrected the transcription in about a dozen places, all of them
folded into the sections below. The largest is `0x20`, which is 186 bytes in
the packet stream and not the 192 the reference's table gives; walking at 192
silently ate 91% of the alliance declarations in the corpus. See "What the
corpus corrected" at the end.

What is *not* verified: this is still a third-party format read out of a
third-party implementation, and nothing here comes from `TotalA.exe`. The
container is not something the original writes at all -- a demo is framed by a
recorder -- so there is nothing in the binary to check it against. The
subpacket payloads are TA's own wire format and could be read out of the
binary; they have not been. Where the corpus exercised something, it is now
solid; where it did not, the reference's guesses stand unexamined, and the
shipped source marks several of them `?? crash` or `Gives explosions! However,
they appear in the wrong place`, which is the authors telling you they guessed.

## Reading one

`tad_probe` parses a demo or a directory of them and prints what is inside,
including the count of anything it could not account for:

```
tad_probe --file game.ted
tad_probe --dir /path/to/corpus            # exits non-zero on any desync
tad_probe --file game.ted --dump-unknown   # explains what it could not size
```

The parser is `src/rwe/io/tad/`, in `librwe`, and `tad.test.cpp` beside it pins
the transforms. `tools/fetch-demos.py` will fetch a small corpus from
tademos.xyz -- read the warning at the top of it first, and prefer an archive
someone has sent you.

## The short version

A `.tad` is a capture of the DirectPlay traffic seen by **one peer**, framed
into length-prefixed records. (tademos.xyz serves them as `.ted`; gpgnet4ta
writes `.tad`. Same container, verified byte for byte -- `tad_probe` takes
either.) It is a stream of *state and effects* -- unit
positions, deaths, damage, shots, build events, resource totals -- and not a
stream of orders. There is no player input in it to replay.

That single fact decides everything downstream: you cannot feed a demo into
`GameSimulation`, because there is nothing of the right kind to feed it.

## The container

Every record is prefixed with a `uint16` length **that includes the two
length bytes themselves**, so the payload is `length - 2`. The reader rejects
anything over 32768 or 2 and under as a framing error, which is a useful sanity
check when hunting for the start of a truncated file.

The records appear in this order:

| Record | Contents |
|---|---|
| `Header` | `char magic[8]` = `"TA Demo\0"`; `uint16 version` at 8; `uint8 numPlayers` at 10; `uint16 maxUnits` at 11; `mapName` from 13 |
| `ExtraHeader` | `uint32 numSectors`. **Version 5 only** |
| `ExtraSector` x N | `uint32 sectorType` at 0, payload from **offset 4**. Version 5 only |
| `Player` x numPlayers | `uint8 color`, `int8 side`, `uint8 number`, then `name` |
| `PlayerStatusMessage` x numPlayers | `uint8 number`, then the encrypted+compressed 0x20 lobby status packet |
| `UnitData` | one record: the `0x1a` unit-type table, uncompressed |
| `Packet` x many | `uint16 time`, `uint8 sender`, payload from offset 3 |

The trailing strings are **delimited by the record length, and are not
necessarily NUL-terminated**. A real header record is exactly 42 bytes for a
27-character map name: 13 bytes of fixed prefix and 27 of name, with no room
left for a terminator. Reading one as a C string walks off the end of the
record.

Versions 3, 4 and 5 are accepted; 5 is what a current recorder writes, and is
all thirteen files of the corpus. `side` is `0 = ARM`, `1 = CORE`, `2 = WATCH` --
watchers are ordinary entries in the player table, so anything counting players
has to filter them out.

`Player::number` is **not consistent between different players' recordings of
the same game**, which is the single most annoying property of the format and
the reason `gpgnet4ta` carries a whole `GameMonitor` layer to derive stable
army and team numbers from alliance chat.

The sector types are named in the reference's own enum, and the earlier reading
here -- that type 6 was the lobby chat -- was wrong:

| Type | Contents |
|---|---|
| 1 | comments |
| 2 | chat |
| 3 | recorder version, e.g. `taf-2026.8.15` |
| 4 | date, e.g. `2026-09-07` |
| 5 | recorder context |
| 6 | player addresses, obfuscated by `XOR 42` |
| 7 | mod id |

The `XOR 42` belongs to 6, not to chat. Only 3, 4 and 6 appear in all thirteen
demos of the corpus and the other four in none of them -- **including 7**, so
the mod id is not the free corpus filter it looks like, at least not from a TA
Forever recorder. The archive tags each demo with its mod on the download page,
which is where `tools/fetch-demos.py` gets it from instead.

The status message is `decrypt` then `decompress` then **skip 7 bytes**; the
DirectPlay id sits at offset `0x91` of what is left. That id is what `REJECT`
(`0x1b`) commands refer to, so it is needed to attribute a drop or a kick. Every
one of the 89 status messages in the corpus decoded to exactly **192 bytes**
with a **checksum that matched** -- which answers one of the open questions
below, and which matters for a second reason: see the note on `0x20` in the
subpacket table.

`Packet::time` is *milliseconds since the previous packet*, wall clock. It is
not a game clock and must not be used as one -- see the note on ticks below.

## The three transforms

Short, self-contained, and the whole of what stands between a file and its
subpackets. Note that only one of the three is applied to a packet record:
**`decrypt` is used solely on the player status message**. The packet records
that make up the body of a demo are not encrypted.

**`decrypt(data, ofs)`** -- `data[ofs]` is a type byte and `data[ofs+1..ofs+2]`
the stored checksum. For `i` from `ofs+3` to `size-4` inclusive, accumulate
`check += b[i]` and then `b[i] ^= key`, where **`key` is a counter starting at 3
and advancing with `i`** -- equal to `i` only when `ofs` is 0. The status
message record uses `ofs = 1`, because the player number sits in front of the
packet, so the `b[i] ^= i` form this document gave before is wrong there. The
checksum sums the ciphertext, so encryption accumulates after the XOR and
decryption before it. A packet too short to hold a header gets a `0x06` byte
appended and is otherwise untouched.

The checksum **is** verifiable, and it verifies: 89 of 89 status messages
across the corpus. The reference computes it and then only warns, which had
been read here as a hint that some recorder got it wrong; there is no evidence
of that.

**`decompress(data, headerSize)`** -- runs only if the type byte is `0x04`;
anything else is already plain. `headerSize` bytes are copied through with the
type byte rewritten to `0x03` (uncompressed), so the output is a well-formed
plain packet, and the stream starts after them: a control byte, then eight
slots, where a clear bit is a literal and a set bit is a little-endian `uint16`
whose top twelve bits are an offset `a` and bottom four a length. Copy
`(len & 0x0f) + 2` bytes from **absolute output position `a + headerSize - 1`**
onward, with reads past the end of the output producing zero. `a == 0`
terminates, and is tested before `headerSize` is added.

`headerSize` is a parameter and is **not always 3**: the status message uses 3,
a demo packet record uses **1**. Getting that wrong shifts every back-reference
in the file.

Two things about the copy. It is not a conventional LZ77 back-reference -- the
offset is absolute into the output buffer, not a distance behind the cursor.
And it is deliberately self-referential: the output grows as it is read, so a
source that catches up with the cursor repeats, which is how a run is encoded.
Getting either wrong produces output that decodes for a while and then goes
wrong in the middle, which is the worst possible failure mode. Write the
round-trip test first -- `tad.test.cpp` does, against a ported `compress`, with
a rapidcheck property over dictionary-assembled payloads because random bytes
produce nothing but literals and prove very little.

**`unsmartpak`** -- undoes the tick-coalescing that the SmartPak mod applies.
It decompresses if needed (with `headerSize = 1` for a demo packet), skips
**one** byte -- the type code -- plus two more if the payload carries a wire
checksum and four more if it carries a timestamp, and then walks the payload
turning `0xfe` / `0xff` / `0xfd` records back into ordinary `0x2c` packets while
maintaining a running packet serial. A demo packet record carries neither
checksum nor timestamp except on version 3, which has the timestamp. Demos from
before SmartPak pass through unchanged.

(The two-byte filler this document described belongs to the reference's slow
path, which splices `"xx"` in so it can reuse the three-byte-header
decompressor. The fast path passes `headerSize = 1` instead and splices
nothing. Either is fine; mixing them is not.)

## The subpacket table

After the transforms, a payload is a concatenation of subpackets whose
lengths come from a hardcoded table keyed on the first byte. There is no
length field for most of them: **misidentify one type and the rest of the
packet is garbage**, which is why the table matters more than it looks. It is
also the one part of this document that a corpus run can actually falsify, and
it did.

Counts are from the thirteen-game corpus, 12,236,992 subpackets. A blank count means
the code did not appear; those lengths are the reference's and are unverified.

| Code | Len | Seen | Meaning |
|---|---|---|---|
| `0x00` | run of zeros | | padding |
| `0x02` | 13 | | ping |
| `0x03` | 7 | | ? |
| `0x05` | 65 | 3,906 | chat (see below) |
| `0x06` | 1 | 20,511 | encryption pad |
| `0x07` | 1 | 30,577 | ? |
| `0x08` | 1 | | loading started |
| `0x09` | 23 | 66,079 | build started |
| `0x0a` | 7 | 94,445 | ? |
| `0x0b` | 9 | 824,844 | damage |
| `0x0c` | 11 | 60,075 | death / explosion |
| `0x0d` | 36 | 631,578 | shot fired |
| `0x0e` | 14 | 10 | area of effect |
| `0x0f` | 6 | 229,523 | feature action |
| `0x10` | 22 | 1,729,546 | unit script call |
| `0x11` | 4 | 265,734 | unit state |
| `0x12` | 5 | 105,617 | build finished |
| `0x13` | **unknown** | | play sound |
| `0x14` | 24 | 548 | give unit |
| `0x15` | 1 | 17,311 | start |
| `0x16` | 17 | 56,442 | share resources |
| `0x17` | 2 | | ? |
| `0x18` | 2 | | host migration |
| `0x19` | 3 | 710 | game speed / pause / unpause |
| `0x1a` | 14 | | unit type data |
| `0x1b` | 6 | 222 | reject / take command |
| `0x1e` | 2 | 48 | start |
| `0x1f` | 5 | 74 | ? |
| `0x20` | **186** | 1,123 | player info (see below) |
| `0x21` | 10 | | ? |
| `0x22` | 6 | | ident |
| `0x23` | 14 | 5,638 | alliance |
| `0x24` | 6 | 1,123 | team |
| `0x26` | 41 | | ident |
| `0x28` | 58 | 469,842 | resource statistics |
| `0x29` | 3 | 726 | ? |
| `0x2a` | 2 | 19,728 | loading progress |
| `0x2c` | `s[1] \| s[2]<<8` | 7,422,196 | **unit state + movement**, the bulk of the stream |
| `0x2e` | 9 | | ? |
| `0x42` | `s[1] \| s[2]<<8` + 3 | | Thaldren extension |
| `0xf6` | 1 | | ? |
| `0xf9` | 73 | 2,873 | ally chat |
| `0xfa` | 1 | | replay server |
| `0xfb` | `s[1]` + 3 | 5,343 | recorder-to-recorder data |
| `0xfc` | 5 | 170,520 | map position |
| `0xfd` | (`s[1] \| s[2]<<8`) **- 4** | | SmartPak, removed by `unsmartpak` |
| `0xfe` | 5 | | SmartPak, removed by `unsmartpak` |
| `0xff` | 1 | | SmartPak, removed by `unsmartpak` |

**`0x20` is 186 bytes, not 192.** This is the correction that matters, and it
is the one the corpus found. Walking at 192 over-runs by six bytes into
whatever follows, which in practice is the run of `0x23` alliance declarations
sent at game start -- so the walker lands mid-record and hands back the rest of
the packet as one unrecognisable blob. Assuming 192 loses **5,134 of the 5,638
alliance records and 1,051 of the 1,123 team records**, and it was the sole
cause of every desync in the corpus: 533 of them, across twelve of the thirteen
games.

The reference's own code half-knows this. Its `TPlayerInfo` constructor has a
branch for "mysteriously short-sized PlayerInfo packets" which decodes exactly
186 bytes -- but the branch was never propagated to `getExpectedSubPacketSize`,
which still says 192. And 192 is not wrong everywhere: it is exactly the length
of the lobby status packet in the `PlayerStatusMessage` record, all 89 of which
measured 192 in the corpus. So the reference has the long form's length in the
table used to walk the short form. Both numbers are real; they belong to
different places.

**`0x13` has no length.** The reference names `PLAY_SOUND_13` in its enum and
then omits it from the size switch, so it falls through to zero. That hole is
left open here rather than guessed at -- `tad_probe` reports an unsized code
loudly, and `0x13` did not appear once in 12.2 million subpackets, so there is
nothing yet to calibrate a guess against.

**`0x05` is not simply 65.** If byte 64 is not a NUL, an older recorder has
emitted more text than it should -- as a single packet, so the whole remaining
buffer is the message. If map position reporting is on, the last five bytes of
that are a `0xfc` belonging to the next subpacket and have to be given back.

**The tick clock lives in `0x2c`.** It carries a continuously incrementing
serial in bytes 3-6, which `GameMonitor` tracks per player as `tick`. That
serial, not `Packet::time`, is the timebase for anything measuring duration --
wall-clock deltas are contaminated by network jitter, by the `0x19` speed
setting and by pauses, all of which a competitive game has plenty of. It holds
up: across the corpus the serial never once went backwards for a sender, and a
game's tick span (153,789 ticks over 5,079 wall-clock seconds for the longest)
is consistent with the 30 Hz simulation once pauses are allowed for.

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
  TA:Esc and other mods**, whose unit data RWE does not have. This is harder
  than it looked: `ExtraSector` type 7 is a mod id, but no demo in the corpus
  carried one, so the filter has to come from the `0x1a` unit-type checksum
  table in the `UnitData` record (still undecoded, see the open questions) or
  from the archive's own per-demo mod tag, which `tools/fetch-demos.py` records
  in a sidecar at fetch time. Do not trust the map name: the bracketed prefixes
  in the corpus (`[Diox]`, `[L]`, `[Metal]`, `[Bully]`, `[Pro]`) are map-pack markers,
  not mod markers;
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

1. ~~Port the container reader and the three transforms; prove them by
   round-tripping `decompress` and by parsing a real demo end to end without
   the subpacket walker desynchronising. Correct this document where it is
   wrong.~~ **Done.** `src/rwe/io/tad/` in `librwe`, `tad.test.cpp` beside it,
   `tad_probe` on top, and the corrections are folded in above. Thirteen games,
   1,913,709 packets, 12,236,992 subpackets, nothing unaccounted for. The one
   real finding was `0x20`'s length.
2. Build the event oracles that need no `0x2c`: build timing, economy curves,
   weapon events. Roughly a week beyond the parser, and it is real
   conformance coverage against thousands of real games. The counts in the
   table above say what a corpus this size gives you to work with: 66,079
   build starts and 105,617 build finishes, 469,842 resource samples, 631,578
   shots against 824,844 damage events and 60,075 deaths.
3. Decide on `0x2c` once the stream has been stared at. If the decode falls
   out of the binary in a day or two of probing -- pivot on `0x44F4A0`, the
   three-waypoint bit-serialiser, and on the emitter `0x451DF0` -- the
   kinematic corpus and tier-2 playback both open up. If it does not, stop;
   the event oracles already justify the parser. Note it is 60% of the stream
   by count and rather more by volume, so this is where the information is.

## Open questions

- The `0x2c` layout: field order, bit widths, coordinate scaling, and what
  fraction of a unit's state it actually carries. Bytes 1-2 are its length and
  3-6 its serial; the rest is unread.
- What `0x10` carries. It is the second most common code in the corpus at
  1,729,546 -- more frequent than damage, deaths and shots put together -- and
  the reference sizes it at 22 bytes and calls it a script call. `TOTALA-EXE.md`
  §on `AimPrimary` decodes the emitter side of exactly this record (unit id,
  script, two arguments, packed by `0x451DF0`), so this one is probably cheap.
- What `0x07` (30,577) and `0x0a` (94,445) are. Both are common enough to be
  something ordinary and both are `UNK_` in the reference.
- The length of `0x13`, which the reference never had, and which nothing in the
  corpus exercises.
- The `0x1a` unit-type table: what the 14 bytes are, and whether the checksum
  in it can identify a mod. It is carried once per demo in its own record and
  never appeared in the packet stream, so it has to be read out of that record.
  It matters more now than it did: `ExtraSector` type 7 (mod id) turned out to
  be absent from every demo in the corpus, so it is not the free filter it
  looked like.
- Whether `0x28`'s 58 bytes carry enough to reconstruct income separately
  from expenditure, which decides how sharp the economy oracle can be. The
  recorder's own notes name the fields it packs -- `lastshared` metal and
  energy, `shared` metal and energy, `income` metal and energy, `lasttotal`
  metal and energy -- so there is income in there; the question is the layout
  and whether expenditure can be recovered by difference.

Answered since this document was written: the `ExtraSector` types (all seven
named, in the container section), and whether the `decrypt` checksum is
verifiable (it is, and it verified 89 times out of 89).
