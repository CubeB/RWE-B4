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
`record+0x21e` (`TOTALA-EXE.md` §100). The order is:

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
`ceil(BuildTime/p)` agree**, and the corpus agrees with both: 26 of 26 such
pairs land on the predicted duration exactly, `ARMVP` to `ARMFLASH` over 249
builds, `ARMLAB` to `ARMJETH` over 302, and so on.

**Where it *is* a multiple they disagree, and that is the test.** An integer
model says the job takes exactly `BuildTime/p` ticks. The float one says it
depends on whether repeated addition of `x` overshoots 1.0f or lands on it, and
that is not something a human can guess from the two numbers. Over the 15 such
pairs in the Escalation corpus it predicts every one, including which ten take
an extra tick and which five do not:

| Builder | Product | BuildTime | p | BuildTime/p | model | corpus | n |
|---|---|---|---|---|---|---|---|
| `ARMVP` | `ARMFLASH` | 1676 | 4 | 419 | +1 | +1 | 249 |
| `ARMVP` | `ARMLART` | 2840 | 4 | 710 | +0 | +0 | 74 |
| `CORVP` | `CORRAID` | 3564 | 4 | 891 | +0 | +0 | 57 |
| `ARMLAB` | `ARMVADER` | 6320 | 4 | 1580 | +1 | +1 | 54 |
| `CORLAB` | `CORCRASH` | 1820 | 4 | 455 | +1 | +1 | 34 |
| `CORAP` | `CORVENG` | 7356 | 4 | 1839 | +1 | +1 | 29 |
| `ARMLAB` | `ARMFLEA` | 5032 | 4 | 1258 | +1 | +1 | 29 |
| `CORLAB` | `CORCK` | 7720 | 4 | 1930 | +0 | +0 | 27 |
| `ARMLAB` | `ARMROCK` | 2432 | 4 | 608 | +1 | +1 | 21 |
| `CORVP` | `CORMIST` | 2636 | 4 | 659 | +0 | +0 | 21 |
| `ARMVP` | `ARMJAV` | 4704 | 4 | 1176 | +1 | +1 | 14 |
| `ARMAAP` | `ARMPNIX` | 30120 | 10 | 3012 | +1 | +1 | 8 |
| `ARMALAB` | `ARMZEUS` | 8560 | 10 | 856 | +0 | +0 | 7 |
| `ARMLAB` | `ARMWAR` | 4568 | 4 | 1142 | +1 | +1 | 6 |
| `CORALAB` | `CORPYRO` | 9000 | 10 | 900 | +1 | +1 | 6 |

Fifteen bits, all fifteen right. Doing the identical sum in **double** precision
gets 6 of 15, which is what says the accumulator really is 32 bits wide and not
just "floating point somewhere". Over every scored pair: `ceil` 10 of 41, `floor`
36 of 41, the float32 replay **41 of 41**.

**Those fifteen pairs are all Escalation**, which is a patched engine, so they
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
tick fast in ten of the fifteen cases where it is. That is worth knowing and is
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

What is left is that an airborne builder credits its job once more over its life
than a ground one does. §23 lists five call sites into `0x41BA60`; which one an
aircraft goes through, and whether it runs on a tick the ground path does not,
is the thing to read out of the binary. Until someone does,
`tools/tad-buildtime.py` scores the airborne class at -1 so that the regularity
is checked rather than merely noted.

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
otherwise** -- with construction aircraft the exception, since they deploy
nothing and sit at a flat -1 (above). `tools/tad-buildtime.py` splits the three
classes for exactly this reason: immobile scored cell by cell, airborne pooled
and scored at -1, ground mobile listed by `--overheads` with their floors so a
floor can be told from a tail, and never scored.

RWE already gates build progress on `inBuildStance` (`UnitBehaviorService.cpp`),
so it reproduces this as long as it runs the same script. There is no engine gap
here.

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
figure looks like. Confirming it against a weapon definition needs the unit-type
names, so it waits on the checksum work.

**`0x0b` is not a complete damage ledger.** 17,526 of the 56,913 script-driven
deaths in the corpus have *no* recorded damage on the victim at all, and the
corpus contains only ten `0x0e` area-of-effect records in twelve million
subpackets, so splash damage is not arriving by that route either. Any oracle
that wants total damage absorbed has to treat the absence as unknown rather than
as zero.

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
`u16` shooter, and one byte that is 0, 1 or 2 and is dominated by 0.

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

**So the standing question is half answered.** There is enough here for
storage-cap and stall episodes, which need only slots 0-3, and enough to see
production and consumption diverge. Recovering expenditure by difference still
waits on naming the last six.

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
      replaying that arithmetic predicts all 41 scored Escalation pairs
      including the 15-way divisible split that no integer model can reach.
      ProTA's ~34 ticks is the constructor's own COB deploy sequence before
      `INBUILDSTANCE`, so it is mod data rather than engine behaviour and RWE
      already reproduces it. `tools/tad-buildtime.py` is the re-runnable check.
      What a fixture may assert is therefore: **factory builds, exactly**, and
      nothing about a mobile builder's offset without that builder's script.
   2. **`--emit-cpp`.** A generated header of plain structs beside the test that
      uses it: unit type, the real FBI values transcribed inline, observed
      timings, and provenance (demo id plus tick range). Regeneration must be
      diff-stable. See "How to build it without poisoning the test suite".
   3. **The economy oracle.** Storage-cap and stall episodes need only `0x28`
      slots 0-3, which are settled, so this has no unknowns left in it and is
      the cheapest first real conformance test. It is also where the
      `--emit-cpp` and expected-difference conventions get shaken out, which
      everything after it inherits. RWE side: `GameSimulation::updateResources`
      and `settleResourcePool`.
   4. **The build-timing oracle**, which (1) has now scoped. RWE side:
      `UnitState::getBuildCostInfo`, `UnitState::addBuildProgress`, their two
      call sites in `UnitBehaviorService.cpp`, and `workerTimePerTick` in
      `LoadingScene_util.cpp`. Consume the mode, never the mean.
   5. **The weapon-event oracle.** `0x0d` shot to `0x0b` damage or `0x0c` death
      gives time-of-flight and hit/miss with the shot as an explicit input,
      aimed at the missile motor model and the ballistics work. Remember `0x0b`
      is not a complete damage ledger: treat absence as unknown, never as zero.

   Every one of those carries the expected-difference annotation described in
   "The hazard to design in from the start". A corpus is an efficient machine
   for regressing intentional decisions if it is allowed to be.
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
- What `0x07` (30,577) and `0x0a` (94,445) are. Both are common enough to be
  something ordinary and both are `UNK_` in the reference.
- The length of `0x13`, which the reference never had, and which nothing in the
  corpus exercises.
- **What computes a `0x1a` id.** The layout of the record is settled and the
  fingerprint does the filtering job, but the id itself is content-derived and
  resists every name hash tried against it, so a type index still cannot be
  turned into a unit name. This is now the single thing blocking checked-in
  episodes.
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

Answered since this document was written: the `ExtraSector` types (all seven
named, in the container section), and whether the `decrypt` checksum is
verifiable (it is, and it verified 89 times out of 89).
