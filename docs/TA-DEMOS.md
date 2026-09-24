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

## Writing one

RWE's counterpart on the output side is a demo recorder: an observer attached
to a game RWE is already simulating, framing the DirectPlay traffic each
player's TA peer would have sent. It is a wire tap of every sender and not a
recording of one seat, because that is what a real capture contains -- the
settle fan-out alone is the corpus's `numPlayers - 1` identical `0x28` copies.
A replay is a different thing and stays RWE's own: seed plus a command stream,
which only RWE can play. `rwe --map "Coast To Coast" --record-demo <file>`
records a live game, `ai_arena --record-demo <file>` records a headless
measurement run, and `rwe --replay <file> --record-demo <out>` re-runs a
recording through the real simulation, which is how the writer is debugged.
The file goes exactly where it is named, with no folder of its own: a bare
replay name lands in the Replays folder because the viewer lists it, and
nothing lists demos.

The target is the first two rungs of the fidelity ladder. **L1** is parseable:
`tad_probe --file` walks it clean, the status checksums verify and nothing is
unknown. **L2** is mineable: the four oracles score RWE's output against the
same TA-derived models they score the corpus with. **L3** -- every subpacket a
real recording of that game would contain, and a TA client able to load it and
watch -- is roadmap.

What is written faithfully:

- **The container and the transforms.** Header, version-5 extra sectors
  (recorder version, date, obfuscated player addresses), player records,
  status messages, the `0x1a` record, packet records -- uncompressed, type
  `0x03`; the status-message compressor's three-byte header cannot be reused
  where a record's header is one byte.
- **The ordering.** Per sender per tick: the unit pass's `0x09`/`0x12`/`0x0d`,
  then the `0x2c`, then the projectile pass's `0x0b`/`0x0c`, then the settle's
  `0x28`. The oracles read a tick off this order.
- **Identity.** Ids partition into blocks of `maxUnits` (a recorder setting,
  default 1000) and are allocated and recycled within a block, TA's arithmetic.
- **Time.** `Packet::time` is tick-derived, so a given game writes the same
  bytes; a pause is a `0x19` record and not a gap in the file.
- **The decoded payloads.** `0x09`, `0x0b`, `0x0c`, `0x0d`, `0x12`, `0x28` and
  the `0x2c` bit streams, as read out below.

What is a recorded divergence (ADR-0001):

- **The `0x1a` ids (D7).** The `sub` 3 block's count is the data set's unit
  count and the fixed pseudo-entry is emitted, but the ids are deterministic
  synthetic values: the content-derived id has not been reproduced, and two
  passes have failed at it. The tools use the table's count and not its ids,
  and `--units` naming does not read it.
- **The status-message body (D8).** Only the DirectPlay id at offset `0x91` is
  decoded, so the other bytes are zero. The message encrypt+compresses to a
  matching checksum, which is what lets `tad_probe` verify it rather than
  merely walk past it. Fields beyond the id are closeout work.
- **`0x10` (D9).** Omitted until the `0x456200` call-site inventory is done. A
  real stream is 14% `0x10` by subpacket count, so a written demo is smaller
  than a real one of the same game; no L2 oracle reads it.

Recording never reaches back into the simulation: nothing the recorder holds
is hashed or saved, and no wall-clock or frame-rate value crosses the divide.

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
| `0x2c` | `s[1] \| s[2]<<8` | 7,422,196 | **unit state**: a bit stream of path and goal updates plus one unit's full state per tick (see below) |
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

## What the event subpackets carry

The table above is lengths. This is contents: the payloads of the handful of
codes an event oracle is built from, decoded in `src/rwe/io/tad/tad_events.{h,cpp}`
with a test beside it whose every fixture is a real subpacket lifted out of a
real recording.

None of it comes from the reference implementation, which has no decoder for
these payloads either, and none of it comes from `TotalA.exe`. It was read off
the corpus, so each struct in that header says what the evidence was. Fields that
did not resolve are called `unknown` rather than guessed at, and there are two of
those.

**Positions are 16.16 fixed point** -- an integer part and a 16-bit fraction, in
world units, y up -- and **rotations are three 16-bit angles** on TA's usual
scale where 65536 is a full turn, the same one the COB machine uses. Both are
kept raw rather than converted on the way in: a 16.16 value has up to 32
significant bits and a `float` has 24.

### `0x09`, build started -- all 23 bytes

| Offset | Size | Field |
|---|---|---|
| 1 | u16 | which unit type, as a dense index -- but not into the `0x1a` table, see below |
| 3 | u16 | **the new unit's id**, not the builder's |
| 5 | 3 x s32 | position of the nanoframe, 16.16 |
| 17 | 3 x s16 | its rotation |

**The type index is a 1-based load-order index**, and not an index into the
`0x1a` table. The table reading is the one this work started from, and the
corpus refutes it: the table is sorted by a content-derived id, so its order is
effectively random with respect to which side a unit belongs to -- and these
indices are not. In demo 14724 the ARM player's 19 distinct indices all fall in
4..152 and the CORE player's 27 in 159..310; in 14733 the split is 7..258
against 268..525; and in the ten-player 14727 the blocks fall into a low group
and a high group the same way. A random permutation produces the two-player case
with probability about 4e-13.

It is the index TA's FBI loader assigns each unit type and stores at
`record+0x21e` (`TOTALA-EXE.md` §109). The order is:

> Take every `units\*.FBI` name the VFS presents once the archives are merged
> into one directory, sort it, and number from **one**.

`tadUnitLoadOrder` in `src/rwe/io/tad/tad_events.h` is that rule, and
`tad_episodes --units <dir>` applies it. The mod files stay out of the
repository: `--units` takes a path.

Three things had to be got right at once, which is why plain alphabetical
"scores better than chance but is not it" was as far as the previous pass got.
The sort is **global across archives**, not per-archive concatenation; the
numbering starts at **one**; and it is a **byte** sort, which matters because
Escalation ships `ALL_L2.FBI` and `_` sorts after `Z`.

#### How it was settled

The validator needs no absolute calibration: for one builder,
`buildDurationTicks / BuildTime` must be the same for every type it builds,
because the only other term is that builder's own `WorkerTime`. Score a
candidate by the mean coefficient of variation of that ratio across builders
that built two or more types; zero is perfect.

| Ordering | ProTA 4.8 (317 types) | TA: Escalation 10.2 (549 types) |
|---|---|---|
| **sorted, numbered from 1** | **0.171** | **0.359** |
| sorted, numbered from 0 | 0.494 | 0.927 |
| every other offset in -8..+8 | worse than 0.5 | worse than 0.83 |
| best of the 5040 per-archive concatenations | n/a (one archive) | 0.773 |

The minimum is singular in both, and the second data set is the real test: a
different mod, a different unit count, and seven archives instead of one.

Then the checks against fields the ordering was **not** fitted to, which is what
keeps this out of the circularity the conformance work has to avoid:

- **Side.** Every one of demo 14724's 30 type indices lands on the side its
  owner's header entry declares. 30 of 30.
- **Index 0 never appears** anywhere in the corpus -- 13 demos, ~42,000
  episodes. That is what a 1-based index looks like.
- **The builder's own `WorkerTime`.** Where a builder was itself built, its type
  is known independently, so its FBI predicts the rate it should build at.
  Taking the modal duration of each builder-type/product-type pair, 63 of 439
  Escalation pairs match `ceil(BuildTime / (WorkerTime/30))` to within two
  ticks, against **0 of 344** for the zero-based sort. `ARMAAP` (`WorkerTime`
  300) building `ARMPNIX` (`BuildTime` 30120) predicts 3012 ticks and the demos
  show a mode of exactly 3012 over 93 builds; `ARMVP` building `ARMFLASH`
  predicts 419 and shows 419 over 372. The rest are shortened by assists, which
  only ever shorten.
- **The pairs read correctly.** Aircraft plants build aircraft, kbot labs build
  kbots, construction vehicles build wind generators and metal extractors. The
  arithmetic knew nothing about unit classes.

#### What that comparison turned up: build completion is a float32 fraction

Scoring the mapping needed a model of how long a build *should* take, and the
obvious one -- `ceil(BuildTime / p)` ticks, where `p = WorkerTime / 30` with
integer division -- came out a tick short across the board. That is now settled,
and the answer is not a fudge factor: **TA's completion test is single-precision
floating point**, and the corpus can see the rounding.

[TOTALA-EXE.md](TOTALA-EXE.md) §23 has the routine. `unit+0x104` is the target's
remaining build fraction; `0x41BACD` divides the builder's `amount` by the
target's `BuildTime` and subtracts that from it each tick, counting **down** from
1.0 to 0.0, and zero means finished. So the tick count is not a division at all,
it is however many subtractions it takes to get there:

```
x    = (float)(WorkerTime / 30) / (float)BuildTime     // note the integer /30
frac = 1.0f;  every tick  frac = clamp(frac - x, 0, 1)
done when frac reaches 0
```

Three things follow, and the corpus shows all three.

**The first increment lands on the tick the `0x09` is emitted.** An episode's
`finishTick - startTick` is therefore one *less* than the number of increments,
which is the whole of the missing -1. It is not the extractor's: the tick clock
was the suspect and it is clean. A packet coalesces about seven ticks under
SmartPak and both event kinds land uniformly across that span rather than
bunching at either end, and over 42,926 episodes the `0x09` and its `0x12` never
once came from different senders, so the two ticks are always read off the same
clock.

**Where `BuildTime` is not a multiple of `p`, the float model and
`ceil(BuildTime/p)` agree**, and the corpus agrees with both: 29 of 29 such
pairs land on the predicted duration exactly, `ARMVP` to `ARMFAV` over 363
builds, `ARMLAB` to `ARMJETH` over 312, and so on.

**Where it *is* a multiple they disagree, and that is the test.** An integer
model says the job takes exactly `BuildTime/p` ticks. The float one says it
depends on whether repeated addition of `x` overshoots 1.0f or lands on it, and
that is not something a human can guess from the two numbers. Over the 16 such
pairs in the Escalation corpus it predicts every one, including which ten take
an extra tick and which six do not:

| Builder | Product | BuildTime | p | BuildTime/p | model | corpus | n |
|---|---|---|---|---|---|---|---|
| `ARMVP` | `ARMFLASH` | 1676 | 4 | 419 | +1 | +1 | 249 |
| `ARMVP` | `ARMLART` | 2840 | 4 | 710 | +0 | +0 | 74 |
| `ARMLAB` | `ARMVADER` | 6320 | 4 | 1580 | +1 | +1 | 65 |
| `CORVP` | `CORRAID` | 3564 | 4 | 891 | +0 | +0 | 57 |
| `CORAP` | `CORVENG` | 7356 | 4 | 1839 | +1 | +1 | 40 |
| `CORLAB` | `CORCRASH` | 1820 | 4 | 455 | +1 | +1 | 37 |
| `ARMLAB` | `ARMROCK` | 2432 | 4 | 608 | +1 | +1 | 35 |
| `ARMLAB` | `ARMFLEA` | 5032 | 4 | 1258 | +1 | +1 | 35 |
| `CORLAB` | `CORCK` | 7720 | 4 | 1930 | +0 | +0 | 29 |
| `ARMAAP` | `ARMPNIX` | 30120 | 10 | 3012 | +1 | +1 | 27 |
| `CORVP` | `CORMIST` | 2636 | 4 | 659 | +0 | +0 | 21 |
| `ARMVP` | `ARMJAV` | 4704 | 4 | 1176 | +1 | +1 | 14 |
| `ARMASY` | `ARMSUBK` | 32110 | 10 | 3211 | +0 | +0 | 10 |
| `ARMALAB` | `ARMZEUS` | 8560 | 10 | 856 | +0 | +0 | 7 |
| `ARMLAB` | `ARMWAR` | 4568 | 4 | 1142 | +1 | +1 | 6 |
| `CORALAB` | `CORPYRO` | 9000 | 10 | 900 | +1 | +1 | 6 |

Sixteen bits, all sixteen right. Doing the identical sum in **double** precision
gets 7 of 16, which is what says the accumulator really is 32 bits wide and not
just "floating point somewhere". Over every scored pair: `ceil` 10 of 45, `floor`
39 of 45, the float32 replay **45 of 45**.

**Those sixteen pairs are all Escalation**, which is a patched engine, so they
had to be cleared before any of this could be called a fact about TA. They are:
the build-rate routine `0x41BA60`, body to the `ret 0xC` at `0x41BCCF`, contains
**no patched byte**, nor does the short routine after it, and the nearest change
either side is 3,838 bytes below and 192 above — the one above being two
function boundaries away, in a routine Escalation does alter. See
[TA-PATCHES.md](TA-PATCHES.md), and `tools/exe/patchdiff.py --range` to re-run
it. ProTA, the unpatched reference, agrees where it can: its one factory pair,
`ARMVP` to `ARMFAV`, predicts 488 and shows 488. That pair is not divisible, so
it corroborates the model without testing the bit pattern — the bit pattern
rests on Escalation, cleared.

The corpus cannot separate the four plausible spellings of that loop -- counting
up to 1.0 or down to 0.0, and rounding the quotient to `float` once or keeping it
at x87 width -- because all four agree on every pair the corpus has. §23's
listing is the one to follow; the point the corpus settles is the width, not the
spelling.

`tools/tad-buildtime.py` is the re-runnable version of all of this, kept for the
same reason `tools/tad-loadorder.py` is: it exits non-zero if a scored pair ever
stops agreeing.

    ./build/tad_episodes --dir ~/ta-demos --units ~/ta-mods/x-esc --emit-json /tmp/ep.json
    tools/tad-buildtime.py --episodes /tmp/ep.json --units ~/ta-mods/x-esc

**What it means for RWE.** `UnitState::addBuildProgress` accumulates integer
build points and finishes when they equal `buildTime`, which is `ceil` — so RWE
already matches TA everywhere `BuildTime` is not a multiple of `p`, and is one
tick fast in ten of the sixteen cases where it is. That is worth knowing and is
probably not worth fixing: a `float` in the simulation is exactly the hazard the
determinism section of `CLAUDE.md` exists to warn about, and the prize is one
tick on a subset of builds.

**A construction aircraft gets one increment more than the model accounts for.**
Every airborne builder in the corpus finishes exactly one tick early, and it is
the sharpest signal in the whole comparison after the model itself:

| Builder | p | builds | at exactly -1 | fastest |
|---|---|---|---|---|
| `CORCA` | 2 | 54 | 46 | -1 |
| `CORACA` | 5 | 9 | 9 | -1 |
| `ARMCA` | 2 | 3 | 3 | -1 |
| **all airborne** | | **66** | **58** | **-1** |
| every ground builder | | 6,658 | 1 | — |

58 of 66 against 1 of 6,658 is not a coincidence, and the eight that are not at
-1 sit at +29, +59 and +119, which is interference and only ever lengthens. So
-1 is a floor, not a centre.

Three things it is **not**, each of which had to be ruled out because each would
have meant something quite different:

- **Not `CORCA`.** Three separate builders across both sides agree.
- **Not the build rate.** `CORACA` runs at p=5 and `CORCA`/`ARMCA` at p=2. The
  first reading of this was that p=2 was doing something, since `CORCA` was the
  only case visible at the five-build threshold; `CORACA` is what breaks it.
- **Not the exactly-divisible case.** `BuildTime % p` is 0 for some of these
  pairs and non-zero for others, and all of them are -1 alike. Nor is it the
  strict-versus-non-strict end test: none of these accumulators lands exactly on
  the endpoint, so both spellings predict the same number for all of them.

What was left was that an airborne builder credits its job once more over its
life than a ground one does, and [TOTALA-EXE.md](TOTALA-EXE.md) §110 has now
read out where: **the extra increment lands on the creation tick, which gets
two.**
`VTOL_MobileBuild` calls the `INBUILDSTANCE` wait and discards its answer, and
the wait leaves event bit `0x4` in the mission's wake mask. Every COB `set`
raises that bit on the unit and nothing on an aircraft consumes it between
jobs, so the mission service loop runs the lathe a second time before the tick
ends. A factory waits for its stance before it creates the nanoframe, and a
ground constructor's wait returns its answer, so neither can. The table above
was drawn before builder ids were scoped in time; over the corpus as it is
emitted now it is 60 of 68 builds (`CORCA` 47 of 55, `CORACA` 9 of 9, `ARMCA`
4 of 4), and the eight off it are late by exactly 30, 60 or 120 ticks, the
whole-second shape a resource stall leaves (factories show it too: 347 of their
384 late builds are late by a multiple of 30). All 15 airborne pairs' modes,
including the 10 whose `BuildTime` divides exactly by p, match the float32
model with two increments on the `0x09`'s tick, and `tools/tad-buildtime.py` now
scores the class that way, cell by cell.

**RWE does the same now**, so the airborne cells are in the build fixture: at
the default `--min-builds` the corpus offers three, `CORCA` to `CORDRAG`,
`CORMEX` and `CORRAD`, and they carry -1, -1 and 0 — the ordinary integer
accumulator delta on the two pairs whose `BuildTime` divides by the rate
(§88), and nothing of their own. That is what porting bought over licensing:
there is no airborne delta left to write into `expectedDurationDelta`, and the
`[build][corpus]` test carries a case showing that crediting them once instead
would put all three a tick late against the games they came from. The
behaviour, as opposed to the arithmetic, is pinned in `aircraftbuild.test.cpp`.

#### The other constant: a builder's own deploy sequence, which is data

The second thing the comparison turned up was that ProTA 4.8's constructors take
a further ~34 ticks where Escalation's do not. That is real, and it is **not an
engine behaviour** -- it is the builder's own COB script, so it belongs to the
mod and not to TA.

The shape gave it away. A habit of the two players in that one demo -- queueing
builds at a distance, say -- would leave a distribution. What ProTA leaves is a
**hard floor**: `CORCV` building `CORWIN` is +34 over 32 builds and the fastest
of them is +34; `CORCV` to `CORMEX` is +34 on all 11; nine separate constructors
across both players give the same number, and `ARMCV` gives +36 rather than +34,
which is a per-type constant and not a per-player one.

ProTA ships `.BOS` sources, and they say it outright. `StartBuilding` starts
`RequestState`, which runs the deploy animation and only then sets
`INBUILDSTANCE`, which is the flag the engine checks before crediting any
progress at all:

| Script | sleeps before `set INBUILDSTANCE to 1` | ms | observed |
|---|---|---|---|
| ProTA `CORCV.BOS` | 498 + 600 | 1098 | +34 |
| ProTA `ARMCV.BOS` | 388 + 389 + 410 | 1187 | +36 |

Escalation's constructors sit at +3 to +5 with a tail, which is the
reposition-sometimes shape, and its factories sit at exactly 0 because a factory
has nothing to deploy. So the rule for an oracle is: **score factory builds, and
treat any mobile builder's offset as that unit's script until its script says
otherwise** -- with construction aircraft the exception, since they never wait
for their stance and get a second increment on the creation tick (above).
`tools/tad-buildtime.py` splits the three classes for exactly this reason:
immobile scored cell by cell, airborne scored cell by cell against two
increments on the `0x09`'s tick, ground mobile listed by `--overheads` with
their floors so a floor can be told from a tail, and never scored.

RWE gates build progress on `inBuildStance` (`UnitBehaviorService.cpp`) for a
factory and a ground builder, so it reproduces this as long as it runs the same
script. There is no engine gap here. A construction aircraft is exempt, because
the original exempts it: neither VTOL build mission waits for the stance
(`TOTALA-EXE.md` §110), which is why an aircraft's overhead is not its script's
either.

**A lead, not a finding.** Those two rows also pin TA's COB clock, if the
overhead really is the sleeps and nothing else. A clock advancing **33 ms** a
tick gives `ceil(1098/33) = 34` and `ceil(1187/33) = 36` -- both exact, no free
parameter. RWE's `toCobTime` uses `gameTime * 1000 / 30`, i.e. 33.33 ms, which
gives 33 and 36 and so needs a different fudge for each. Two scripts is thin
evidence for a constant this load-bearing, and the honest statement is that the
overhead is bounded to (32.97, 33.27] ms a tick *given* that assumption. Worth a
look in the binary before anything is changed.

#### Back to the packet: the second id, and the last six bytes

The second id is the one to be careful about. It is the *nanoframe*: over demo
14724, 781 of the 790 distinct values of that field reappear as the **finished
unit** of a `0x12`, and only 63 of them as a `0x12`'s **builder**. The builder is
not in this packet at all, so learning who built what means pairing the `0x09`
with its `0x12`.

What made the last six bytes legible was the contrast between two kinds of
record. A building placed on flat ground has whole-number coordinates and a
rotation of exactly zero; a unit rolling out of a factory that stands on a slope
has a fractional position and a non-zero pitch and roll with no yaw. Those are
terrain-derived, which is what identifies the field.

That also explains a shape that looks wrong at first: one factory emits the same
type at the *same* position 144 times over a game, from 144 different second ids.
That is not a hundred assists on one nanoframe, it is a factory, and it is the
observation that settled which id is which.

### `0x12`, build finished -- all 5 bytes

`u16` finished unit at offset 1, `u16` builder at offset 3.

### `0x0b`, damage -- 9 bytes, 7 of them read

`u16` victim, `u16` attacker (zero where there is no attacking unit), `u16`
damage, and a trailing `u16` that is **not remaining health**. Tracked across
successive hits on one victim it neither falls monotonically nor falls by the
damage figure: unit 1523 in demo 14724, hit repeatedly for 30 by unit 8, reads
322, 320, 318, 319, 318, 319. It moves by ones and it goes back up, so whatever
it counts is not being spent by the hits.

The damage field is constant per attacker over a run of hits and takes small
values -- 30, 40, 41, 45, 46, 180 in that demo -- which is what a weapon's damage
figure looks like. **It is now confirmed against the weapon definitions
themselves**: once a shot is paired to the damage it caused and the pairs are
grouped by (shooter type, weapon slot), each cell's modal damage is that
weapon's own `[DAMAGE] default` out of the mod's weapon TDF. See "Pairing a
`0x0d` to the `0x0b` it caused" below -- and note that the confirmation came
free, because nothing in the pairing filters looks at this field.

**`0x0b` is not a complete damage ledger.** 17,526 of the 56,913 script-driven
deaths in the corpus have *no* recorded damage on the victim at all, and the
corpus contains only ten `0x0e` area-of-effect records in twelve million
subpackets, so splash damage is not arriving by that route either. Any oracle
that wants total damage absorbed has to treat the absence as unknown rather than
as zero.

**It is incomplete for units that live, too, and by much less.** The figure
above only ever measured dying units. Bracketing a shot with its victim's own
`0x2c` full-state records either side of it puts a number on the other case:
119 of 1,713 clean brackets show a victim losing health with nothing in the
ledger at all, 6.9% before allowing for the instrument's own 82% sensitivity.
See "Why 53,706 shots drew no damage" below, which is also where the sensitivity
is measured and where the bias in that sample is spelt out.

### `0x0c`, death -- all 11 bytes, and this one confirms the disassembly

| Offset | Size | Field |
|---|---|---|
| 1 | u16 | the unit that died |
| 3 | u32 | DirectPlay id of the killing player, matching offset 0x91 of a status message |
| 7 | u16 | the killing unit |
| 9 | u8 | corpse **severity** |
| 10 | u8 | death **cause** in the high nibble, corpse **level** in the low one |

See the wrecks section below; this is the largest single finding of the pass.

### `0x0d`, shot fired -- all 36 bytes

Two 16.16 position triples -- where the shot came from and where it was aimed,
and the second is far enough from the first that it cannot be a velocity -- then
a rotation triple, then `u16` target (zero if the shot was not aimed at a unit),
`u16` shooter, and one byte that is 0, 1 or 2.

**That last byte is the weapon slot**, a 0-based index into the shooter's own
`Weapon1`/`Weapon2`/`Weapon3` -- so `WeaponN` with `N = slot + 1`. This is what
makes a shot resolvable to a *weapon definition* rather than only to a shooter,
and it is therefore the thing the weapon-event oracle stands on.

**The evidence, and why the obvious test gets it wrong.** `tad_episodes
--weapon-slots` collects, for every unit type the corpus caught firing, the set
of slots it was seen using, and checks each against that type's FBI. Over the
thirteen demos and all 631,578 shots:

| test | (demo, type) observations that fail |
|---|---|
| `slot < number of weapons the FBI declares` | 112 of 658 |
| the slot the FBI actually **fills** is non-empty | **14 of 658** |
| the same, with the shooter named by its id's *first* build | 453 |

The first gap is the whole argument for the reading. Ninety-eight observations
are explained by nothing except the standard TA convention of putting a unit's
anti-air weapon in the **third** slot and leaving the second empty: `ARMSAM`,
`ARMJETH`, `CORMIST` and `ARMYORK` all declare `Weapon1` and `Weapon3`, all fire
slots 0 and 2, and none of them ever fires slot 1. Under a count test they look
like counter-examples. Under an occupancy test they are confirmations, and they
are confirmations no other reading of the byte predicts.

The second gap is not about the byte at all -- it is what naming a shooter
badly costs, and it is the next paragraph.

**The fourteen that remain are not explained**, and the obvious explanation has
been tested and does not cover them. Twelve are types the data set gives no
weapon at all -- wind generators, metal extractors, fusion plants, a repair pad
-- and two are factories whose only weapon is `LAB_DIR`, the dummy TA labs aim
their build spot with. None of them can fire, so the natural reading is that the
shooter was named wrongly: the id was recycled once more than the scoping
caught, and the unit really firing was one whose own build the recording never
paired.

Two measurements were made to test that, and both came out against it.

- **Volume.** The fourteen account for **7,180 shots**, not a handful -- 1.1% of
  the 630,522 with a named shooter. Three of them carry 85% of that: `ARMULAB`
  2,686, `ARMASP_UPGRADE` 1,721 across two demos, `ARMFWIN` 1,668. A stray
  misattribution does not fire two thousand times.
- **Staleness.** If the name came from a long-dead unit whose id was reused, the
  shot should land far after the build that supplied the name. The median gap is
  **10,008 ticks for a failing shot against 7,083 for a conforming one** -- a
  difference, but nothing like the separation a recycled id would give.

The per-type figures do split, and the split is the useful part. Five of the
fourteen are *very* stale -- `ARMCK` at 79,952 ticks, `ARMMAKR` 48,792, `ARMFAHP`
43,990, `ARMMOHO` 40,100, `ARMWIN` 20,959 -- and those are recycled ids on the
evidence, but between them they are only 506 shots. The three that carry the
volume sit at or below the conforming median (`ARMFWIN` 5,956, `ARMASP_UPGRADE`
9,527, `ARMULAB` 13,447), so staleness does not explain them.

The competing story -- that `0x0d` covers something besides weapons, a factory
aiming its build spot -- is **also** damaged, and by the slot itself: `ARMULAB`
and `ARMFAHP` carry `LAB_DIR` in slot **3** and the shots attributed to them
read slot **0**. A record reporting `LAB_DIR` would have to say 2.

So the reading of the byte stands on the 112-to-14 gap, which nothing here
touches, and the fourteen are an open question of their own. What would move it:
whether those shooter ids are ever rebuilt *after* the shot (an open-ended naming
window is where a missed `0x09` hides), whether the same type conforms in the
demos where it is not a violator, and what those shots look like geometrically --
`ARMFWIN` and `ARMULAB` are both water units, which may or may not be a
coincidence.

**Naming the shooter is the whole difficulty here**, and it is worth knowing
before reusing any of this. TA recycles unit ids heavily -- in 14725, 2,622 of
4,093 distinct ids are reused by a later nanoframe, and 2,550 of those by a
*different* type -- so a map that keeps the first name an id ever held names
most shooters wrongly, and a wrongly named shooter is exactly what makes a metal
extractor appear to open fire. `--weapon-slots` scopes each name to the id's
most recent build *before the shot*, and that one change takes the occupancy
test from **453 failures to 14**, which is the third row of the table. With the
scoping, **630,522 of the 631,578 shots have a nameable shooter** and 612,992
are aimed at a unit, so naming is not what will limit a weapon corpus.

The build-timing cells scope the same way, in `tools/tad-buildtime.py` first and
then in the port, and the two still agree cell for cell. It was worth doing for
the evidence rather than for a correction: **no scored cell's mode moved**, but
builds roughly double -- `ARMVP -> ARMFAV` 151 to 363, `ARMLAB -> ARMPW` 152 to
256, `CORAP -> CORFINK` 69 to 115, `ARMAAP -> ARMPNIX` 8 to 27 -- and four more
pairs clear `--min-builds`: `ARMAVP -> ARMLATNK`, `ARMASY -> ARMSUBK`,
`ARMHP -> ARMLH`, `CORVP -> CORCV`. The nine cells whose mode *did* move are all
ground-mobile builders, which are never scored. Why the scored modes were safe
under the old map is in "What an episode looks like", and it is luck a weapon
oracle will not have: a build's arithmetic depends on one shared integer, and a
weapon's behaviour depends on the weapon.

### Pairing a `0x0d` to the `0x0b` it caused

The decoding above is not the hard part of a weapon oracle. **Nothing in the
stream links a shot to the damage it caused** -- no shot id, no sequence number,
and no tick on a `0x0b` beyond the `0x2c` serial of the packet carrying it,
against 631,578 shots and 824,844 damage events. A unit firing a burst has
several shots in flight and several damage events arriving, and nothing says
which came from which. So time-of-flight is a **filtered statistic**, the filters
are the work, and `tad_episodes --emit-shots` exists so they can be argued over
the data before any of it is ported:

```bash
./build/tad_episodes --dir ~/ta-demos --units ~/ta-mods/x-esc \
    --emit-shots /tmp/shots.jsonl
```

It writes every `0x0d`, `0x0b` and `0x0c` as **JSON Lines** -- one object per
line, about 1.5 million of them and 320 MB, which is why it is not a JSON array
-- with shooters, targets, victims and killers already named through the same
scoped lookup `--weapon-slots` uses, so a consumer never has to redo the
load-order work and can never do it differently.

**Coordinates are written exactly**, each 16.16 value in its shortest exact
decimal form. They were three decimals until the footprint model below, which
floors positions onto sixteen-unit squares: a rounded coordinate on the wrong
side of a boundary moved whole pairings, and `CORVAMP` read 52% in the script
against 66% in the port. Regenerate an old dump before scoring it.

**A shot also carries its rotation triple** as `rx`, `ry`, `rz`, which is what
identified the field: see "Why 53,706 shots drew no damage". A dump written
before that has no such keys and has to be regenerated to use them.

#### A `0x0b` is sent by the attacker's owner, which is what makes this tractable

The first thing the dump settled, and the most useful. Each peer emits the
events for its own units against its own `0x2c` clock, so it mattered a great
deal whether a shot and its damage were stamped by two different peers. They are
not. Taking each unit's owning peer from the `0x0d`s it emits and checking every
damage record against it:

| the sender of a `0x0b` is | records |
|---|---|
| the **attacker's** owner | **797,783** |
| the victim's owner | **0** |
| neither, or no attacker to attribute | 27,061 |

Not one counter-example in thirteen games. The residual is not evidence against
it: 21,013 of those records carry attacker id 0 -- terrain, decay, self-damage --
and the rest name an attacker that never fired a `0x0d` at all, so there is
nothing to compare the sender against. **A flight time is therefore a difference
within one clock**, and none of the cross-clock correction this was expected to
need is required.

#### The filters, and what they leave

Keyed on `(attacker, victim)`, a shot is kept only when the shooter fired at
that victim exactly once in a +-300-tick window and exactly one damage record
from that shooter to that victim lands in the 300 ticks after it. Over the
corpus that keeps **35,535 of 631,578 shots**. The rejections are the shape to
expect and are worth reading rather than summing: 405,784 shots have another
shot at the same victim just before them and 111,547 just after -- which is what
sustained fire looks like -- 53,706 have no damage recorded in the window at
all, and 5,404 have several. **The 53,706 are accounted for**, in "Why 53,706
shots drew no damage" below: a quarter of them are rounds whose victim died
before they arrived, and most of the rest are misses, held to the `0x2c` health
stream rather than assumed from the ledger's silence.

**The pairing is confirmed by a number it was not built from.** Group the
survivors by (shooter type, weapon slot) and take the modal `damage` value of
each cell: it is the weapon's own `[DAMAGE] default` out of the mod's weapon
TDF, cell after cell -- `GAUSS_SNIPE` 675, `LIGHTNING` 225, `ROCKET` 108,
`MISSILE_GF_HEAVY` 45, `LASER_SUMO` 768. Nothing in the filters looks at the
damage figure, so a wrongly paired event has no reason to carry the firing
weapon's damage. This also closes the older note in the `0x0b` section above:
the damage field **is** the weapon's damage, now checked against a weapon
definition rather than inferred from looking small.

#### Three models, one per scored class

With the pairing done, the implied speed -- straight-line distance from the
shot's origin to its aim point, over the paired flight time -- lands on the
weapon's declared `weaponvelocity / 30` with a median ratio of **0.971** over 95
cells. The deviations are not noise, and sorting the cells by what kind of
weapon fired them is what makes them legible. `tools/tad-weapontime.py` is the
reference that does the sorting and the scoring, and it is a check rather than a
listing: it exits non-zero if a scored cell moves.

```bash
./build/tad_episodes --dir ~/ta-demos --units ~/ta-mods/x-esc \
    --emit-shots /tmp/shots.jsonl
tools/tad-weapontime.py --shots /tmp/shots.jsonl --units ~/ta-mods/x-esc --classes --drift
```

**All three classes stop the same way: on the victim's footprint, not at the aim
point.** The models below differ only in how far each step goes. Where the
round stops is the section after them, "Where a round stops", and it is what
makes all 40 scored cells land -- 24 constant-speed, 13 with a motor, 2 that
lob a shell and 1 whose `cruise` flag turns out to be inert, the last two
classes added later and written up under "A shell: the flat root, and the cosine
that falls out of it" and "And `cruise` has left the table altogether". Before the footprint stop
the constant-speed model was `flight = ceil(d / v) - 1`, landing on 22 of 24
cells, and its `-1` was read as a projectile taking its first step on the tick
it is fired. That reading of the `-1` is retired -- the `-1` was the footprint.
A round *does* take its first step on the tick it is fired; that fact simply
never showed in this interval, for the reason "Which tick a round first moves
on" gives below.

**A round that flies at one speed** covers `weaponvelocity / 30` world units a
step. The mode's share now runs 43% to 93% -- `CORFAV` 93%, `ARMFAV` 92%,
`CORFAST` 91%, `PARALYZER` 90%, `GAUSS_SNIPE` 58% over 4,348 pairings -- against
40% to 76% under the aim-point model, and what is left of the spread is mostly
drift: a victim that moved is not where its footprint was stamped.

**A round with a motor gets replayed a tick at a time.** A missile leaves the
barrel at `startvelocity`, gains `weaponacceleration` up to `weaponvelocity`
while its motor runs, and coasts after it stops. The replay is a **port of RWE's
own `createProjectileFromWeapon` and `updateSelfPropelledProjectile`**, which are
in turn a decoded reading of `0x49C980` and `0x49B9AE`, and not a guess at the
shape from the TDF field names -- which is what it was while the class was only
being scouted, and the difference is the point. Against the same corpus, stopped
on the footprint, it lands on **13 of 13** cells, at shares of 41% to 76%.

Three things that reading settled, none of them guessable from the field names:

* **`startvelocity = 0` means two different things.** With no acceleration it is
  "off the rail at full speed"; with acceleration it is "from a standstill". The
  engine asks it in that order and so does the script's `launch_speed`.
* **The burn is real, and it changes pairings without changing a mode.** The
  motor runs `range / weaponvelocity` ticks, or `weapontimer` where the weapon
  says `noautorange`, and running out is not death -- without `burnblow` the
  missile coasts on at whatever speed it reached. A `MISSILE_GF_HEAVY`'s motor
  stops at tick 15 and one of the checked-in episodes arrives on step 17, so the
  coast is exercised; but adding the term left **every cell's mode where it was
  and moved no share by more than three points**, because most shots arrive
  before the motor stops or are at the cap by then anyway. It is in both the
  script and the port on the strength of being the engine's arithmetic rather
  than of what it does to this corpus, which is the right way round.
* **`cruise` and `twophase` are not the accelerating class**, and reading the
  velocities alone hides that. `ROCKET_HRK` is `selfprop` with `cruise` and a
  start speed equal to its cap, so the velocity test put it in the constant-speed
  table -- where it sat at the worst share in it, 40%. It is its own class now,
  and it *is* scored: the reason first given here for splitting it out ("a cruise
  missile climbs to a fixed altitude and flies over the aim point before coming
  down, `0x49B455`") was the wrong reason, because this weapon cannot steer and
  so never reaches that clause. The right reason was that a `selfprop` cell wants
  the motor class's drift bound, which the constant-speed table does not apply.
  See "And `cruise` has left the table altogether" below. `GAUSS_SNIPE` is the
  other `selfprop` round whose
  start speed equals its cap; that one is genuinely a constant speed, and the
  fixture flies it on the motor path anyway because that is the path the engine
  puts it on.

#### Where a round stops: the victim's footprint

A projectile does not detonate on reaching the point it was aimed at. The
per-tick update `0x49B720` moves it and then calls `0x49B090`, and that routine
takes the map square the round is now in (`0x4815A0`, `pos >> 20`, sixteen
world units) and reads the square's two unit slots:

```
49b1c7  ax = WORD [sq+0x0]             ; the unit standing in this square
49b1f4  cmp cl,[esi+0x66]              ; same owner as the round -> ignore
49b1f9  edx = unit's def ; ebp = unit+0x6E (its y)
49b202  ecx = [def+0x16E] + y          ; the top of its model
49b20d  cmp proj.y, ecx ; jge miss     ; below the top -> 0x499EB0, detonate
49b222  ax = WORD [sq+0x2]             ; the second slot: the same test with a
49b25b  ...                            ;   floor as well, def+0x162 <= dy <= def+0x16E
```

then features (`0x49B2B3`, the `hitdensity` finding in `docs/TOTALA-EXE.md`
§24), then the ground. A unit is in a square's slot when its footprint covers
it: `FootprintX` by `FootprintZ` squares, the same box `def+0x15E..0x172` holds
at a half-extent of `footprint * 8` (§27). So the round stops on the **first
step that puts it in one of the victim's squares** and below its top.

The model scores the footprint half of that and not the height half: flight is
the first step `k` at which the round, flown along the line from its origin to
its aim point, stands in the victim's footprint, stamped at the aim point with
the left edge rounded to the nearest square -- which is RWE's
`computeFootprintRegion` -- and the shot-to-damage interval is `k` itself.

That the *recorded* interval is `k` and not `k - 1` is the demo's clock rather
than a tick of hesitation in the round: a `0x0d` is queued before its tick's
`0x2c` and a `0x0b` after it, so a shot reads a tick early. "Which tick a round
first moves on" below has the decode and the stream measurement. The model and
`flightTicks` are unaffected; what it settles is what a consumer may conclude
from them, which is that a round fired on tick T detonates on `T + k - 1`.

`tools/tad-weapontime.py --footprint` prints the evidence, over victims that
cannot move so nothing but geometry is being scored:

| larger footprint side | constant speed: aim point | at -1 | footprint | accelerating: aim point | at -1 | footprint |
|---|---|---|---|---|---|---|
| 2 | 66% | 4% | **98%** (n=160) | 74% | 20% | **79%** (n=171) |
| 3 | 72% | 18% | **98%** (n=343) | 58% | 38% | **80%** (n=226) |
| 4 | 80% | 18% | **98%** (n=173) | 31% | 58% | **84%** (n=45) |
| 5 | 37% | 48% | **99%** (n=99) | 26% | 61% | **90%** (n=31) |
| 6-8 | 6% | 67% | **94%** (n=33) | 15% | 15% | **59%** (n=27) |
| all | 65% | 21% | **98%** (n=810) | 56% | 34% | **79%** (n=503) |

The aim-point column falls as the victim grows and its `-1` column climbs to
meet it, which is the residual the earlier model carried; the footprint column
does not move. **792 of 810** still constant-speed pairings land on it.

Two measurements back the stamping, neither tuned to the table. The aim point is
the victim's position: for a victim that cannot move, the footprint edge
computed from it falls on a square boundary to a tenth of a unit on both axes in
904 of 1,313 still pairings. And rounding that edge to the nearest square is the
rule that fits -- over the constant-speed class at drift zero it takes 97.8%,
against 88.6% for flooring it, 84.1% for ceiling it and 94.6% for flooring the
centre square and counting half the footprint either side, and the margin
widens once the victim moves (87.9% against 65.6%, 66.9% and 74.4% under eight
units of drift). RWE already stamps a unit that way.

**What stays open.**

* **The height half.** A round that passes over a short victim's top keeps
  going in the original. Nothing here scores it, and the fixture's victims are
  made too tall to fly over so that it cannot decide a tick. The two fighter
  cells are where it would show -- `ARMFIG`, `CORVAMP` and `CORVENG` all fire
  from about 130 units above their aim point -- and `ARMFIG` still reads 41%
  against `CORVENG`'s 61%, same airframe, same weapon, same height, so the
  height half alone does not explain it.
* **47 scored pairings step over the footprint without landing in it** -- a
  fast round crossing the corner of a small victim between two steps. They are
  counted against their cell's share rather than dropped.
* **Which tick the first step lands on** was open here until the section
  below. It is settled: on the tick the round is fired, and the extra tick the
  interval appears to carry is the demo's own clock rather than the engine's.

#### Which tick a round first moves on, and why the interval reads one higher

The interval above is `k`, the step count to the footprint. Read naively that
says a round fired on tick T lands its damage on `T + k`, which would mean the
round waits a tick before moving. **It does not.** The binary says the first
step is on the firing tick, and the extra tick is put there by how a demo
stamps its events.

**The engine's order.** One tick of `TotalA.exe` is `0x4954BD`: increment the
game tick, the unit pass `0x48AD30`, the projectile pass `0x49B720`
(`0x495513`), the feature pass, then the per-player settle (§111 of
[TOTALA-EXE.md](TOTALA-EXE.md)). A firing weapon is reached from the per-tick
weapon update `0x49E1A0` inside the unit pass, and at `0x49D77E` it calls
`0x49C9C0`, which appends the round to the flat projectile array
(`globals+0x141F3` the count, `+0x141F7` the base, stride 107) and takes the
count up with it. The projectile pass then reads that count **once**
(`0x49B728`, latched into its trip counter at `0x49B740`) and walks it,
decrementing (`0x49BE41`) without ever re-reading it. The round created a
moment earlier in the same tick is inside that count, so it moves
(`0x49BD41`) and is tested against the square it now stands in immediately
afterwards (`0x49BD88` into `0x49B090`). A hit runs `0x499EB0` → `0x499CD0` →
`0x489BB0` synchronously, and that is what queues the `0x0b`.

So **fire on T, first step on T, detonation and damage on `T + k - 1`.**

Two things that could have made it later, and do not:

* **The burst counter is not a deferral for an ordinary weapon.** `0x49CB79`
  sets the new round's `proj+0x60` from the weapon's own `burst` (`wdef+0xEA`),
  and the parser at `0x42E619` defaults that key to **0**. A zero sends the
  record down the flying path at `0x49B9AE`; a non-zero one makes it a
  *template* that spawns one copy per `burstrate` and never flies itself. Only
  twenty-eight weapon definitions in the Escalation data name the key at all
  and none of them is a scored cell -- the burst class is excluded from the
  oracle for a different reason, further up. A burst continuation **is** a tick
  later than the template that made it, because `0x49B810` appends it past the
  trip count the pass had already latched -- which is the one place this array
  walk's snapshot is observable. The consequence worth writing down is that
  **`burst=1` is not the same as omitting the key**: it makes the record a
  template all the same, so its single round is spawned by the projectile pass
  and does not move until the tick after the trigger. One weapon in the data
  set does that.
* **No creation-tick guard exists.** The only per-round time test on the way in
  is the burst gate at `0x49B790` (`gameTick < proj+0x42 + burstrate`), and
  `proj+0x42` is the creation tick (`0x49C7AC`), so with `burstrate` at zero it
  does not even hold the first copy back.

**Where the extra tick comes from.** A subpacket has no clock of its own; it is
stamped with the last `0x2c` before it in its sender's stream. The `0x2c` is
built and queued at the end of that player's unit sub-pass -- `0x48B003` calls
`0x48B710`, which ends by handing the packet to the same queue `0x451DF0` that
every event record goes through (`0x48B903`). So within one tick the sender's
buffer gets: the unit pass's `0x0d`s, `0x09`s and `0x12`s; **then** the `0x2c`
stamped T; **then** the projectile pass's `0x0b`s and `0x0c`s, and the settle's
`0x28`s. A shot therefore reads as tick `T - 1` and its damage as tick
`T + k - 1`, and the difference is `k`.

**The stream says so directly.** Take the run of subpackets between two
consecutive `0x2c`s from one sender and ask which side of it each code sits on:

| in one sender's run between two `0x2c`s | runs |
|---|---|
| every `0x0b` **before** every `0x0d` | **121,624** |
| every `0x0d` before every `0x0b` | 536 |
| interleaved | 477 |
| every `0x0b` before every `0x09`/`0x12` | **13,145** of 13,485 |
| every `0x28` before every `0x0d` | **4,210** of 4,373 |

99.2% of the runs that carry both put the damage records first, which is only
possible if they belong to the *previous* tick's projectile pass. The build
events sit on the shot's side of the divide and the resource samples on the
damage's side, exactly as their emitters' positions in the tick predict. The
residual is expected rather than awkward: `0x489BB0` has callers inside the
unit pass too (the lathe at `0x41BC49`/`0x41BDC7` among them), so a minority of
`0x0b`s really are queued before the `0x2c`.

**This also closes §111's either/or.** That section could not tell whether the
settle runs at internal ticks one short of a multiple of 30 or the demo clock
is a tick behind the internal one. It is the second, for everything queued
before the `0x2c`: a settle at the end of internal tick T pays a stalled
builder in tick `T + 1`'s unit pass, and that builder's `0x09` is stamped with
tick T's `0x2c` -- which is why the corpus puts the first accepted increment on
demo ticks that are multiples of thirty.

**What it means for the fixture.** Nothing changes. `flightTicks` is still
`damageTick - shotTick` and still the step count, and `weaponflight.test.cpp`
still spawns the round outside `tick()` and counts `k` ticks to the victim, so
the two conventions cancel and the numbers stand. What was missing is a test of
the half neither of them covers, and that is
`src/rwe/sim/weaponfiretick.test.cpp`: it fires a real weapon from a real unit
through `tick()` and asserts the tick of the damage against the tick of the
shot, with a case whose round reaches the footprint on its first step so that
the two fall on one tick. RWE already matched -- `spawnProjectile` emplaces
during the behaviour pass and `updateProjectiles` walks the new round in the
same tick -- so nothing was changed to make it pass; the test exists so that a
rearrangement of `tick()` cannot quietly put every weapon in the game a tick
behind the original without the corpus episodes noticing, which they would
not.

**Every routine above is unpatched in Escalation's `TotalA.exe`.**
`tools/exe/patchdiff.py --range` reports `0x4954A0`-`0x495570` (the tick),
`0x48B710`-`0x48B920` (the `0x2c` builder), `0x49C9C0`-`0x49CC20` (creation),
`0x49D660`-`0x49D880` (the fire routine and the `0x0d`), `0x451DF0`-`0x451FD0`
(the queue), `0x489BB0`-`0x489CE0` (damage and the `0x0b`), `0x49B090`-`0x49B2E0`
(the stop) and `0x42E5F0`-`0x42E690` (the `burst` parse) all clean. Two ranges
in the chain do carry patched bytes and neither touches the order:
`0x48AD30`-`0x48B030` is patched only between `0x48AEC0` and `0x48AFC5`, which
is the self-repair arithmetic -- the call sequence, the unit loop's back edge
and the `0x2c` call at `0x48B003` disassemble identically in both -- and
`0x499CD0`-`0x49A0A0` has two bytes at `0x49A00C`, a `jmp` turned into two
`nop`s so that a weapon with flag bit 10 gets the crater call as well as the
effect call. The `0x0b` emitter at `0x499E37` is untouched.

**One patched byte pair that the next class must know about.** Escalation
*does* change the projectile pass, at `0x49BC67`: the test that decides what a
**ballistic** round does when its life runs out reads `burnblow` (bit 23,
`shr eax,0x17` / `je`) in GOG v3.1 and `noautorange` (bit 27, `shr eax,0x1b` /
`jne`) in Escalation, which is the opposite sense as well as a different flag.
The selfprop equivalent at `0x49BAC3` is unpatched, so nothing scored today is
affected -- but the ballistic model is the next one to be written and it would
be scored against Escalation demos, so it has to use Escalation's rule.

#### The drift bound: when a flight time stops measuring a flight

The missile class needs a filter the constant-speed class does not, and the
reason is the same fact the pairing rests on: **a `0x0d` records where the shot
was AIMED**. A victim that moves while the round is in the air is not where the
distance says it is when it arrives. Bucket every pairing by how far the victim
could have gone -- its own FBI `maxvelocity` times the observed flight -- and
both scored classes fall down one curve (`--drift` reprints it):

| drift, world units | constant speed | accelerating |
|---|---|---|
| 0 (immobile victim) | 98% (n=810) | 78% (n=515) |
| 0-8 | 88% (n=2,091) | 71% (n=173) |
| 8-16 | 77% (n=2,523) | 56% (n=836) |
| 16-32 | 61% (n=3,415) | 43% (n=4,064) |
| 32-64 | 39% (n=1,383) | 26% (n=4,121) |
| 64-128 | 47% (n=194) | 18% (n=667) |
| 128+ | 21% (n=198) | 9% (n=5,398) |

(share of pairings landing on their class's model, the footprint stop; victims
that cannot be named are left out of both columns. Under the aim-point model the
top row read 65% and 54% and the curve was much flatter: the footprint is what
lifted the still end, and drift is what is left below it. The constant-speed
64-128 row, 194 pairings, is the one place the curve rises.) **The two classes
sit on the same curve and differ only in where their mass lies:** two thirds of the
constant-speed pairings drift less than 32 units and two thirds of the
accelerating ones drift more. That is the whole of the difference between a class
that can be scored over every victim and one that cannot. A laser crossing 200
units in six ticks barely notices that its target moved; a missile spending
thirty ticks getting there does, and missiles are what gets fired at aircraft.

So a self-propelled cell is scored over the pairings whose victim **could not
have outrun one step of the projectile**, which costs 7,130 of its pairings and
leaves 13 cells. The bound is the projectile's own step rather than a constant
because the quantity is quantised in steps: a drift under a step cannot move the
arrival tick by more than one, and a drift of several can move it by several.

**The bound is not tuned.** It was set under the aim-point model, when half a
step would have put every surviving cell on that model and made its two missile
exceptions disappear -- which is exactly why it was not the bound, since a filter
chosen for the disagreements it removes is not evidence -- and the footprint
model was scored under it unchanged. The constant-speed class keeps every
victim it can name for the mirror-image reason: it does not need the bound.
Both choices are visible in the script, and `--drift` prints the measurement
either could be argued from.

**And 40 of the 40 scored cells carry their firing weapon's own `[DAMAGE]
default` as their modal damage,** across every scored class. Nothing in the
filters looks at the damage field, so that is the pairing checking itself against
evidence it was not built from.

**The three classes no model describes** are listed by `--classes` and never
scored. Each is explicable, and only one of the three still wants a model:

| class | cells | pairings | why no model fits |
|---|---|---|---|
| `vlaunch` | 3 | 780 | goes up before it goes anywhere, and about 190 ticks later arrives with no mode -- see "A vertical launch" below |
| `burst` | 7 | 630 | the quantity the filters isolate is not a flight time; see below |
| `waterweapon` | 4 | 297 | a torpedo's model is probably right and the corpus cannot hold it; see below |

(Two more names appear there. "ballistic selfprop" is for a weapon carrying both
flags: TA dispatches on `selfprop` first at `0x49B9C2`, so such a round is flown
by the motor off a ballistic launch angle. `ROCKET_HEAVY` is the only one in this
data set and nothing in the corpus fires it. "cruise steering" is the guard
described below, and nothing in this data set is one either.) **Ballistic used
to head this table** at 18 cells and 4,946 pairings, "travels an arc, which is
longer than the straight line measured". It has a model now -- the arc is longer
by exactly the cosine of the launch angle -- and seventeen cells, `CANNON_FIDO`
having moved to `burst` where it belonged. Two of those seventeen are scoreable.

**And `cruise` has left the table altogether, because its entry was wrong.**
That row read "climbs to a fixed altitude and crosses the aim point before
descending", which is a true statement about a cruise missile and a false one
about the only cell it was ever applied to. The cruise clause lives in the
**aim point** (`0x49B3E0`), and `updateSelfPropelledProjectile` asks for an aim
point from exactly one place: its guidance step. `ROCKET_HRK` declares no
`guidance`, no `twophase` and no `turnrate`, so it never steers, so it never
reaches the clause, and it flies the same straight line every other motor round
flies. What its exclusion was really costing it was the **drift bound** --
`scoreable` handed an unbounded pass to every class that was not `accelerating`
or `ballistic`, and this is a `selfprop` cell that wanted the motor class's
victim bound like any other. With it, `CORHRK` reads **+0 at 65% over 291
pairings** against +0 at 41% over 709 unbounded, which is squarely inside the
motor class's own 41-76% range, and its modal damage is `ROCKET_HRK`'s own 160.
**Not one pairing's answer changed; only which ones were scored.** It is the
fortieth checked-in episode.

A cruise weapon that *can* steer is still unscored, under the name "cruise
steering", on the precedent that names "ballistic selfprop". Nothing in this
data set is one, and that is exactly why the guard exists: the model above is
only known to be right about a cruise weapon that cannot turn.

#### The replay, and why it scores nothing

That correction was measured rather than argued, and the instrument is worth
keeping. `selfprop_replay` in `tools/tad-weapontime.py` is the whole of
`updateSelfPropelledProjectile` written out a tick at a time -- the launch of
`0x49C980` and the vertical-launch spawn of `0x49CC20`, the motor of
`0x49BA16`, the two-phase turnover of `0x49BAE1`, the aim point of `0x49B3E0`,
the `burnblow` abort, and the SimAngle quantisation RWE's heading and pitch
carry -- ported from RWE's own code, the same provenance the step-length model
has. `--replay` prints what it establishes, and both halves are negative
results:

* over `ROCKET_HRK` it gives the step-length model's answer on **all 709** of
  its pairings, which is what says the cruise clause does nothing here;
* over the accelerating class it gives the stepper's answer on **all 3,537**
  pairings of all thirteen cells -- not merely the same modes, the same
  pairings, which a different model that happened to agree would not manage.

So the replay is not a second model. Nothing is scored by it: `steps()` still
scores everything, with `cruise` added to its motor branch. What the replay
bought is that the two remaining selfprop exclusions below are measurements
instead of sentences, and it is the reason each of them can now say what it
does.

#### A vertical launch: the model is not the problem

The replay flies a `vlaunch` round without difficulty -- a vertical launch is
one more branch of the same routine, heading zero and pitch straight up out of
`0x49CC20`, with the whole climb coming from the motor -- and the class still
does not land. `--unmodelled` prints it:

| cell | weapon | median flight | inside the bound | mode | share | victims that cannot move | IQR |
|---|---|---|---|---|---|---|---|
| `ARMMERL` | `VLAUNCH_TRUCK_ARM` | 188 | 189 | -23 | 6% | 189 | -19..-1 |
| `CORVROC` | `VLAUNCH_TRUCK_CORE` | 195 | 64 | +0 | 8% | 64 | -16..+9 |
| `CORSPID` | `RADIATION_CLOUD` | 10 | 41 | +1 | 15% | 7 | -2..+2 |

The shape is what matters. These flights are **about 190 ticks**, thirty times
a laser's, and over that the drift bound admits only victims whose
`maxvelocity` is under about 0.12 -- which is why its column and the
cannot-move-at-all column are the same number. **So the spread is not drift**,
and it is still twenty ticks wide with no mode. `RADIATION_CLOUD` is the
opposite case and no better: its climb is zero ticks and its turn rate is
effectively instant, so it barely exercises the profile at all, and it sits on
the replay at the median at every distance while still having no mode (15% over
41). Scoring the class would check in a disagreement, which is what the rule
against that exists to prevent. A flight that long is not a flight time an
engine can be held to.

#### A torpedo: the model is probably right and the corpus cannot hold it

`waterweapon` is **not a flight kind**. The dispatch at `0x49B9AE` names five --
`selfprop`, `lineofsight`, `ballistic`, `dropped`, `meteor` -- and this is not
one of them. It enters a flight from inside the selfprop branch only, at
`0x49B9EB`, where a waterweapon **above sea level** takes gravity and has its
pitch forced to zero; otherwise it is a target-eligibility rule (`0x49ABE3`, the
submarine rule above). So the old entry, "a torpedo's path from a surface
launcher to a submerged target is not that line either", names a real clause,
and the replay does not model it.

The replay still settles the class, from the other end:

| cell | weapon | named | inside the bound | mode | share |
|---|---|---|---|---|---|
| `ARMSUBK` | `TORPEDO_SNIPE` | 106 | 47 | +0 | 13% |
| `CORAMPH` | `TORPEDO_LIGHT` | 66 | **19** | **+0** | **95%** |
| `ARMLANCE` | `TORPEDO_SEAP` | 63 | **0** | - | - |
| `ARMLANCE` | `TORPEDO_SEAP` | 62 | **0** | - | - |

`CORAMPH` fires from *below* the surface -- its shots leave at y≈22-33 at
victims at y≈74-87 -- so its torpedo never has an above-water segment, the
unmodelled clause never fires, and it reads **the best share anywhere in this
corpus**. `ARMLANCE`'s are dropped from an aircraft 185 units up, the clause
runs for most of the flight, and they are the worst. That is the clause
confirming itself without being modelled.

**And it would not help to model it.** Sea level is a per-map byte at
`world+0x1427f` that a demo does not carry, and getting it from the map files
would change nothing: after the drift bound the two `ARMLANCE` cells keep
**zero** pairings and `CORAMPH` keeps **19** against a `--min-n` of 30. There is
no value of sea level, and no model of the above-water segment, that adds a
scoreable cell. The class is lost to arithmetic, not to a gap. Dropping
`--min-n` to twenty would admit `CORAMPH` at 95%, and that is precisely the move
this document refuses: a threshold chosen after seeing which cell it admits is
not evidence.

The `burst` exclusion is the one worth spelling out, because it is what turned
eight failures into two. A burst weapon fires `burst` rounds `burstrate` seconds
apart from one trigger, each thrown off the aim line by `sprayangle`, so the
isolation filter cannot mean there what it means everywhere else: the shot that
survives it is one round of several and the damage that arrives need not be its
own. Six of the eight cells that failed the model before the class existed are
burst weapons -- both flamethrowers, both `EMG`s, `EMG_VTOL`, `GAUSS_SPRAY` --
excluded on a criterion that has nothing to do with flight time. The seventh is
`CANNON_FIDO`, which is `ballistic` as well and had been classed by that;
`burst` is asked first now, because a burst weapon's isolation filter fails
whatever shape its rounds fly.

#### What a burst cell's spread actually is: a comb

One correction to the sentence above, and then the measurement. The rounds of a
burst are **not** each their own `0x0d`. `0x49CB79` makes the record a
*template* that never flies, the projectile pass spawns one copy per `burstrate`
and appends it past the trip count it had already latched (`0x49B810`), and only
the trigger emits a `0x0d`. So one shot record stands for `burst` rounds, which
is also why such a shot passes the isolation filter at all -- there is only one
of it -- and why "several damage events in the window" throws so many away.

A copy therefore first moves on `1 + j*burstrate` ticks after the trigger, and a
cell's delta is drawn from a **comb** of `burst` teeth rather than from a single
value. `--unmodelled` prints it:

| cell | weapon | burst | teeth | sprayangle | deltas on a tooth |
|---|---|---|---|---|---|
| `ARMFIDO` | `CANNON_FIDO` | 6 | +1..+6 | 1536 | 180/323 (56%) |
| `CORPYRO` | `FLAMETHROWER` | 10 | +1..+10 | 1024 | 91/112 (**81%**) |
| `CORAFAV` | `FLAMETHROWER_HVY` | 5 | +1..+5 | 1536 | 12/60 (20%) |
| `ARMBRAWL` | `EMG_VTOL` | 3 | +1..+7 | 1024 | 6/38 (16%) |
| `ARMWAR` | `GAUSS_SPRAY` | 3 | +1..+3 | 3072 | 30/35 (**86%**) |
| `ARMFLASH` | `EMG` | 3 | +1..+7 | 1024 | 11/32 (34%) |
| `ARMPW` | `EMG` | 3 | +1..+7 | 1024 | 9/30 (30%) |

Where the comb is short and the spray is not the dominant term it is plainly
visible: `GAUSS_SPRAY` puts 86% of its deltas on its three teeth and does it in
the decaying order later rounds predict (+1:15, +2:10, +3:5), which is what a
single surviving hit out of three should look like. Where the comb is long it is
not, `CANNON_FIDO` spreading over twenty values against six teeth -- because
`sprayangle` throws each copy off the aim line the model measures, and for a
shell that becomes a range error by the same cotangent the aim cone does.

So this is not a flight-time model with a residual to chase. **The quantity the
filters isolate here is not a flight time**, and no amount of modelling the
round's flight will make it one; what would be needed is a way to say which
round of a burst drew the damage, and nothing in the stream carries it.

**The four exceptions the aim-point model carried all land on the footprint
model**, and `KNOWN_EXCEPTIONS` is empty. Each read one tick low, and how firmly
each now lands is worth keeping apart:

| cell | aim point | footprint | the runner-up now |
|---|---|---|---|
| `ARMAMPH`, `GAUSS_MAV` (221) | -1 at 45% | **+0 at 71%** | -1 at 15% |
| `CORVAMP`, `MISSILE_VTOL_GF` (44) | -1 at 41% | **+0 at 66%** | -1 at 32% |
| `CORGEO`, `RIOT_ALL` (53) | -1 at 45% | +0 at 47% | -1 at 38% |
| `ARMFIG`, `MISSILE_VTOL` (262) | -1 at 43% | +0 at 41% | +1 at 35% |

`ARMAMPH` is explained outright, and so is why it differed from `ARMMAV` firing
the same weapon: its victims are bigger, a mean footprint side of 3.0 against
2.3, so a round stopped on the footprint arrives earlier against the aim point.
`CORVAMP` lands clearly too. `CORGEO` and `ARMFIG` land as near-ties, and
`ARMFIG` still sits twenty points under `CORVENG` -- that is the open question
under "Where a round stops", not a closed one. The exit code still covers a new
disagreement, and a cell named in `KNOWN_EXCEPTIONS` that stops reading what it
read, exactly as before.

**Mutations, and what they moved.** Each was predicted before it was run, by
replaying the checked-in episodes under the model with the one change, and each
moved exactly the predicted episodes and no others:

* stamping a unit's footprint with its edge **truncated** rather than rounded to
  the nearest square (`computeFootprintRegion`) fails **15 of 40**, the fifteen
  whose aim point sits where the two rules pick different squares -- ten
  constant-speed, four with a motor and `CORMORT` alone of the two shells,
  `ARMBULL`'s aim point falling in the same square under either rule, and
  `CORHRK`'s too;
* testing the occupied grid at the round's position **before** its move rather
  than after fails **all 37** (measured before the shell class existed);
* a motor that **never accelerates** fails **10 of the 13** motor episodes and
  none of the 24 others;
* taking the **high root** of the firing solution instead of the flat one
  (`pitches->first`) fails **exactly the 2 shell episodes** and none of the other
  37 -- `ARMBULL` reads 116 ticks against 23 and `CORMORT` 170 against 11,
  because the high root is near the vertical and its cosine is almost nothing;
* **halving the per-tick gravity in the ballistic branch of
  `updateProjectiles`** fails **none of the 39**, which is the sharpest statement
  of what the shell model claims: the launch angle decides the flight time and
  the fall does not touch it. It does fail `wind.test.cpp`'s ballistic case,
  which is watching the height it changes.

Three more came with the `cruise` episode, and the first two are the ones that
say what it does *not* pin:

* turning **`p.cruise` on** for every episode in `weaponflight.test.cpp` fails
  **none of the 40**, because the clause is read only from the guidance step and
  no episode's weapon has `guidance`;
* turning **`p.cruise`, `p.guidance` and a wide `p.turnRate` on** together fails
  **none of the 40** either -- every episode in the fixture is closer to its aim
  point than the 1024-unit handover, so the cruise aim point is never the one
  returned, and steering at a point already dead ahead is an identity. **The
  corpus cannot tell a cruise round from a straight one**, which is the honest
  limit of the fortieth episode and the reason the class carries the "cruise
  steering" guard instead of relying on this fixture to catch a steering one;
* making every **self-propelled round one world unit a tick slower** fails
  **10 of 40** -- `ARMAABOT`, `ARMFIG`, `ARMROCK`, `ARMSAM`, `ARMSNIPE`,
  `CORCRASH`, **`CORHRK`**, `CORMANT`, `CORSTORM`, `CORVAMP` -- and nothing
  without a motor. That is the one that says the new episode is live rather
  than inert, which the two above could not, and it is why it had to be run:
  `CORHRK` has no acceleration and an aim point that lands in the same square
  under either stamping rule, so neither of the older motor mutations touches
  it.

### A shell: the flat root, and the cosine that falls out of it

The ballistic cells had no model until 2026-09-18, and the one they have now is
shorter than expected, because most of the arc turns out not to matter.

**How far a shell goes in a tick.** `createProjectileFromWeapon` launches it at
`direction * weaponvelocity`, with `direction` rebuilt from the heading and pitch
the fire routine solved, and the ballistic branch of `updateProjectiles` then
touches **only `velocity.y`**. So the horizontal half of that launch vector never
changes: the round covers `weaponvelocity / 30 * cos(pitch)` world units a tick,
for ever, and the footprint test is horizontal. **Gravity enters a flight time
only through the launch angle.** That is what the gravity mutation above
demonstrates, and it is why this model is a cosine rather than a replay.

**Which pitch.** The flat root, always. `0x49A890` solves the standard ballistic
quadratic -- the same discriminant RWE's `computeFiringAngles` forms, arrived at
by a different factoring -- and chooses between the roots at `0x49AA11` against
`minbarrelangle` as a floor and **π/4** as a ceiling. The high root exceeds 45°
for every target inside the gun's maximum range and equals it only at the range
itself, so the ceiling rejects it every time: there is no lofted artillery arc in
Total Annihilation, and RWE's `pitches->second` is the same choice. See
[TOTALA-EXE.md](TOTALA-EXE.md), "The ballistic firing solution". A geometry with
no solution is a shot that never happened (`cmp ax,0x8000` at `0x49D61B`).

The wind is the one term left out. `0x49BD10` adds the map's vector to a
ballistic round's position every tick, a demo does not record it, and at a
typical map's `maxwindspeed` of 3000 it is 0.09 world units a tick -- about six
units over a Crusader's whole flight.

#### The aim cone, which is why there are two shell cells and not seventeen

The model above is not what limits this class. The original perturbs **every
turret shot** before it spawns the round (`0x49D6D7`): `heading += rand(acc) -
acc/2` and `pitch += rand(acc) - acc/2`, where `acc` is the weapon's `accuracy`
widened by however hurt the shooter is and narrowed by its kills, with
`sprayangle` a second draw on the heading. Every ballistic weapon in the data set
but three is `turret=1`.

For a round that flies level a pitch error of δ costs `tan(pitch)·δ` of the
horizontal speed, under one percent for anything the other two classes fire. For
a shell it is a **range** error of `2·cot(2·pitch)·δ`, because the range goes as
`sin(2·pitch)`: at `CANNON_ART_MEDIUM`'s `accuracy=750` and an 18° elevation that
is ten percent of the flight, six or seven ticks on a sixty-five-tick shell. The
draw is nowhere in the stream.

So a ballistic pairing is scored only where **the jitter cannot move the
answer**: the whole arc is replayed at the corners of the weapon's own cone, and
the pairing is kept only if every corner gives the same step. There is nothing in
that to tune -- it is the weapon's declared `accuracy` run through the original's
own formula. `tools/tad-weapontime.py --cone` prints the split it makes:

| the drift-bounded ballistic pairings | n | at +0 |
|---|---|---|
| the aim cone **cannot** move the answer | 227 | **42%** |
| it can | 389 | 17% |

The replay follows the round in y as well as x and z for one reason the flight
model does not need: a shell the jitter sends into the ground **short** of its
victim was not stopped by the footprint at all, and its interval is measuring a
blast radius rather than an arrival. That clause is what rejects `ARMVULC`, whose
shells are independently seen to detonate thirty to fifty units *below* their aim
point and outside the footprint entirely.

**It is a lower bound on the jitter, not the whole of it.** The health term opens
the cone for a damaged shooter and a demo does not carry hit points, so a pairing
this bound keeps may still have been fired through a wider cone than the weapon
asked for. That is most of what is left between the 42% here and the 98% the
constant-speed class reaches at drift zero.

#### What the class leaves, and what it does not

**Two of the seventeen cells are scoreable at `--min-n 30`, and both land on the
model**: `CORMORT` (`CANNON_MORT`, +0 over 38 pairings, 42% at the mode) and
`ARMBULL` (`CANNON_BULL`, +0 over 36, 33%). They are the fixture's two shell
episodes. The other fifteen are **printed with what took them** rather than
dropped -- the script prints the table below whenever it runs -- because a cell
that cannot be scored is a result:

| | named | drift | cone | left |
|---|---|---|---|---|
| `ARMMART`, `CANNON_ART_MEDIUM` | 1463 | 1339 | 122 | 2 |
| `CORMART`, `CANNON_ART_MEDIUM` | 1055 | 969 | 85 | 1 |
| `CORGOL`, `CANNON_GOL` | 367 | 338 | 20 | 9 |
| `CORTHUD`, `CANNON_ART_LIGHT` | 269 | 218 | 24 | 27 |
| `ARMHAM`, `CANNON_ART_LIGHT` | 183 | 149 | 24 | 10 |
| `ARMSTUMP`, `CANNON_TANK_LIGHT` | 136 | 113 | 0 | 23 |
| `CORRAID`, `CANNON_TANK_LIGHT` | 113 | 94 | 0 | 19 |
| `ARMVULC`, `CANNON_LRPC_ARM` | 35 | 0 | 32 | 3 |

(and seven more; `drift` and `cone` are how many each bound took.) The shape is
worth reading. The **artillery** cells, which have by far the most pairings in
the corpus, lose nearly everything to the cone, because their `accuracy` is the
widest in the data set. The **tank cannons**, whose cone is zero at full health,
lose theirs to the drift bound instead: a shell spends thirty to sixty ticks in
the air where a laser spends six, so a bound that costs the constant-speed class
nothing costs this one almost everything. Nine of the fifteen would land at +0 if
the threshold were dropped to ten pairings; it is not dropped, because a
threshold chosen after seeing which cells it admits is not evidence.

**Two reclassifications fell out of scoring the class, and both are corrections
rather than conveniences.** `burst` is now asked **before** `ballistic`:
`CANNON_FIDO` is both -- six shells from one trigger at `burstrate=0.001`, each
thrown off the aim line by a 1536 `sprayangle` -- and it had been sitting in the
ballistic table reading +5 while really being excluded for a reason that has
nothing to do with flight time. And a weapon that is `selfprop` as well as
`ballistic` is now named rather than modelled ("ballistic selfprop"), because TA
dispatches a round's flight on `selfprop` first at `0x49B9C2`, so such a round is
flown by the motor off a ballistic launch angle -- a fourth shape. `ROCKET_HEAVY`
is the only one in this data set and nothing in the corpus fires it.

**What stays open.** The height half of the collision test was the first
explanation tried for this class's spread and it is **refuted**: replaying the
drift-bounded pairings with a victim-top clause at every height from zero to
sixty world units moves the pooled agreement by a single point, because a shell
aimed at a unit arrives well below that unit's top. The residual is the aim cone
and the shooter's hit points, not the geometry. `ARMVULC` remains genuinely
unexplained beyond "it is splash": all 35 of its pairings are against one
building type in one demo, and its shells arrive at roughly `d / (v·cos 45°)`
whatever their solved pitch says, which nothing here accounts for.

#### Why 53,706 shots drew no damage, and what that means for a hit/miss oracle

The pairing above keeps 35,535 shots. The largest rejection that is not simply
sustained fire is **53,706 that drew no damage in the window at all**, and the
hit/miss half of the oracle was blocked on them: `0x0b` is not a complete
ledger, so silence is unknown rather than zero and no shot may be called a miss
on it alone. `tad_episodes --miss-buckets` is the re-runnable answer.

```bash
./build/tad_episodes --dir ~/ta-demos --units ~/ta-mods/x-esc --miss-buckets
```

**The buckets**, in the priority order a shot is tested against. A shot lands in
the first that fits, so the counts sum to the 53,706 and nothing is claimed
twice.

| bucket | shots | share |
|---|---|---|
| 1. the slot names no weapon, or one that cannot fly a round | 1,068 | 2.0% |
| 2. the recording ends inside the window | 192 | 0.4% |
| 3. the victim died before the round could arrive | **13,991** | **26.1%** |
| 4. the shot killed the victim; only the `0x0c` records it | 47 | 0.1% |
| 5. the round damaged a bystander instead | 3,381 | 6.3% |
| 6. the victim cannot be named, or has no footprint to occupy | 153 | 0.3% |
| 7. the round steps over the victim's footprint | 117 | 0.2% |
| 8. the damage arrived just outside the window | 375 | 0.7% |
| 9. the victim could outrun the round | **30,185** | **56.2%** |
| 10. **unexplained**: the victim could not outrun the round | **4,197** | **7.8%** |

Buckets 1 to 8 are structural: the round had nothing to hit, or it hit something
else, or the record it drew is outside what the filter looked at. Together they
are **19,324 shots, 36.0%**, and the largest of them is worth spelling out
because it is a fact about the engine rather than about the recording. **A round
in flight when its victim dies flies on.** `0x49B090` reads the square's unit
slots, and by the time the round gets there the slot is empty, so there is
nothing to damage and no record to emit. That accounts for a quarter of the
53,706 on its own, and it is corroborated by an instrument that knows nothing
about the model: of the brackets the health test below has to throw away because
the victim's `0x2c` slot changed type or skipped a cycle, **32,524 of 32,528
carry a `0x0c` for that victim inside them**. A victim that vanishes from the
`0x2c` stream is a victim that died.

Bucket 9 uses the **same drift bound the missile class already uses** -- the
victim could travel further than one step of the projectile while the round was
in the air -- so it is not a bound chosen here, and buckets 9 and 10 are the two
halves of what is left rather than an explanation and a residue.

**The drift axis is what says these are misses.** If a no-damage shot were the
ledger dropping a record, its rate would have no reason to depend on how fast
the victim can move. It depends on nothing else:

| drift, world units | isolated shots | no damage | rate |
|---|---|---|---|
| 0 (immobile victim) | 4,954 | 1,892 | 38% |
| 0-8 | 3,088 | 428 | **14%** |
| 8-16 | 5,853 | 1,532 | 26% |
| 16-32 | 15,296 | 5,568 | 36% |
| 32-64 | 20,232 | 10,810 | 53% |
| 64-128 | 16,637 | 12,660 | 76% |
| 128-256 | 12,126 | 9,078 | 75% |
| 256+ | 13,612 | 10,505 | **77%** |

Fourteen per cent to seventy-seven, monotone but for the last pair, over an axis
the ledger cannot see. The population it is drawn from is the same one the
flight-time table is drawn from, which is why the two can be read together: the
rows where a flight time lands on its model are the rows where a shot draws
damage. The immobile row at 38% sits above the 0-8 row and is the one anomaly;
it is artillery, which arcs. `--miss-buckets` prints the same rate split by
weapon class and victim kind, and there a constant-speed round at an immobile
victim reads **26.5%** against a ballistic one's **48.7%** at the same victim.

**The `0x2c` health bracket is the direct test**, and it is the one that settles
whether the remainder are misses or gaps. A unit's full state goes out once
every `maxUnits` ticks, so a shot can be bracketed by the victim's own records
either side of it. The bracket is used only where it is clean -- the same type at
both ends, a finished unit, the unit id not recycled inside it (a `0x0c` for that
id disqualifies it), the round's arrival inside it, and **no damage record
against that victim anywhere in it**. Then the health difference is the whole
truth about what the victim absorbed and the ledger says nothing happened.

The control comes first, because an instrument that cannot see a hit it is
looking at proves nothing about one it cannot find. Over shots **known** to have
drawn damage, bracketed the same way, the loss shows **781 times of 954, 82%**.
The 18% it misses are mostly small fast units at full health at both ends: a
cycle is 33 seconds, which is long enough for a repairer to put back what a
round took (§94 -- one hit point per repairer per tick), and adding the
recycling guard alone took the sensitivity from 75% to 82%.

Against that:

| | clean brackets | health flat | health fell |
|---|---|---|---|
| bucket 9 (could outrun the round) | 1,528 | **1,426 (93%)** | 102 |
| bucket 10 (could not) | 185 | **168 (91%)** | 17 |

Corrected for the instrument's 82% sensitivity, **8% of bucket 9 and 11% of
bucket 10 really lost health**, so about **90% of both are genuine misses** and
about 10% are ledger gaps. The two buckets read the same, which is the useful
part: whatever makes bucket 10's shots miss is not the victim outrunning them.

**The honest residue.** Of the 53,706: 19,324 (36.0%) are structural and named;
30,185 (56.2%) are misses the drift bound explains and the health test confirms
at about nine in ten; 4,197 (7.8%) are misses the drift bound does **not**
explain, of which the health test again says about nine in ten really are
misses. **So what is unexplained is the mechanism behind those 4,197, not their
disposition** -- they are misses, and nothing here says why. Their shape points
at things this document already has open, and `--miss-buckets` prints it by
(weapon class, victim kind). By class: **2,187** constant speed, of which 1,963
are at mobile ground victims and are the largest single cell in the residue;
**855** accelerating, **452** `vlaunch`, **447** ballistic, **202** burst,
**53** `cruise` and one torpedo.

The slice that says the most is the other axis. **1,023 of the 4,197 were fired
at a victim that cannot move at all**, so no amount of drift is available to
explain them and the round was sent at a footprint that was still there when it
arrived. Those are where the two unmodelled pieces of `0x49B090` have to be:
351 of them are ballistic and 211 `vlaunch`, neither of which flies the straight
line every model here measures, and the **height** half -- a round passing over
a victim's top, which nothing scores and which the flight-time fixture
deliberately makes impossible by building its victims too tall -- would show
exactly here and nowhere else. The remaining 3,174 are at victims that can move
but not far enough to clear one step of the round, where the bound is a worst
case from `maxvelocity` rather than an observation; the zero-drift filter the
`0x2c` path stream could supply, and which has never been built, is what would
sharpen those.

**And the ledger gaps are real but small.** 119 shots across both open buckets
have a clean bracket in which the victim demonstrably lost health with nothing
in the ledger at all. As a share of the 1,713 clean brackets that is 6.9%, or
8.5% once the instrument's own 82% sensitivity is allowed for. Whether that
share holds over the 34,382 shots the brackets are sampled from is the weakest
number in this section and should be read as an order of magnitude rather than
an estimate -- a clean bracket needs a quiet 33 seconds and a surviving victim,
which is not a fair sample of a firefight. What the 119 do establish without any
extrapolation is that `0x0b` drops records **for units that live**, where the
older figure (17,526 of 56,913 deaths carrying no damage at all) only ever
measured it on units that died.

**Two things the pass found on the way that are not about misses.**

*The `0x0d`'s rotation triple is the shot's own launch attitude.* `--emit-shots`
now carries it. `ry` against `atan2(dx, dz)` over the aim line has a circular
correlation of **0.984** with a constant offset of half a circle, so it is a
yaw; `rx` never leaves -494..182 and tracks the aim line's elevation, so it is a
pitch. The yaw is not the aim bearing exactly -- its departure has a median of
**1.8 degrees** and a 90th percentile of 7 -- so it records a real aiming error,
which is the first thing in the stream that does. But it does **not** decide a
shot: bucketing isolated shots by that departure gives a no-damage rate flat at
50-55% across every bucket from a sixteenth of a degree to thirty. Flying the
round along the recorded yaw instead of along the aim line does separate them,
and that is the strongest single geometric signal found -- 70% of the
damage-drawing shots still enter the victim's footprint against 50% of the
damageless ones -- but half of each is on the wrong side, so it classifies a
population and not a shot.

*Fourteen weapon blocks declare a negative `weaponvelocity`* -- `BOMB_SHOCK`
-400, `VSPAM_ALL` -10, `NUKE_SUB_ARM` -8 and friends. `WeaponFacts` holds the
field unsigned because `parseWeaponTdf` does, so the sign wraps to a value above
`INT32_MAX` and any bound computed from it is nonsense. Every one of the
fourteen is `vlaunch`, which no model here flies and which the flight-time cells
never score, so nothing checked in moves -- but it was worth 311 shots in this
pass before the guard, and it is the whole of what the port and the reference
script disagreed about. It was found by diffing the two per shot rather than by
argument, which is the method this document keeps recommending.

**And it is not a speed.** That guard treats the value as unusable, which is
right, but what the original does with it is worth knowing before anything ever
tries to fly one. The motor's two comparisons are at `0x49BA1E` and `0x49BA2D`:

```
49ba1e  cmp eax,ecx ; jae ...   ; already at the cap, leave it
49ba2d  cmp eax,ecx ; jbe ...   ; still under it, skip the clamp
```

`jae` and `jbe` are **unsigned**. So a negative cap is compared as a number near
2^32, the speed is never at or above it, and the clamp can never fire. Its whole
effect is to **disable the ceiling** -- which is exactly what these blocks want,
because every one of them also carries a *negative* `weaponacceleration`
alongside a large positive `startvelocity`. `VSPAM_ALL` is the pattern:
`startvelocity=480`, `weaponacceleration=-75`, `weaponvelocity=-10`. It is a
**decelerating missile**, 16 world units a tick off the rail and losing 0.083 a
tick for the sixteen ticks its motor burns, and disabling the ceiling is the
only way TA lets a mod write one down.

`BOMB_MS` and `BOMB_SHOCK` are the other shape: `startvelocity` is negative too
and *equal* to the cap, so the first comparison takes its `jae` and the speed
never moves at all. The round flies **backward** along its nose at 13.3 units a
tick -- and since `0x49CC20` points a vertical launch's nose straight up, that
is straight down. A bomb, written as a negative-velocity vertical launch.

Neither shape is scored here and neither is ported. What this settles is that
the guard is dropping a *meaningful* value rather than a corrupt one, so a
future pass that wants these fourteen weapons has something to implement and not
merely a sign to fix.

**So: is the hit/miss oracle feasible?** Yes, and not as the statistic it was
first imagined to be. Three things constrain it.

* **It can only ever be a rate, never a per-shot prediction.** The `0x0d`
  records where the shot was *aimed* -- and, in the rotation triple, roughly
  where it was pointed -- but whether it connected depends on where the victim
  was when it arrived, and the victim's position is in the stream once every
  `maxUnits` ticks. A cell's hit *share* is measurable; "did this shot hit" is
  not.
* **It must be scored over shots the structural buckets clear.** A cell computed
  over raw silence would be measuring how often the victim died first, which is
  a quarter of the corpus and has nothing to do with the weapon.
* **Target type belongs in the cell key, and the flight-time cells' key does
  not.** A flight time depends on the weapon and the geometry, so
  (shooter type, weapon slot) is enough. A hit rate depends on the victim as
  much as on the weapon, and by more than it depends on the weapon. Holding the
  weapon class and varying the victim moves the damageless rate further than
  holding the victim and varying the class:

  | weapon class | immobile | mobile ground | aircraft |
  |---|---|---|---|
  | constant speed | 26.5% | 33.0% | **92.1%** |
  | accelerating | 32.1% | 27.4% | 67.6% |
  | ballistic | 48.7% | **70.2%** | 82.6% |
  | burst | 29.9% | 38.9% | 83.2% |
  | `vlaunch` | 48.4% | **92.2%** | 91.5% |

  So the key wants to be **(shooter type, weapon slot, victim type)**, or
  (shooter type, weapon slot, victim class) where a type is too thin -- and the
  drift table is the argument that "immobile / mobile ground / aircraft" is the
  right coarsening, because that is the axis the rate moves on. Note what the
  table does *not* say: a ballistic round is worse against a moving ground unit
  (70.2%) than a constant-speed one is (33.0%), which is a fact about the arc
  and not about the target, so the weapon half of the key earns its place too.

What it would assert against RWE is a share, so it wants an interval rather than
an equality, and `expectedDurationDelta`'s analogue here is a tolerance. That is
a different shape of fixture from the three already checked in, and designing it
is the next piece of work rather than this one.

### `0x10`, script call -- all 22 bytes

`u16` unit, `u16` script index into that unit's own COB, `u8` argument count,
then four `s32` argument slots. This is the record `TOTALA-EXE.md` decodes from
the emitting side at `0x451DF0`, so the layout is transcription rather than a
reading, and the corpus agrees with it.

### `0x28`, resource statistics -- 58 bytes, 40 of them read

Seventeen bytes, then ten IEEE-754 floats, emitted about every 120 ticks per
player. The first four are settled by watching them move over whole games:

| Slot | Field | Why |
|---|---|---|
| 0 | metal stored | bounded above by slot 2, and it is the one that sits at zero for long stretches |
| 1 | energy stored | bounded above by slot 3 |
| 2 | metal storage | moves in building-sized steps: 1000, 1450, 1950 |
| 3 | energy storage | likewise: 1000, 1650, 1950 |

The remaining six are two monotonically increasing triples, **energy first and
then metal**. That order is not a guess: over a game's opening the first of each
triple grows at 50 a second and 2 a second respectively, which is the shape of a
commander's output and not the other way about. They are cumulative counters of
something, and which of the recorder's own `lastshared`/`shared`/`income`/
`lasttotal` names each carries is not settled, so they keep positional names.

A watcher's record is a useful control and a trap. In demo 14733 the `WATCH`
side's 972 samples read zero in every field except the last of each triple, both
exactly 1000.0 -- which fits no reading taken from a playing peer. Treat a
watcher's record as uninitialised and filter it out rather than averaging it in.

#### Whose state it is: the sender's, and the burst is a fan-out

This was the standing blocker on the economy oracle, and it dissolves rather
than resolving. The record has no player id, and a sender emits exactly
`numPlayers - 1` of them on one tick, which looks exactly like a positional
table of every *other* player. It is not one. **Every copy in a burst is
identical** -- all ten floats, byte for byte -- over all 60,445 multi-copy
bursts in the thirteen-demo corpus, without a single exception. A burst is one
unicast per peer, which the recorder sees every copy of. There is nothing to
attribute: collapse the burst, credit it to its sender, and the bookkeeping is
done.

That argument rules out a positional reading on its own -- a burst carries one
state, not `numPlayers - 1` of them -- but the reading it leaves is worth
confirming on evidence it was not built from, so:

**Storage steps.** A completed building that grants storage raises its owner's
capacity. Of the 5,987 upward steps in `metalStorage`/`energyStorage` across the
corpus, 4,416 are explained by the completions of exactly one owner block in the
sampling window and **that block is the sender's own**; 53 resolve to some other
single block, and 1,256 windows admit more than one block (1,249 of which
include the sender's). 255 match nothing, which is what a build whose `0x09`
preceded the recording looks like. So the sender's record tracks the sender's
own buildings.

The one field that ever varies within a burst is **prefix byte 0**, in 19 bursts
of the 60,445. It reads 1 rather than 0 only in a game's closing seconds, and
there the records arrive every five ticks instead of every 120 -- so those are
two successive samples landing on one tick, not a per-recipient field. The
sampling interval is otherwise 120 ticks in every demo, 47,880 of 56,535 gaps
landing on exactly 120 and the rest within a few ticks of it.

`tad_episodes --emit-resources` collapses the burst and reports both halves of
this per demo: the modal burst size against `numPlayers - 1`, and a count of any
burst whose copies disagreed. That count is zero across the corpus, and it is
the number that would overturn the reading if a demo ever produced one.

**So slots 0-3 are usable now.** Storage-cap and stall episodes need only those,
and they are attributable. Recovering expenditure by difference still waits on
naming the last six.

#### What a stalled settle costs a factory, and where the settles fall

The stall half of the economy oracle. `docs/TOTALA-EXE.md` §23 has the settle
and §111 the re-reading this rests on. The rule that matters: between settles a
consumer's only gate is its own debt, only a settle writes debt, and a settle
that can pay a debt leaves it at exactly zero. So a builder granted anything in
a second whose settle falls short is refused every tick until a settle pays,
and a stall costs whole seconds.

**Which episode, and why not the stockpile path.** Two shapes were on the table.

- *The stockpile path*: predict slots 0 and 1 across a window from the player's
  composition. That needs a production model the stream cannot supply. Wind
  speed is not in it. What an extractor makes depends on the metal under its
  footprint, which is map data. Reclaim income is not recorded at all. Spending
  needs every builder's rate, the mobile builders' and the assists' included,
  plus every shot's charge. The last six floats that might carry production and
  spending are still unnamed. And a sample lands on one settle in four, so the
  three between are guessed. Not attempted: it cannot be made hermetic, and it
  would test the model of the game rather than the engine.
- *Build lateness*: what the debt rule does to a factory's build timings, with
  the `0x28` stream only as the witness that a settle stalled. The build model is
  already solved (`tools/tad-buildtime.py`), a factory has nothing to walk to or
  deploy, and the test can call the lathe and let the engine's own settle decide.
  This is the one taken.

Of 60,760 non-watcher samples over the twelve Escalation demos, 6,268 read an
empty metal store and 1,538 an empty energy store (227 both). Those are what a
stall looks like from outside.

`tools/tad-stalltime.py` is the reference. It reuses `tad-buildtime.py`'s model
and builder naming, and `tad_episodes --stall-episodes` prints the same report
line for line. It measures three things:

1. **Where the settles fall.** Every `0x28` comes from the settle pass, so its
   tick marks a settle. 60,588 of the 60,760 samples, from all 86 senders, sit
   within 6 ticks of a multiple of 30 of the demo clock, and the least aligned
   sender still has 208 of 211 there. The few ticks are how far a sender's clock
   runs behind. Settles are not staggered per player, and §111 found why: every
   player's counter starts on the same tick. Scored per sender at 95%.
2. **The quantum.** Over factory builds on the 45 cells the build model
   explains, 3,546 land on the model and 2,653 are late. **2,011 of the late ones
   are late by an exact multiple of 30**, where chance gives one in thirty. Only
   4 of the on-time builds have an empty store sampled inside them, against 1,388
   of the whole-second ones. Printed, not scored. Assists shorten a build by an
   amount nothing records, so 7,578 builds are early and no filter can clean that
   population.
3. **The episodes, scored.** A factory finishes a job in the second before a
   settle whose sample reads an empty store. It starts its next job before the
   following settle. It was granted resources in the stalled second, so it owes,
   so the new job is refused until a settle pays. The prediction is that the job
   is late by exactly `30 - start % 30` plus a whole 30 per further stalled
   settle. The residue comes only from the start tick and the settle cadence.
   Nothing about it is read from the build it predicts.

The filters, with what each took out of 138 candidates:

| Filter | Removed |
|---|---|
| The (builder, product) cell's mode is not explained by the build model | 14 |
| The builder's build before or after this one is early, so something was assisting it | 45 |
| This build is early | 1 |
| Frame or builder damaged, or a speed change | 1 |

That leaves **77 scored builds, and 73 land exactly on their residue**. The four
that do not are all *short* of it, by 19, 2, 18 and 4 ticks, and none is long. A
debt can only lengthen a job, while an assist can only shorten one, so they read
as assists the neighbour guard missed. That is a reading, not a proof, so the
script names them in `KNOWN_EXCEPTIONS` and fails if they move. They never
become episodes.

**The control is what makes the residue mean anything.** The same pattern over a
settle whose sample reads a *non*-empty store predicts the residue in **0 of
449** builds. The finish-then-start pattern alone does not produce it; the
stall does.

**The fixture.** `tad_episodes --emit-stall-cpp` writes
`src/rwe/sim/tad_stall_episodes.h`: 24 episodes, one per distinct residue. Where
several builds share a residue, the one with the fewest further stalled settles
wins, because it is the shortest replay and rests least on settles no sample saw.
They come from 6 demos and 7 owner blocks, with both an empty metal store and an
empty energy store among them. Each carries the factory's `WorkerTime` and both
jobs' `BuildTime`, `BuildCostMetal` and `BuildCostEnergy`, the stalled settle
and the sample that saw it, the model duration, the residue, and
`furtherStalledSettles`. That last one is **read from the observation**: the
corpus sees one settle in four, so how long a stall ran is taken from the build,
and what is asserted is everything else. Two episodes carry
`expectedDurationDelta = -1`, the two `CORLAB -> CORCRASH` builds, whose
`BuildTime` of 1820 divides exactly by 4 (§88, the integer accumulator). Nothing
about the settle needs a delta.

**The tests.** Three `[economy][corpus]` cases in `economy.test.cpp`. Each
replays an episode through `GameSimulation::tick` with one player per owner block
up to the episode's. The sampled store is emptied for exactly the stalled
settles and refilled after. The test calls the factory path's two calls itself
-- `GameSimulation::addResourceDelta` with the step's cost, then
`UnitState::addBuildProgress` if accepted -- once per tick, after `tick()`
returns, which is where the behaviour pass runs relative to `updateResources`.
It does not go through `UnitBehaviorService`, for the reason
`buildtime.test.cpp` gives. The settle's cadence, its phase and the debt gate
are all the engine's own. The cases assert that the job finishes on the demo's
duration plus the delta; that the first accepted tick is a multiple of 30 and
exactly `residue + 30 * furtherStalledSettles` after the start; and that the
fixture spans at least 20 residues, three owner blocks, both resources, and
deltas only on divisible pairs. All 24 passed on the first run.

**Mutations**, each run against the whole `[economy]` and `[build][corpus]` sets:

| Mutation | Episodes moved |
|---|---|
| Settle on `gameTime % 30 == 1` instead of `0` | all 24, in both the duration and the first-accepted-tick case |
| `UnitState::inResourceDebt` always false, so debt never refuses | all 24, in both |
| Settle each player on its own phase, `(gameTime + playerIndex) % 30 == 0` | 22: every episode except the two owned by block 0 (`14726` `ARMVP -> ARMFAV` at 61193, `14727` `ARMVP -> ARMFAV` at 67434) |
| Forgive the carried debt at each settle, keeping only the new shortfall | 21: every episode except the three with no further stalled settle (`14725` `ARMLAB -> ARMCK` at 105832, `14734` `ARMLAB -> ARMPW` at 63576, `14734` `CORAP -> CORFINK` at 71614) |

None of the four moved a `[build][corpus]` case or a storage episode. Each moved
exactly the subset its mechanism predicts, which says the episodes measure the
cadence, the phase, the debt gate and the debt's persistence rather than a
constant.

**What is still not assertable.** The fractions a single settle computes: how a
partial shortfall splits between debt and new asks, and how much a stalled
factory owes. A debt the next settle pays costs one second whatever its size, so
the corpus cannot see the size. That needs a sample on every settle, and the
stream gives one in four.

Regenerating (Escalation; the paths are a local corpus):

```bash
./build/tad_episodes --dir ~/ta-demos --units ~/ta-mods/x-esc \
    --emit-stall-cpp src/rwe/sim/tad_stall_episodes.h
./build/tad_episodes --dir ~/ta-demos --units ~/ta-mods/x-esc --all \
    --emit-json /tmp/ep.json --emit-resources /tmp/res.json
tools/tad-stalltime.py --episodes /tmp/ep.json --resources /tmp/res.json \
    --units ~/ta-mods/x-esc
```

The first drops the ProTA demo, 14724, because its unit table declares 317 types
against the 549 `--units` gives. The script drops it the same way, from the
`unitTypes` field `--emit-resources` now records.

### `0x2c`, unit state -- every bit, read out of `TotalA.exe`

Unlike the sections above, this one was decoded from the binary first and held
to the corpus second. **All 7,422,196 `0x2c` subpackets in the thirteen demos
decode with every bit accounted for** -- the fields end in the subpacket's last
byte, and its declared length matches -- and the decode passes every
independent check below. `tadDecodeUnitState` in `tad_events.{h,cpp}` is the
decoder; `tad_episodes --units <dir> --unit-state` re-runs the checks, and
`--emit-unit-state <path>` (with `--with-updates` for the per-tick half) dumps
the result as JSON Lines.

**The routines are unpatched in Escalation's `TotalA.exe`**:
`tools/exe/patchdiff.py --range` reports every one of `0x48B200`-`0x48BAD2`,
`0x44DDC0`, `0x44E080`, `0x44E930`, `0x44E9C0`, `0x44F480`-`0x44F564`,
`0x44F5C0`, `0x4908B0`, `0x490A10`, `0x415C10`-`0x415EF0`, `0x43DC00` and
`0x42D5F0` clean, so the GOG v3.1 reading applies to the whole corpus.

#### It is a bit stream, and what that means

The builder is `0x48B710`. It writes nothing byte-wise: every field goes through
the bit writer at `0x415C10` (reader `0x415DC0`), which packs values **least
significant bit first into little-endian 32-bit words**. Read the subpacket as one
little-endian integer and take fields off the bottom, and that is the format.
Widths are per field, not per byte, which is why nobody got anywhere staring at
the bytes: after the header nothing is aligned.

| Bits | Field | Source |
|---|---|---|
| 8 | `0x2c` | constant |
| 16 | length in bytes, patched in afterwards via `0x415DA0` | `ceil(bits/8)` |
| 32 | the sender's tick -- the serial this document already uses as the clock | `globals+0x38A47` |
| *repeat* | **one entry per unit whose mover has news**, in unit order: | `0x48B77A` loop |
| 16 | the unit's index within its owner's block, `id - (block * maxUnits + 1)` | `unit+0xA8 - player+0x6F` |
| *W* | its type index -- the same 1-based load-order index as a `0x09` | `unit+0xA6` |
| ... | the mover's own serialiser, below; which one is decided by the type | mover vtable `+0x20` |
| 16 | `0xFFFF`, end of entries | |
| 1 | always 1: a full-state record follows | `0x48B83F` |
| ... | **one unit's full state**, below | `0x48B200` |

*W* is the bit length of the unit type count (`0x42D65B` shifts it right until it
is gone): **10 bits for Escalation's 549 types, 9 for ProTA's 317**. It is the
only reason decoding needs the data set, together with the next point.

**Entries stop once the packet reaches 512 bytes** (`0x48B7F6`), so a busy tick
defers some units' news to a later one.

**Which serialiser wrote an entry is not on the wire.** `0x43DC00` builds a unit's
mover from its definition: `canfly` (`def+0x241` bit 11) gets an aircraft mover,
everything else a navigator. A reader has to know the type flies. Taking a
`CORVENG` for a ground unit does not account for its length, which is a test.

#### A ground unit's entry: the next three waypoints (`0x44F4A0`)

| Bits | Field |
|---|---|
| 1 | blocked, `mover+0x2E` bit 2 |
| 2 | waypoint count, `min(nav+0x5C, 3)`, zero when the navigator has no path |
| 16 + 16 per waypoint | x, z in **whole world units**, signed (`nav+0x0C` onward) |

This is the serialiser `TOTALA-EXE.md` §87 found. It is sent only when the
navigator's path-changed bit is set or its blocked bit has moved (`0x44F480`),
so it is a **delta**: the path a unit had until it next appears. An empty path
means it has stopped. The receiving machine's stub navigator (`0x44F570`, reader
`0x44F5C0`) steers along those three points itself -- **positions are not sent
per tick at all**.

#### An aircraft's entry: its goal (`0x4908C0`)

| Bits | Field |
|---|---|
| 2 | goal kind: 0 none, 1 a move goal, 2 a moving goal |
| ... | the goal, below |
| 2 | movement mode, `mover+0x2E & 3`: 1 landed, 2 flying |

A **move goal** (kind 1, the 0x36-byte goal of `TOTALA-EXE-MISSIONS.md`,
serialiser `0x44DDC0`) is 8 bits of its flag word and then, in this order, only
the parts whose flag is set: `0x01` a signed 16 at `goal+0x10` (not identified)
and the attached unit's 16-bit global id; `0x10` the 16-bit arrival tolerance;
`0x08` the 16-bit cruise altitude; `0x40` the 16-bit heading offset; `0x20` the
goal position as three 16.16 values. Over the corpus the goal height never
exceeds the 511 `0x44E6C0` clamps it to.

A **moving goal** (kind 2, `0x44E930`, 463 of them) is 1 flag bit, a 16.16
position, a 16.16 per-tick velocity that its resolver `0x44EA60` adds to the
position every tick, and a 16-bit heading if the flag is set.

#### The full-state record (`0x48B200`)

Every `0x2c` ends with one. Its unit is not named on the wire: it is the one
whose block index is **`tick % maxUnits`** (`0x48B835`), so a sender walks its
whole block round robin and **each unit's full state is sent once every
`maxUnits` ticks** -- 33 seconds at 1000, 50 at 1500 -- and at no other time.

| Bits | Field | Source |
|---|---|---|
| *W* | type index; **0 for an empty slot, and the record ends there** | `unit+0xA6` |
| 16 | current health | `unit+0x108` |
| 8 | build progress: 0 once complete, else `1 + trunc(254 * remaining)` | float `unit+0x104` |
| 8 | the flag byte (bit 1 armoured, bit 4 paralysed) | `unit+0x10E` |
| 2 | motion state, 2 airborne | `unit+0x110 & 3` |
| 1 | attached | `unit+0x86 != 0` |
| 15 + 8 | *if attached*: the carrier's global id, and the piece it hangs from | carrier `+0xA8`, `unit+0xF9` |
| 3 x 32 | *if not*: position, 16.16, x y z | `unit+0x6A` |
| 3 x 16 | *if not*: rotation, **sent y, z, x** | `unit+0x66`, `+0x68`, `+0x64` |
| 32 | *if not, and the unit has a mover*: current speed, 16.16 a tick | `mover+0x20` |

The speed is present exactly when the receiver's copy of the unit has a mover,
which a reader of the stream cannot know. It does not have to: the record is the
last thing in the subpacket, so padding leaves at most seven bits and a speed
leaves at least 32.

"Attached" is broader than riding a transport. Over 14725 the carriers are
mostly **factories** -- a nanoframe on the build pad, and a finished unit that
has not yet rolled off -- plus geothermal and fusion plants, which were not
looked into.

#### The evidence

Printed by `tad_episodes --units ~/ta-mods/x-esc --unit-state` over the twelve
Escalation demos (the ProTA one, run with its own `--units`, reads the same in
every row that applies):

| Check | Result |
|---|---|
| decodes, every bit accounted for | **7,309,124 of 7,309,124** (and 113,072 of 113,072 ProTA) |
| the first full-state record of a `0x09`'s slot carries the `0x09`'s type | 61,993 of 62,639 (99.0%) |
| ... and, for an immobile type, the `0x09`'s exact 16.16 position | **20,165 of 20,213 (99.8%)** |
| ... and its exact rotation, once the wire's y, z, x is put back | 19,292 of those |
| a speed is present on a type with a `MaxVelocity`, absent on one without | 446,557 and 884,045; the exceptions are 845 immobile `ARMASPEN`/`CORASPEN` pads, which have a mover |
| the speed does not exceed 1.1 x the FBI `MaxVelocity` | 446,497 of 446,557 |
| an immobile unit has not moved between two records one cycle apart | 864,713 of 864,840 |
| a mobile one has moved no further than `MaxVelocity` allows | 407,370 of 408,274 |
| a `0x0d` aimed at an immobile unit aims at its record's position | **6,236 exactly, 2,311 within 8**, 913 further, of 9,460 |
| a ground unit's first waypoint is within 32 units of where a `0x0d` puts that unit within 10 ticks | **150,341 of 208,338 (72%)**, 97% within 128 |
| ... the same test against another unit's waypoint | 35 of 208,338 |

The type mismatches after a `0x09` are most likely an id recycled inside one
cycle, and the rotation mismatches are all yaw alone, mostly on wind generators
and metal extractors, which reads as the building turning after it was placed;
neither was chased further. On bounds: the header carries no map size and the
maps are not on disk, so only a range check was possible. Every full-state
position has non-negative x and z and stays inside a map-sized range (the largest
is 14,357 by 8,706 on Iron Isle), except 292 records below zero -- and every one
of those is an aircraft whose motion state reads airborne, flying off the edge.

#### What it does and does not carry

**What a unit's state on the wire is:** its intentions every tick it changes
them, and its actual state once a cycle. A ground unit's position, heading and
speed are observable every `maxUnits` ticks and otherwise only as the path it is
following. Velocity is never sent; nor are weapon state, targets, orders or
script state -- those travel as `0x0d`, `0x10` and friends. That is exactly the
shape an owner-authoritative engine with dead reckoning would have, which is what
the rest of this document already says TA is.

**So the kinematic corpus this document hoped for is not there.** Acceleration,
turn rate and braking cannot be fitted to samples 33 seconds apart. What is
there for movement is the replicated *path* -- the first three waypoints of every
route every unit took, which is a direct view of the pathfinder's output and
could become a pathfinding oracle -- and the per-cycle speed, which is a clean
sample of "how fast was this unit going" at a known instant.

**What it opens for the weapon oracle.** *The drift bound*: the zero-drift row of
that table assumed an immobile victim's aim point is its position; it now
demonstrably is, to the bit, in two thirds of cases. For a mobile victim the
bound is still a worst case from `maxvelocity`, but the path stream supplies an
observed alternative: a ground victim whose last path update before the shot
carried **no waypoints**, with none between the shot and the damage, was standing
still for the whole flight. That is a zero-drift filter over mobile victims, and
it does not cost the missile class its fast targets by assumption. It has not
been built. It does not help against aircraft, whose entries are goals rather
than positions. The footprint residual, which this section left as the only
reading remaining, has since been settled that way: see "Where a round stops".
*Hit or miss*: health is sampled once a cycle, so a victim's health across the two records bracketing a
shot, against the `0x0b` damage recorded between them, is a direct test of how
incomplete `0x0b` is and of whether a shot with no damage record really missed
-- but only at 33-second resolution, so only for victims hit by little else in
that window. **Built**, and it is what unblocked the hit/miss half: see "Why
53,706 shots drew no damage". Three things about it that were not obvious in
advance and are worth knowing before reusing the bracket. A unit id that dies
inside a bracket is a different unit at the far end even when the type reads the
same, because TA rebuilds `ARMFAV`s and `ARMPEEP`s into their own slots
constantly, and adding that one guard took the instrument's sensitivity from 75%
to 82%. The 18% it still misses is mostly a repairer putting back what a round
took, which a 33-second cycle is ample for. And the brackets that have to be
thrown away are not a random loss: **32,524 of the 32,528** discarded for a
changed type or a skipped cycle carry a `0x0c` for that victim inside them, so
the instrument is blind to exactly the shots whose victim died -- which is why
that case needs a bucket of its own rather than a health test.

**For the economy stall work** it adds little: a nanoframe's build progress is
sampled once a cycle, which is coarser than `0x28`'s 120 ticks.

**For puppet playback** it settles the design: the original itself does not send
positions per tick, so a player has to steer each unit along its replicated
waypoints (or toward its goal) and correct at each full-state record, which is
what the receiving TA does.

**Stream coverage.** `0x2c` is 60.7% of the subpackets and **69.6% of the bytes**.
With it, the fully decoded codes (`0x09`, `0x0c`, `0x0d`, `0x10`, `0x12`, `0x2c`)
account for 81.8% of subpackets and 88.1% of bytes, and 98.2% of bytes once the
partly read `0x0b` and `0x28` are included.

### Owner blocks, which are the cheap filter

Unit ids partition into contiguous blocks of `maxUnits`, numbered from zero, with
id zero meaning no unit -- so `(id - 1) / maxUnits` is the owner. Verified over
the corpus: in every demo each sender's units fall in exactly one block and no
two senders share one, including the ten-player demo 14727, which uses all ten.

Two cautions. `maxUnits` really does vary -- demo 14724 is a 1500-unit game, so
the arithmetic cannot be hardcoded to 1000. And the block is **not** the player
number: in demo 14733, sender 1 owns block 2 and sender 3 owns block 1. It is a
stable per-player key, which is all the filters need -- "is this event about one
of the recording peer's own units" is a comparison of blocks.

## The `0x1a` unit table, and what it can and cannot tell you

The `UnitData` record between the player status records and the packet stream is
a flat array of 14-byte entries, `1a <sub> <u32 zero> <u32 id> <u32 value>`: every
entry of `sub` 2, then every entry of `sub` 3, each sorted ascending by id.

The `sub` 3 block is **exactly the data set's unit count** -- 317 for ProTA 4.8
and 549 in every TA: Escalation 10.2 demo, both matching each mod's own published
figure. TA Demo Recorder's `unitid.txt` describes these ids as "the number saved
in a unit restrictions file", which fits.

**`value` is not one number.** The packet builder at `0x46d630` in `TotalA.exe`
assembles it from three separate places in its source record:

```
46d650  mov  edx,[eax+0x0]   ; buf[6:10]  = id
46d656  mov  dl,[eax+0x8]    ; buf[10]    = value's low byte
46d65d  mov  dl,[eax+0xa]    ; buf[11]    = value's second byte
46d660  mov  ax,[eax+0xc]    ; buf[12:14] = value's top word
```

It only looks like a dword because of how it is packed, and that decode exactly
accounts for what the corpus shows. In the `sub` 3 block the low byte is always
1, the second byte is a flag, and the top word is a `0xffff` sentinel -- so
`0xffff0101` on every entry but one, in every demo. `buf[2:6]`, the "zero" field,
is never written by this routine at all; it is whatever the caller left in the
scratch buffer.

**The exception is the same id in every demo of both data sets.** Id
`2455016279` (`0x92549357`) reads `0xffff0001` -- the flag byte cleared -- and it
is the *only* id ProTA's 317 and Escalation's 549 have in common. An id shared
between two unrelated data sets cannot be derived from either's unit files, so
this is a fixed pseudo-entry and not a unit. In demo 14735 its top word reads
1000 rather than the sentinel, which is the shape of a limit rather than a flag.

The `sub` 2 block is always a **superset**: 550 entries in demos 14727 and 14728
and 551 in 14732 and 14734, the extra ids being `1235944411` and `410801334` in
both cases. That difference is not explained.

**The id is content-derived and has not been reproduced.** Two passes have now
failed at it, and between them they have ruled out a lot:

- 88 name-hash combinations -- crc32 plain and complemented, djb2, djb2-xor,
  sdbm, FNV-1, FNV-1a, java-31, rotate-xor, byte sum and adler32, over upper and
  lower case, with and without a `.fbi` suffix and a `units\` path prefix --
  against both mods' real unit-name sets. Nothing.
- crc32 and adler32 of the **raw FBI bytes** as shipped, with `\r` stripped, and
  upper- and lower-cased; of a **canonical `key=value;` serialisation** with keys
  case-folded and sorted and comments stripped, which should survive exactly the
  cosmetic differences that would otherwise explain a text hash failing; of the
  **referenced `.3do` and `.cob` files**; and of every 1-, 2- and 3-field
  permutation of `UnitName`, `Objectname`, the build costs, `MaxDamage`,
  `BuildTime`, `WorkerTime`, `Side` and `UnitNumber` under four separators.
  Nothing, against either data set's real table.

`tools/exe/unitsync.py` runs those candidates against a ground-truth CSV so the
negative is reproducible rather than asserted, and so a new candidate can be
checked against real data before anyone believes it.

Where the trail stops is specific and worth writing down. The `0x46d630` decode
above says `id` is a dword at offset 0 of a small per-unit-type record, and that
record is **not** the 585-byte FBI-parse struct -- the first thing written to a
freshly indexed one of those is its own table index at `+0x21e`, and nothing in
the per-unit field-read block writes a checksum-shaped value to offset 0. So the
source is a separate transient structure built for the lobby exchange, and its
construction site was not found: `0x46d630` has no call site that either an
absolute-address search or a grep of the resolved disassembly can see, and
objdump's linear sweep demonstrably desynchronises on a jump table at `0x46d84c`
in the same neighbourhood, which is the likely reason. A recursive-descent
disassembly of `0x455000`-`0x46e000`, or a live breakpoint on the
restrictions-dialog arrays, is the way in next time.

**But it identifies a data set today, which was the question that mattered.**
`tad_probe` now prints the table's size and an order-independent fingerprint of
the `sub` 3 block, and one fingerprint covers all twelve Escalation demos while
another covers the ProTA one. That is the mod filter the corpus section below
asks for, arrived at without the checksum: a table whose fingerprint is not
recognised is a data set we do not hold, and its episodes are not usable.

**And naming a unit type no longer needs it either.** That was the reason the
checksum was chased in the first place, and it was a false lead twice over: a
`0x09`'s type index is not an index into this table at all, and the load order
that it *is* an index into was recovered from the corpus without touching the
binary. See the `0x09` section above. The checksum is now wanted only for its
own sake -- reproducing the table, rather than reading a demo.

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
  at the missile motor model and the ballistics work. The shot names its
  **weapon** and not just its shooter, since the trailing byte is the slot.
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

### What an episode looks like, and the conventions it sets

Settled by the economy oracle, and inherited by everything after it.

- **A generated header of plain structs**, checked in beside the test that uses
  it: `src/rwe/sim/tad_economy_episodes.h`, and now
  `src/rwe/sim/tad_build_episodes.h` beside it. Small, text, diffable. **One
  header per oracle**, not one shared table: the two share no struct, are mined
  by different passes -- storage per demo, build timing corpus-wide, because a
  build-timing cell pools across games -- and are regenerated from different
  corpora, so neither regeneration should churn the other's diff.
- **The data set's own FBI values transcribed inline**, beside the observation
  they explain. `rwe_test` never opens a file, and demos and mod files never
  enter the repository, so an episode that needed either would not be a test.
  `tad_episodes` reads them with the engine's own `parseUnitFbi`, so a fixture
  cannot disagree with the loader about what a field means.
- **Provenance**: the demo, the owner block, the sample tick and the sample
  before it. The pair of ticks is the window a failure has to be explained
  inside.
- **Regeneration is diff-stable.** Episodes are emitted in (demo, owner block,
  tick) order, compositions in unit-name order, floats at nine significant
  digits so they read back bit for bit, and nothing in the output records a
  time or a path. Two runs over one corpus, in either argument order, produce
  byte-identical files; the tool reads the old file back and prints `unchanged`
  or `CHANGED` so a regeneration says which it was.
- **An expected-difference annotation on every episode.** Each carries a delta
  and the name of the `docs/TOTALA-EXE.md` §88 entry that licences it, and the
  tests assert the observation **plus** the delta. A test that asserts equality
  gets disabled the first time it is right to fail. No storage episode needs one
  -- §88's staggered settle does not move a capacity -- but the build-timing
  ones do: ten of the twenty-five carry `-1`, for the integer accumulator §88
  keeps on purpose. That is what the field was put there for, and why it was put
  there before anything needed it.

  A storage delta is hand-written into the emitter's table, because nothing can
  derive it. A build-timing delta is computed, because both completion models
  are small enough to replay and the emitter replays them. That is not circular:
  a case asserting mode + delta still fails if the engine stops matching its own
  model, and it fails on exactly the ten -- and no others -- if the engine is
  changed to the original's float.
- **Variety, not volume.** The emitter keeps one episode per distinct set of
  storage-granting types: two episodes with the same set assert the same thing.
  What the survivor also owns rides along in its composition, so the types that
  grant nothing are still checked to grant nothing.

  A build-timing cell already **is** an aggregate, so there the rule is one
  episode per scored cell, capped by `--max-cells`, and the cap prefers the
  cells where the two completion models part company: all sixteen whose
  `BuildTime` divides exactly by the rate, because those carry the deltas, then
  the most-observed of the rest, because a mode over 249 builds is a stronger
  observation than one over 5. Provenance for a mode is not a single tick: the
  episode carries the builder and product names, how many builds the cell pooled
  and how many landed on the mode, and the tick range of one representative
  build out of one of the games it pooled over.

- **Naming a unit by its id is lossy, and the name is scoped in time because of
  it.** TA recycles unit ids heavily -- in 14725, 2,622 of 4,093 distinct ids are
  reused by a later nanoframe and 2,550 of those by a *different* type. So
  `cells()` in `tools/tad-buildtime.py`, and the port that follows it, name a
  builder by the **most recent build of that id to finish before this build
  started**, not by the first name the id ever held.

  **It buys builds and it moved no scored mode.** The unscoped map was measured
  against the scoped one over the whole corpus before it was replaced: every one
  of the 41 cells kept its mode and its delta, while the builds behind them
  roughly double (`ARMVP -> ARMFAV` 151 to 363, `ARMLAB -> ARMPW` 152 to 256,
  `CORAP -> CORFINK` 69 to 115, `ARMAAP -> ARMPNIX` 8 to 27) and four more cells
  clear `--min-builds` (`ARMAVP -> ARMLATNK`, `ARMASY -> ARMSUBK`,
  `ARMHP -> ARMLH`, `CORVP -> CORCV`), taking the scored set from 41 to 45. Nine
  cells did move, and every one of them has a ground-mobile builder, which is
  never scored.

  The modes survived because a stale name is wrong in only two ways: if its
  `WorkerTime` differs the build misses the outlier cap and is dropped rather
  than miscounted, and if it matches -- every stock factory is `p = 4` -- the
  build lands in the wrong cell with an identical duration, because only `p` and
  the product's `BuildTime` enter the arithmetic. **That is luck a weapon oracle
  will not have**, since a weapon's behaviour depends on the weapon and not on
  one shared integer. Changing this meant changing the script first, because the
  script is the reference and the port is checked against it.

- **The wrong data set excludes itself.** A build-timing cell pools across
  demos, so one recording made on another mod would name its types out of the
  wrong load order and merge into somebody else's cells. The demo's own `0x1a`
  table carries its type count, so a disagreement with `--units` drops that demo
  from the cells rather than printing a warning and hoping -- which is what lets
  the regeneration below be a plain `--dir` over the whole corpus with the one
  ProTA recording in it.

- **A stall episode predicts part of its observation and reads the rest, and
  says which is which.** The fourth fixture, `tad_stall_episodes.h`, asserts a
  factory's lateness after a stalled settle. The residue, `30 - start % 30`, is
  predicted. The number of further stalled settles is read from the build,
  because the corpus samples one settle in four. The script's control shows the
  predicted half is real: the same selection over settles that did *not* stall
  predicts no build. One episode per residue is the variety rule. The episodes,
  the filters and the mutations are under `0x28`, "What a stalled settle costs a
  factory".

- **A weapon-flight cell is a shot, not an aggregate, and it says how much
  company it had.** There are three fixtures now, one per oracle, in three
  headers that share no struct: storage in `tad_economy_episodes.h`, build
  timing in `tad_build_episodes.h`, and flight time in
  `tad_weapon_episodes.h`. A weapon episode carries one representative
  pairing -- the earliest one of its cell that landed on the cell's modal
  flight time -- with the shot's own origin and aim point as the raw 16.16
  integers the wire carried, the tick it was fired on, the tick its damage
  arrived, and `pairings`/`pairingsAtMode` beside them. The geometry is part of
  the observation here in a way it never was for a build: what the test has to
  reproduce is a distance being crossed, so the distance travels with the
  number.

  **Only the cells a model predicts are checked in**, and since the footprint
  model that is all 37 -- 24 constant-speed and 13 with a motor. Under the
  aim-point model it was 33, and the four it did not predict were skipped with
  a printed reason rather than checked in with their offset written into
  `expectedFlightDelta` -- the same rule that kept airborne builders out of the
  build fixture until §110 explained their extra tick and RWE reproduced it.
  That field is for a divergence somebody decided on, never for an
  observation nobody has explained, and the rule still stands for the next
  cell that disagrees.

  An episode carries the weapon's own launch speed, acceleration, range and
  motor timer beside its `weaponvelocity`, and a flag for whether the engine
  flies it on the motor path, so the test builds the same physics the loader
  would. It also names the **victim** and carries its `FootprintX` and
  `FootprintZ`, because the round stops on that footprint and because a
  self-propelled cell is scored only over victims that could not outrun a step
  of the round, and the row should say which one that was. What it does not carry is `guidance`, `tracks` or
  `turnrate`: every episode is a shot at a point along a heading the round is
  already on, which is what the victim bound selects for, and steering towards a
  point dead ahead is an identity.

  `expectedFlightDelta` is zero in all 37. The test stands a victim at the aim
  point with the episode's footprint, stamped into the occupied grid by
  `tryAddUnit`, fires through `spawnProjectile`, and counts ticks until the
  projectile dies on one of that victim's squares. RWE moves a projectile and
  then tests the grid, the order `0x49B720` uses, and every episode agreed on
  the first run. The mutations that make that worth anything are listed under
  "Pairing a `0x0d` to the `0x0b` it caused": truncating the footprint edge
  fails exactly the 14 predicted, testing before the move fails all 37, and a
  motor that never accelerates fails exactly the 10 predicted. What the test
  does **not** pin is where RWE's own firing sits against the first step in
  play, because it spawns the round outside `tick()`; that is open, and it is
  written up under "Where a round stops". The field stays because a fixture
  that cannot express a divergence is a fixture that gets disabled the week one
  is decided on.

Regenerating (Escalation; the paths are a local corpus, not a repository one):

```bash
cd build && make -j$(nproc) tad_episodes && cd ..
./build/tad_episodes --file ~/ta-demos/14727.ted --file ~/ta-demos/14731.ted \
    --units ~/ta-mods/x-esc --emit-cpp src/rwe/sim/tad_economy_episodes.h
./build/tad_episodes --dir ~/ta-demos --units ~/ta-mods/x-esc \
    --emit-build-cpp src/rwe/sim/tad_build_episodes.h
./build/tad_episodes --dir ~/ta-demos --units ~/ta-mods/x-esc \
    --emit-weapon-cpp src/rwe/sim/tad_weapon_episodes.h
./build/tad_episodes --dir ~/ta-demos --units ~/ta-mods/x-esc \
    --emit-stall-cpp src/rwe/sim/tad_stall_episodes.h
```

The build-timing and weapon ones take the whole directory because their cells
pool across games and want every observation they can get. The build pass prints
a warning for the one ProTA recording and then excludes it, per the rule above.
`--cells` and `--weapon-cells` print the two tables without writing anything,
which is what `tools/tad-buildtime.py` and `tools/tad-weapontime.py` score and
what each port was checked against, cell for cell, over the same corpus.

The single ProTA demo yields exactly one scoreable cell of its own
(`ARMVP -> ARMFAV`, three builds, agreeing with the model), which is not enough
to be worth a fixture, and it cannot join the Escalation ones in any case:
`--units` names types out of one load order, and the two data sets do not share
one.

**A demo with a computer player is contaminated for economy work** and is not a
candidate. TA handicaps a computer player's production at every site (§23), and
Escalation moved that constant (`docs/TA-PATCHES.md`), so its income is neither
TA's nor the mod's documented figure. The thirteen ladder games in the corpus
are human, but read the header rather than assuming it of the next one.

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
- the data set is one RWE does not hold. **Most of that archive is TA:Esc and
  other mods.** `ExtraSector` type 7 is a mod id but no demo in the corpus
  carried one, so the filter comes instead from the `0x1a` table's fingerprint,
  which `tad_probe` now prints and which does separate the two data sets in the
  corpus cleanly -- see the section on that table above. The archive's own
  per-demo mod tag, recorded in a sidecar by `tools/fetch-demos.py` at fetch
  time, is the cross-check. Do not trust the map name: the bracketed prefixes in
  the corpus (`[Diox]`, `[L]`, `[Metal]`, `[Bully]`, `[Pro]`) are map-pack markers,
  not mod markers;

  Note that a mod being present is **not** on its own a reason to reject an
  episode. What is under test is `TotalA.exe`'s arithmetic, and a mod that is
  pure data records that arithmetic as faithfully as vanilla does -- the episode
  transcribes the FBI values inline and never cares where they came from. What
  does disqualify a demo is a mod that patches the **engine**, and one of the two
  in this corpus does: ProTA 4.8 ships a `TotalA.exe` byte-identical to the GOG
  one (md5 `8e74a1dffa1f5988624c52048f5b20cd`), while TA: Escalation 10.2 ships
  one of identical size but md5 `1e677a7f92c79b5ab35440853d822c17` -- 4,425
  patched bytes over 103 runs, 71 of those runs and 3,185 of those bytes inside
  `.text`. So the single ProTA demo is the clean reference case and the twelve
  Escalation ones are quarantined until those runs are classified against the
  routines `TOTALA-EXE.md` names. The patched DLLs both mods ship (`tplayx.dll`,
  `tdraw.dll`, `win32.dll`, and Escalation's `eplayx.dll` and `ddraw.dll`) are
  network, render and platform shims and do not bear on the simulation;
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

## The tools, mode by mode

What each mode of the demo tools does, and which finding above it rests on.
This lived in CLAUDE.md, where it was a fifth of a file that is read into
every session; it belongs beside the format it reads.

### `tad_episodes`

mines the same corpus for short bounded episodes with real
numbers in them, currently the build-timing ones: `0x09` nanoframe to `0x12`
finish, with the filters that make a duration mean anything (speed changes,
damage to the frame or its builder, and a stalled owner all disqualify a
window, and every rejection is counted rather than dropped). Deliberately not a
`tad_probe` mode — that tool's contract is "non-zero if anything walked out of
step" and an extractor exits non-zero for different reasons. `--emit-json`
writes the episodes; the console summary prints the **modal** duration per unit
type, which is the number to consume, since assists shorten a build and missed
micro-stalls lengthen it. `--units <dir>` names each type: a `0x09` carries a
1-based index into the sorted `units\*.FBI` names of the merged VFS, so point
it at the data set the demo was recorded on and the episodes stop being
anonymous. It warns if the demo's own type count disagrees with the directory.

Other modes. `--emit-resources` dumps every `0x28` resource sample as
JSON, collapsed: a sender emits `numPlayers - 1` **identical** copies of one
record on one tick, one unicast per peer, so a burst carries one state and it
is the sender's own. The dump keeps the burst length beside each sample and
counts any burst whose copies disagreed, which is the number that would
overturn that reading. `--emit-cpp` writes the checked-in fixture,
`src/rwe/sim/tad_economy_episodes.h` — storage episodes for the
`[economy][corpus]` tests, with each unit type's own FBI values transcribed
inline. It needs `--units`, emits in a deterministic order with no timestamp
and no path, and reads the old file back so a regeneration prints `unchanged`
or `CHANGED`. Do not hand-edit the header; regenerate it. See
`docs/TA-DEMOS.md`, "What an episode looks like".

`--cells` prints the (builder type, product type) **build-timing** cells --
the same grouping and the same float32 completion model
`tools/tad-buildtime.py` scores, ported, and checked against it cell for cell
-- and `--emit-build-cpp` writes them as the second fixture,
`src/rwe/sim/tad_build_episodes.h`, for the `[build][corpus]` tests. Unlike
the storage pass this one is corpus-wide, because a cell pools across games,
which is also why a demo whose own type count disagrees with `--units` is
dropped from it outright rather than merely warned about. Immobile and
airborne builders become episodes, each scored against its own model — a
construction aircraft's first tick pays two increments (`docs/TOTALA-EXE.md`
§110), which RWE reproduces, so its cells carry the ordinary
`expectedDurationDelta` and nothing of their own. A ground mobile builder
never becomes one. `--max-cells` caps how many, keeping every cell whose
`BuildTime` divides exactly by the rate, because those are the ones that carry
a non-zero delta, and every airborne cell, because there are three.

`--emit-shots` dumps every `0x0d`, `0x0b` and `0x0c` as **JSON Lines** (about
1.5 million records, 320 MB, coordinates exact), shooters and victims already
named, so the weapon-event pairing can be argued over the data before any
miner is written. What it established: a `0x0b` is sent by the **attacker's**
owner and never the victim's, 797,783 to 0, so a shot and its damage share one
clock; and a round stops on the **victim's footprint**, not at its aim point,
so the flight time is the first step that puts it in one of the victim's
squares. See `docs/TA-DEMOS.md`,
"Pairing a `0x0d` to the `0x0b` it caused", for the filters, the rejection
counts and the classes that need models of their own.

`--weapon-cells` prints the (shooter type, weapon slot) **flight-time** cells
— the same filters and the same four models `tools/tad-weapontime.py` scores,
ported and checked against it cell for cell — and `--emit-weapon-cpp` writes
them as the third fixture, `src/rwe/sim/tad_weapon_episodes.h`, for the
`[weapon][corpus]` tests. Four classes become episodes: rounds that fly at a
constant speed, rounds with a motor (flown by a port of the engine's own
`updateSelfPropelledProjectile` and scored only over victims that could not
outrun a step of the round), rounds that are **lobbed**, stepped
horizontally at `weaponvelocity / 30 * cos(pitch)` off the flat root of the
engine's own firing solution and scored only where the weapon's own aim cone
could not have moved the answer, and one carrying an **inert `cruise`** — the
clause is read only through an aim point a round that cannot steer never asks
for, so such a weapon is a motor round wearing the flag, and one that *can*
steer is named "cruise steering" and left unscored. Only the cells a model
predicts -- all 40 of them; a cell that does not is skipped with a printed
reason rather than checked in with its offset, the same rule that keeps
airborne builders out of the build fixture, and fifteen of the seventeen
ballistic cells are skipped that way.
Needs `--units`, which also reads the data set's `weapon*/*.tdf` through the
engine's own `parseWeaponTdf`. `--window` and `--min-pairings` are the script's
two knobs and mean the same things.

`--weapon-slots` is the evidence that a `0x0d`'s trailing byte is the
shooter's **weapon slot**, a 0-based index into its FBI's
`Weapon1`/`Weapon2`/`Weapon3`: it checks every slot each type was seen firing
against the slots that type's FBI fills, and prints the naive
`slot < weapon count` tally beside it, and a third for naming a shooter by its
id's first build rather than its most recent. The three read 112, **14** and
453 over the corpus: the first gap is the standard TA convention of putting
the anti-air weapon in slot 3 and leaving slot 2 empty, which is the argument
that the byte is a slot at all, and the second is what unit-id recycling costs
anything that names a unit from its id. The build cells scope the same way
now; `docs/TA-DEMOS.md`, the `0x0d` section, says what that bought.

`--stall-episodes` prints the **stall** report -- where every sender's settles
fall, how many late factory builds are late by whole seconds, and the scored
episodes, in which a factory refused into its next job by a stalled settle is
late by exactly `30 - start % 30` plus whole seconds -- line for line with
`tools/tad-stalltime.py`, and `--emit-stall-cpp` writes the fourth fixture,
`src/rwe/sim/tad_stall_episodes.h`, for `[economy][corpus]` cases that replay
each episode through `GameSimulation::tick`. `docs/TOTALA-EXE.md` §111 is the
settle it rests on.

`--miss-buckets` answers why 53,706 of the paired shots drew **no** damage in
the window, which was what blocked the hit/miss half of the weapon oracle. It
sorts them into ten named buckets in a priority order -- a quarter are rounds
whose victim died before they arrived, which is `0x49B090` finding the
square's unit slot empty -- prints the no-damage rate down the same drift axis
the flight-time work uses, and then holds what is left to the `0x2c` health
stream, with the same bracket over shots *known* to have drawn damage as the
control that says how often the instrument misses one. Absence in `0x0b` is
still unknown rather than zero; the point of the pass is that it no longer has
to be. `docs/TA-DEMOS.md`, "Why 53,706 shots drew no damage".

`--unit-state` decodes every `0x2c` and holds the decode to the rest of the
stream -- a `0x09`'s type and position, `MaxVelocity`, a building staying put,
where a `0x0d` aims -- and exits non-zero if one fails to decode;
`--emit-unit-state` dumps the result as JSON Lines, with `--with-updates` for
the per-tick path and goal half. A `0x2c` is a bit stream: every tick, the
path or goal of each unit that changed it, and one unit's full state
round-robin, so a position is sent once every `maxUnits` ticks and there is no
kinematic corpus in it. `docs/TA-DEMOS.md`, "`0x2c`, unit state".

### `tools/tad-buildtime.py`

scores the corpus's modal build durations
against TA's own completion arithmetic, which is a **float32** fraction and
not `ceil(BuildTime / (WorkerTime/30))`; the difference is a tick, and where
`BuildTime` divides exactly it is the tick that tells the two apart. Exits
non-zero if a scored pair stops agreeing, so it is a check rather than a
listing — but also when there is *nothing* to score, so read the message and
not just the status. The single ProTA demo hits that: it has one scoreable
immobile pair and needs `--min-builds 3`. Builders split three ways and the split matters: **immobile** ones
are scored cell by cell, **airborne** ones are scored pair by pair against
two increments on the nanoframe's own tick, because a construction aircraft's
mission runs its build step twice on that tick (`docs/TOTALA-EXE.md` §110), and
**ground mobile** ones are never scored at all, because they pay their own COB
deploy before `INBUILDSTANCE` and that belongs to the mod rather than the
engine. `--overheads` lists those. `docs/TOTALA-EXE.md` §23 and
`docs/TA-DEMOS.md`. The same arithmetic, ported, is what feeds
`--emit-build-cpp`; the two must keep agreeing, and the script is the
reference.

### `tools/tad-weapontime.py`

the reference for the weapon oracle, the same
role `tad-buildtime.py` plays for build timing. It pairs each `0x0d` to the
`0x0b` it caused (nothing in the stream links them, so the filters are the
work) and scores the result. A round detonates the first tick its move puts
it in a map square the victim occupies (`0x49B090`), so the flight time is the
first step that lands the round, flown along its aim line, in the victim's
**footprint** -- 792 of 810 constant-speed pairings against still victims,
where the old aim-point model `ceil(distance / (weaponvelocity / 30)) - 1` took
65% and its `-1` was really the footprint. How far a step goes is the class:
`weaponvelocity / 30` for a constant-speed round, for one with a **motor**
a tick-by-tick replay out of `startvelocity`, `weaponacceleration` and the
burn -- a port of the engine's own `createProjectileFromWeapon` and
`updateSelfPropelledProjectile` -- and for a **shell** that same
`weaponvelocity / 30` times the cosine of the angle the gun elevated to, which
is the flat root of the solution at `0x49A890` and the only way gravity
reaches a flight time at all. All 40 scored cells land on it. The missile
class is scored only over the pairings whose victim could not have outrun
**one step** of the round, because a `0x0d` records where the shot was
*aimed*; `--drift` prints the measurement that bound comes from, which is one
falling curve both classes sit on. The shell class takes that bound and a
second one of its own -- the original jitters every turret shot's pitch, which
for a shell is a *range* error rather than a speed error -- and `--cone` prints
the split it makes and the cells it costs. `--classes` lists
the three it deliberately does not score — `vlaunch`, `waterweapon` and
`burst` — and `--unmodelled` is the measurement behind each, which is what
makes those exclusions results rather than sentences: a vertical launch spends
about 190 ticks in the air and arrives with a twenty-tick spread and no mode
even over victims that cannot move at all; no torpedo cell keeps `--min-n`
pairings after the drift bound, whatever sea level was, though the one fired
from below the surface reads the best share in the corpus; and a burst
weapon's deltas come off a **comb**, because one `0x0d` stands for several
rounds. `--replay` prints the instrument behind all three — the whole of
`updateSelfPropelledProjectile`, which scores nothing and gives the
step-length model's own answer on every pairing it is run over, and which is
what caught `cruise` being excluded for a clause its one cell cannot reach.
`--footprint` prints the evidence for the stop.
Exits non-zero if a scored cell moves. `docs/TA-DEMOS.md`, "Pairing a
`0x0d` to the `0x0b` it caused", "Where a round stops", "A shell: the flat
root, and the cosine that falls out of it" and "The three classes no model
describes".

### `tools/tad-stalltime.py`

the reference for the stall oracle. It imports
`tad-buildtime.py`'s model rather than copying it, uses the `0x28` stream
only as the witness that a settle stalled, and scores factory builds whose
lateness a stall should fix to a residue. Exits non-zero on a miss, on a
named exception moving, or on nothing to score. It needs the episodes with
`--all` and `--emit-resources` beside them; `docs/TA-DEMOS.md`, "What a
stalled settle costs a factory".

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
   weapon events. The counts in the table above say what a corpus this size
   gives you to work with: 66,079 build starts and 105,617 build finishes,
   469,842 resource samples, 631,578 shots against 824,844 damage events and
   60,075 deaths.

   **Partly done.** The payloads are decoded (`tad_events.{h,cpp}`, sections
   above) and the death oracle landed a finding that goes into
   `TOTALA-EXE-WRECKS.md` rather than into a test. The economy has slots 0-3,
   which is enough for storage-cap and stall episodes.

   **Naming is solved.** A `0x09`'s type index is a 1-based index into the
   sorted `units\*.FBI` names -- see the `0x09` section above for the rule and
   the evidence -- so `tad_episodes --units <dir>` names every episode and an
   episode fixture can transcribe the unit's real FBI values. It did not need
   the `0x1a` checksum, and the checksum would not have answered it.

   **What is left, in the order to do it:**

   1. ~~**Settle the build-duration baseline and the ProTA overhead.**~~ Done,
      and both answers are above. The baseline was the model's, not the
      extractor's: TA's completion test is a **float32** fraction counted down
      from 1.0, the first increment lands on the `0x09`'s own tick, and
      replaying that arithmetic predicts all 45 scored Escalation pairs
      including the 16-way divisible split that no integer model can reach.
      ProTA's ~34 ticks is the constructor's own COB deploy sequence before
      `INBUILDSTANCE`, so it is mod data rather than engine behaviour and RWE
      already reproduces it. `tools/tad-buildtime.py` is the re-runnable check.
      What a fixture may assert is therefore: **factory builds, exactly**, and
      nothing about a mobile builder's offset without that builder's script.
   2. ~~**`--emit-cpp`.**~~ Done, together with (3), because designing the
      format before it had a consumer would have been guesswork.
      `tad_episodes --emit-cpp` writes
      `src/rwe/sim/tad_economy_episodes.h`: plain structs, each unit type's own
      FBI values transcribed inline, provenance as demo id plus the sample tick
      and the one before it, emitted in a deterministic order with no timestamp
      and no path, and the tool reads back what was there and says whether the
      file moved. The conventions it settles are in "What an episode looks
      like" below.
   3. ~~**The economy oracle.**~~ Done: the storage half, which is the half
      slots 2 and 3 can carry alone. TA's storage capacity is a plain sum over
      what a player has **finished** -- the lobby's base, plus each unit's own
      `MetalStorage`/`EnergyStorage`, with nanoframes contributing nothing --
      and that sum tracks the reported capacity from the opening sample of all
      86 players in the corpus. Three `[economy][corpus]` cases in
      `src/rwe/sim/economy.test.cpp` assert it against `updateResources`, and
      both halves bite: crediting a nanoframe, or taking the base from the
      commander's own FBI, each break them.

      ~~**The stall half.**~~ Done, through build lateness rather than the
      stockpile path, and "What a stalled settle costs a factory" above says why.
      A stall costs a factory whole seconds on a settle every player shares
      (`TOTALA-EXE.md` §111, which also struck the staggered-settle entry from
      §88). `tools/tad-stalltime.py` is the reference: 73 of 77 scored builds
      land on their residue, the four short ones are named exceptions, and the
      same pattern over settles that did not stall predicts 0 of 449.
      `--emit-stall-cpp` writes `src/rwe/sim/tad_stall_episodes.h`, 24 episodes
      replayed through `GameSimulation::tick` by three `[economy][corpus]` cases,
      and four mutations each moved exactly the subset their mechanism predicts.
      What stays unassertable is the fractions inside one settle, because the
      corpus samples one settle in four. §111 also lists ten places RWE's settle
      differs from TA's, none changed yet.
   4. ~~**The build-timing oracle.**~~ Done.
      `tad_episodes --emit-build-cpp` writes `src/rwe/sim/tad_build_episodes.h`
      -- 25 (factory, product) cells of the Escalation corpus, each consumed as
      the modal duration of every build of that pair -- and three
      `[build][corpus]` cases in `src/rwe/sim/buildtime.test.cpp` assert them
      against `UnitState::addBuildProgress`, driven directly, with
      `workerTimePerTick` (`src/rwe/LoadingScene_util.cpp:438`) as the rate.

      **Not through the factory pipeline**, and that is the scoping decision the
      whole item turns on. RWE does not credit progress on the tick the
      nanoframe appears: `UnitBehaviorService.cpp` pushes a
      `unitCreationRequest` that `spawnNewUnits` services later, the next tick
      starts a `StartBuilding` COB thread and returns without building, and
      nothing is credited until the script sets `INBUILDSTANCE`. The corpus
      number has the opposite convention on purpose -- TA's first increment
      lands on the `0x09`'s own tick, and a builder's deploy before
      `INBUILDSTANCE` is mod data rather than engine behaviour, which is why
      only immobile builders are scored at all. A pipeline measurement would
      carry RWE's scheduling latency and the test script's deploy and compare
      them against a number chosen to exclude TA's. So the cases count calls to
      the accumulator and assert `calls - 1`, which is hermetic, fast, and
      immune to anything a script does. A pipeline test is worth having and is a
      separate thing, under a separate name and with no corpus number in it.

      **This is the first fixture whose expected-difference field carries a
      non-zero value**, which is the whole argument for the field existing. Ten
      of the sixteen cells whose `BuildTime` divides exactly by the builder's
      rate need one increment more than the division says, and RWE finishes
      those a tick early -- `docs/TOTALA-EXE.md` §88, kept deliberately, because
      closing it means a `float` in hashed simulation state. Those ten carry
      `expectedDurationDelta = -1` and the other fifteen carry zero. A test
      asserting equality here would have been right to fail on a quarter of its
      cases and disabled within a week.

      Both mutations landed. Finishing one increment early moves **all 25**
      cells, which says the cases are measuring the accumulator and not a
      constant. Replacing the accumulator with the original's float32 fraction
      fails **exactly the ten** and no others, which says the deltas are
      precisely the divergence §88 describes rather than a per-episode fudge.

      Airborne builders are still not episodes. Why a construction aircraft
      finishes a tick early is now explained -- its mission runs the build step
      twice on the nanoframe's tick (`TOTALA-EXE.md` §110) -- and the decision
      is to match that in RWE rather than license a `+1`, so they become
      episodes together with that change and not before it.
   5. **The weapon-event oracle.** `0x0d` shot to `0x0b` damage or `0x0c` death
      gives time-of-flight and hit/miss with the shot as an explicit input,
      aimed at the missile motor model and the ballistics work. Remember `0x0b`
      is not a complete damage ledger: treat absence as unknown, never as zero.

      **Two things are settled for it already.** A `0x0d`'s trailing byte is the
      shooter's weapon slot (the `0x0d` section above), so a shot resolves to a
      weapon definition and not merely to a shooter; and with the shooter named
      by the id's most recent build, 630,522 of the corpus's 631,578 shots have
      a named shooter and 612,992 are aimed at a unit, so naming will not be
      what limits this.

      **Pairing is solved, and the model with it.** `--emit-shots` dumps every
      `0x0d`, `0x0b` and `0x0c` as JSON Lines; a `0x0b` turns out to be sent by
      the **attacker's** owner and never the victim's (797,783 to 0), so a
      flight time is a difference within one clock; filtering to shots that are
      the only shot from that shooter at that victim in a +-300-tick window, and
      that draw exactly one damage record in the 300 after, keeps 35,535 shots;
      and over the constant-speed weapons the flight time is
      `ceil(distance / (weaponvelocity / 30)) - 1`, the same first-step-on-the-
      firing-tick off-by-one as the build accumulator. The pairing is confirmed
      by a number the filters never look at: each cell's modal `damage` is that
      weapon's own `[DAMAGE] default`. The whole argument, the rejection counts
      and the three classes that need their own models are in "Pairing a
      `0x0d` to the `0x0b` it caused" above.

      **The fixture and the test exist, over four classes.** `--emit-weapon-cpp`
      writes `src/rwe/sim/tad_weapon_episodes.h`, now 40 episodes over the cells
      a model predicts -- 24 constant-speed, 13 with a motor, 2 that lob a shell
      and 1 carrying an inert `cruise` (it was 33 over two classes when this
      paragraph was written, 22 and 11) -- with `UnitFacts`
      widened to carry each FBI's `WeaponN` names and a second reader over the
      data set's weapon TDFs going through the engine's own `parseWeaponTdf`, so
      a fixture cannot disagree with the loader about what a field means.
      `weaponflight.test.cpp` (`[weapon][corpus]`) spawns a real `Projectile`
      through `spawnProjectile` and steps it with `tick()`, and every episode of
      both classes passed on the first run: `expectedFlightDelta` is zero
      throughout, which was the prediction each time.

      Mutations are what make that worth anything, and four have landed. On the
      constant-speed pass: making a projectile skip its first step moved **all
      23** of the episodes there were then, and converting `weaponvelocity` to a
      per-tick step with integer division instead of float moved **exactly the
      two** whose representative distance sits near a tick boundary. On the motor
      pass: stopping the motor accelerating fails **10 of the 11** motor episodes
      and **none** of the 22 others, which is the model's own subset and nothing
      else; and taking away the projectile's first step of travel fails **all
      33**, which says every episode measures the stepping and not a constant.

      ~~**What is left** is the three classes that still have no model~~ --
      **ballistics is done**, and the write-up is "A shell: the flat root, and
      the cosine that falls out of it" above. A shell is stepped horizontally at
      `weaponvelocity / 30 * cos(pitch)`, with the pitch the flat root of the
      solution `0x49A890` finds; gravity reaches the flight time only through
      that angle, which is what a mutation halving it and moving no episode
      demonstrates. Its cells are not what limits it: the aim cone the original
      puts on every turret shot is a **range** error for a shell rather than a
      speed error, so only 2 of its 17 cells keep enough pairings to be scored,
      and the other 15 are printed with what took them. The decoding prize that
      was hoped for there -- the `0x0d`'s **rotation triple** -- was not what
      gave this class its launch vector, since the muzzle and the aim point
      already determine the angle through the engine's own solver. It has since
      been identified from the other side (the shot's own launch attitude, "The
      `0x0d`'s rotation triple" above) and `--emit-shots` carries it now, so
      what remains is the use rather than the decode: it is the only way to test
      the solver's *output* rather than its inputs.

      ~~What is left is `vlaunch` (3 cells), torpedoes (4) and `cruise` (1)~~ --
      **`cruise` is done and the other two are answered in the negative.**
      `cruise` needed no model at all: its clause is read only through an aim
      point a round that cannot steer never asks for, and what its exclusion was
      really costing it was the motor class's drift bound. `vlaunch` and the
      torpedoes were both put through a full replay of
      `updateSelfPropelledProjectile` and neither can be scored -- a vertical
      launch spends about 190 ticks in the air and arrives with a twenty-tick
      spread and no mode even over victims that cannot move, and no torpedo cell
      keeps `--min-n` pairings after the drift bound whatever sea level was.
      `burst` is not a flight-time class at all: one `0x0d` stands for several
      rounds, so its deltas come off a comb. All three are written up under
      "The three classes no model describes", with the measurement behind each
      reprintable through `--unmodelled`.
      ~~The sub-tick residual~~ is closed: a round stops on the
      first step that puts it in one of the victim's footprint squares ("Where a
      round stops"), every scored cell now lands on +0, the four named
      exceptions are gone, and the fixture is 40 episodes fired at a real victim.
      What that reopened is **which tick a new round first steps on**: the old
      `-1` was the only evidence, and a first read of the binary disagrees with
      the corpus. It has to be settled before any of the classes above, and is
      being. The height half of the collision test is not modelled either --
      though it is no longer a *suspect*, having been tried as the explanation
      for the shell class's spread and refuted there.

      ~~And the hit/miss half of the oracle, which has to answer why 53,706
      shots drew no damage in the window before it can call any of them
      misses.~~ **Answered, and it is unblocked.** `tad_episodes
      --miss-buckets` sorts the 53,706: 36% structural, led by rounds whose
      victim died before they arrived; 56% misses the drift bound explains; 8%
      misses it does not, and what stays unexplained about those is the
      mechanism rather than the disposition, because the `0x2c` health bracket
      says about nine in ten of both open buckets took no damage at all. "Why
      53,706 shots drew no damage" has the table, the instrument's own control,
      and the residue. **What it settles for the fixture**: the oracle is a
      RATE and never a per-shot prediction, since where the victim was when the
      round arrived is only in the stream once every `maxUnits` ticks; it has to
      be scored over shots the structural buckets clear, or it measures how
      often the victim died first; and target type does belong in the cell key,
      which is now measured rather than suspected -- holding the weapon class
      and varying the victim moves the rate further than the reverse. It is a
      fixture asserting an interval rather than an equality, which is a shape
      none of the three checked in have, and designing it is the next piece of
      work.

   Every one of those carries the expected-difference annotation described in
   "The hazard to design in from the start". A corpus is an efficient machine
   for regressing intentional decisions if it is allowed to be.
3. ~~Decide on `0x2c` once the stream has been stared at.~~ **Decoded**, bit
   for bit over all 7,422,196 of them -- see "`0x2c`, unit state". It did not
   open the kinematic corpus: positions go out once every `maxUnits` ticks, so
   acceleration and turn rate cannot be fitted. What it did open is a
   replicated-path stream that could become a pathfinding oracle, a zero-drift
   filter over mobile victims for the weapon oracle, per-cycle health for the
   hit/miss half, and the design of puppet playback: steer along the replicated
   waypoints and correct at each full-state record, as a receiving TA does.

## Open questions

- ~~The `0x2c` layout.~~ **Decoded**, out of `TotalA.exe` and held to the corpus:
  a bit stream of per-unit path and goal updates and one round-robin full-state
  record per tick, all 7,422,196 accounting for every bit. See "`0x2c`, unit
  state". What is left inside it is small: the move goal's `goal+0x10` word,
  why a few percent of buildings report a different yaw from their `0x09`, and
  what a geothermal or fusion plant is carrying when a unit reports it as its
  carrier. The larger consequence is a negative: positions are sent once every
  `maxUnits` ticks, so the kinematic corpus hoped for below is not in the stream.
- What `0x07` (30,577) and `0x0a` (94,445) are. Both are common enough to be
  something ordinary and both are `UNK_` in the reference.
- The length of `0x13`, which the reference never had, and which nothing in the
  corpus exercises.
- **What computes a `0x1a` id.** The layout of the record is settled and the
  fingerprint does the filtering job, but the id itself is content-derived and
  resists every name hash tried against it. ~~This is now the single thing
  blocking checked-in episodes.~~ It is not: naming came from the load order
  instead (see `0x09`), so episodes can be named and checked in without it. The
  id stays unexplained and stays here, but nothing waits on it.
- **The last six floats of `0x28`.** Stored and storage are identified for both
  resources; the two cumulative triples are not, so expenditure still cannot be
  recovered by difference.
- **The trailing `u16` of `0x0b`.** Not remaining health, and not identified.
- **Why the `0x1a` `sub` 2 block is one or two entries longer than `sub` 3 in
  four of the thirteen demos.**

Answered by the event-payload pass: what `0x09`, `0x0c`, `0x0d`, `0x10` and
`0x12` carry, the first four floats of `0x28`, the layout of the `0x1a` record
and whether it can identify a mod (it can identify one it has seen before), and
how unit ids partition by owner. All in the sections above.

Answered by the economy pass: **which player a `0x28` belongs to** -- the
sender, and the `numPlayers - 1` burst is a unicast fan-out of one identical
record rather than a table of the other players. See the `0x28` section. The
filters were never skewed by this; `tad_episodes` was already crediting the
sender, for the wrong reason.

Answered since this document was written: the `ExtraSector` types (all seven
named, in the container section), and whether the `decrypt` checksum is
verifiable (it is, and it verified 89 times out of 89).
