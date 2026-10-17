# Packet envelope, compression choice, receive caching, bit streams and traffic counters parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-UTIL-SQSH-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers two
chapters of the reference together because the second consumes the first's output:
`network-net-condenser` (nine services, `0x44f720` to `0x44fd40`: the three-byte envelope, the
partial XOR transform and 16-bit checksum, the decision to compress, the simulated-loss
switch, and the cache that holds a decoded packet that did not fit the caller's buffer) and
`network-net-stats` (fifteen services, `0x415b20` to `0x416240`: the bit writer and reader every
network record is packed with, the per-direction and per-kind traffic counters, and the
formatted rate text). This is the finding that implementation work cites for any of them, per
`AGENTS.md` and ADR-0004.

**Scope.** RWE's own lockstep multiplayer is RWE-original and is **not** audited. Only the
TA-compatible networking is: the envelope in `io/tad/tad_util.*` and `net/ta/TaPacket.*`, the
bit reading and writing in `io/tad/tad_events.cpp` and `tad_encoders.cpp`, and the shipped
captures documented in `docs/TA-NETWORK.md`, which is an **executed-against-real-TA record and
outranks a static reading**. The LZSS codec's own byte rules are `BT-UTIL-SQSH-PARITY.md`.
RWE's F9 network overlay (`docs/NETWORK-OVERLAY.md`) is its own diagnostic and has no
counterpart to audit against the original's counters.

## Source and evidence class

- **Source.** Two prose chapters of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\`, files
  `network-net-condenser.md` and `network-net-stats.md`). Nothing else under that tree was
  opened beyond the sibling network chapters: no source, no decompilation, and none of its
  source links were followed. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; BT-164 (the packet
  decoder window and preset) touches the codec and is not re-raised.
- **Evidence class.** A **static reading** by that project: nothing was executed. Weaker than
  `TOTALA-EXE*.md` and than `TA-NETWORK.md`'s captures. Disagreements are marked **CONFLICT**
  with no winner picked.
- **The RWE half** is a read-only audit of `io/tad/tad_util.*` (`tadEncrypt`, `tadDecrypt`,
  `tadCompress`, `tadDecompress`), `net/ta/TaPacket.*`, `net/ta/TaHostGame.cpp`,
  `net/ta/TaBattleroom.cpp` (the handlers' use of the parse), `io/tad/tad_events.cpp` (the bit
  reader) and `io/tad/tad_encoders.cpp` (the bit writer), with `docs/TA-NETWORK.md` and
  `docs/TA-DEMOS.md`, at the working tree of 2026-10-17. Nothing was built, run or changed.
  Another agent had uncommitted edits in the tree, so line numbers are "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Buffers and size limits
3. The envelope: kind, checksum and the partial XOR
4. Choosing to compress
5. Receiving: validation, the minimum size, pass-through
6. The buffer-too-small cache and simulated loss
7. Bit writer and reader
8. Traffic counters and the formatted rates
9. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| envelope layout, XOR, checksum | `TA-NETWORK.md` "TA packets": decrypt, decompress with a three-byte header, skip seven bytes | agrees; the chapter gives the exact byte ranges (rule 3) |
| compress decision | `TA-NETWORK.md`: "a `0x02` is 20 bytes and does not compress"; "uncompressed packets (type 0x03) were accepted where TA sends compressed ones" | rule 4 gives the rule behind both |
| receive checks | none | rule 5 |
| bit writer and reader | `tad_events.h` ("a BIT stream ... `0x415C10`", "`0x415DC0`"); `TA-DEMOS.md` "`0x2c`, unit state" | rule 7, **MATCHES** |
| counters, rates | none | rule 8 (not applicable) |
| also registered in the stats range: VTOL order handlers under id `0x16`; GAF sequence stepping | `BT-ORDERS-VTOL-ORDERS-PARITY.md`, `BT-GRAPHICS-GAF-PARITY.md` | not repeated |

## 2. Buffers and size limits

**Reference.** Each condenser (there are two global ones, a "send" and a "receive" object of the
same class, each with its own buffers) owns a **28000-byte** receive/expanded buffer and three
**2800-byte** arrays (send, encoded packet, and one that is allocated and never used). The
accumulate step *replaces* the send buffer's contents (it does not append; bundling is done
upstream) and, if the request is larger than 2800 bytes, deletes and reallocates **only** the
send buffer to the exact size, again on every later oversized request. The encoded-packet array
never grows, so an oversized send overflows it: even a raw payload of exactly 2800 bytes needs
2803 envelope bytes. The receive path writes the decoded bytes into the 28000-byte buffer with
no capacity argument to the decompressor.

**RWE: not applicable as buffers, DIFFERS as bounds.** RWE builds a fresh `std::vector` per
packet; there is no fixed array to overflow, and its decompressor grows the output only while
input remains (a back-reference of two bytes yields at most seventeen, so output is bounded by
about 8.5 times the input; the transport's read buffer is 64 KB). The original's *limits* are
what a peer's buffers were sized for, and RWE has no sender-side limit (see
`BT-NETWORK-PACKETS-PARITY.md` rule 5): a bundle larger than about 2800 bytes would overflow a
real TA's encoded-packet array. Keeping RWE's bundles under the packet manager's 1066-byte
block also keeps them under this limit.

## 3. The envelope: kind, checksum and the partial XOR

**Reference.** The wire form is three bytes then the payload: byte 0 is the **kind** (three for
stored, four for LZSS), bytes 1 and 2 an unsigned 16-bit checksum (little-endian), then the
payload. For payload indices **three through total-minus-four inclusive** the sender XORs each
byte with its own **index narrowed to a byte** and adds the XORed byte into a 16-bit sum;
the header is written afterwards. The **last three payload bytes are neither transformed nor
summed**, and the kind byte is not protected. The receiver sums the received (still XORed) bytes
over the same range, then XORs them back in place. It is a symmetric lightweight transform, not
encryption or authentication.

**RWE: MATCHES.** `tadEncrypt` XORs each byte from index three through size minus four inclusive
with a key that starts at three at index three and rises by one (that is the index, narrowed to
a byte) and sums the XORed byte; `tadDecrypt` sums first, then XORs, and stores the 16-bit sum
at bytes 1 and 2 little-endian (`tad_util.cpp` about 28-66). The range, the key, the unprotected
tail and the order of sum and XOR are the chapter's, and `TaPacket::build` applies the
transform to the whole packet including the seven-byte header's marker, as the "payload"
including the leading dword. `TA-NETWORK.md` records "zero checksum failures" over the
captures, so this is also executed evidence. Packets shorter than four bytes are padded rather
than failed in the demo reader (`tadDecrypt` pushes a `0x06`), which suits a recording and not a
live check (rule 5).

## 4. Choosing to compress

**Reference.** LZSS compression is **attempted only when the payload is longer than twelve
bytes**, with the codec's global preset enabled around the call and disabled after. The
compressed form is chosen only when compressed length plus three is **strictly less** than the
original payload size and the "compression off" setting is zero; otherwise the raw payload is
copied behind a kind-three header. Setting "compression off" still runs the compressor, and
declining to use the result is all it does.

**RWE: MATCHES in effect, DIFFERS by at most three bytes.** `TaPacket::build` frames a packet as
compressed or uncompressed by the batcher it was built for (pings and the load packets
uncompressed, everything else compressed). `tadCompress` always tries and falls back to the raw
packet when the compressed form is not smaller: it keeps compression when compressed payload plus
three is less than the **whole** packet (header included), which is "compressed payload smaller
than raw payload"; the reference's wording compares the compressed length plus three with the
payload size, a threshold stricter by the three header bytes (**UNCLEAR** only in whether the
chapter's "compressed length" already counts the codec's own header). Both forms decode on any
receiver, so the difference is a handful of packets that RWE sends compressed and TA would send
raw; it cannot be seen. The twelve-byte floor has no counterpart and needs none: a payload that
short never compresses (`tadCompress` adds a control byte and a two-byte terminator). The
captures agree: the 20-byte ping request and its reply go out uncompressed.

## 5. Receiving: validation, the minimum size, pass-through

**Reference.** Reception is gated on the sender being nonzero (a zero sender is a DirectPlay
system message and returns the transport result untouched). A first byte other than three or
four is **passed through untouched** as a success. Kinds three and four need a total size of at
least **four**, so a bare three-byte envelope is rejected with the "no messages" error. A
**checksum mismatch returns the "no messages" error**; the caller's buffer has already been
partly decoded and nothing is rolled back.

**RWE: DIFFERS: a bad checksum is ignored.** `taParsePacket` computes `checksumValid` but the
two live callers never read it: `TaBattleroom::handleAppData` and the in-game handler in
`TaHostGame.cpp` (about 291) walk and apply the packet whatever it says. Only
`ta_host_probe` prints it (`ta_host_probe.cpp` about 399). A corrupted UDP datagram (the
in-game stream) is therefore parsed as if sound and can be applied: a garbled `0x2c` or `0x0b`
is somebody else's input to the puppet layer, and the original would drop it. The fix is one
line on the live path, **and only the live path**: the demo reader must stay lenient because a
recording can contain packets the recorder rewrote. Kind: robustness (`docs/SECURITY-AUDIT.md`
rules: anything read from a socket is someone else's).

- **Pass-through of other first bytes: MATCHES.** A type that is neither `0x03` nor `0x04` is
  read as plain (`TaPacket.h` about 70-75).
- **Minimum size: DIFFERS, trivially.** RWE needs seven bytes (the whole header) before it will
  parse; the original accepts four and then stops at "no messages" or on the command scan.
  Packets of four to six bytes carry no command either way.
- **The zero-sender system path: not applicable.** DirectPlay system messages are separated by
  `TaDirectPlay` before application data reaches the TA parser.

## 6. The buffer-too-small cache and simulated loss

**Reference.** When the decoded packet is larger than the caller's buffer the condenser keeps it
in an internal single-slot cache, returns the required size and the "buffer too small" code, and
delivers it to a retry with a bigger buffer; a retry that is still too small leaves it cached.
It does not snapshot the sender and recipient, so the caller depends on the network object's
retained from/to fields. A diagnostic is formatted and discarded. A separate debug setting drops
outgoing packets: it draws a value from 0 to 100 and drops when the draw is **less than or equal
to** the configured percentage (the draw is a whole number from 0 to 100, so a setting of N drops about
(N+1) in 101 packets and no setting below 100 is an exact percentage); a simulated
drop reports success to the counters.

**RWE: not applicable.** `TaDirectPlay` hands the parser a complete datagram or stream message
of its own size, so no retry-with-larger-buffer protocol exists. RWE has loss testing of its
own at the sim level (`tools/net-test.py --lag`, `RWE_SIM_LAG`), unrelated to this switch; the
live-receiver simulation (`tad_puppet --live-sim jitter:loss:seed`) is how packet loss is
exercised on the TA path.

## 7. Bit writer and reader

**Reference (stats).** Every packed network record (the unit-state record above all) is written
by one bit writer and read by one bit reader. Fields are packed **low bits first** into
little-endian 32-bit words; a field that crosses a word boundary fills the remainder of one word
and continues in the next; a full-width field at offset zero is a whole word; supported widths
are one to thirty-two with at most one crossing; there is no endian conversion, no schema and no
authentication. The writer starts with 256 inline words (1024 bytes) and a growth routine that
**allocates a single integer instead of an array and then copies the old words into it**, so the
first growth overruns its allocation; a patch routine writes one byte at an offset with no
bounds check (used to write a length into a header already emitted). The reader has **no length
and no end-of-packet test**: it reads whatever is in memory. The signed read extends the top
field bit; a width of zero or more than thirty-two is unchecked.

**RWE: MATCHES the layout, and is stricter.** The decoder describes itself as reading "the way
0x415DC0 does" (`tad_events.cpp` about 323-352): fields least significant bit first out of the
subpacket taken as one little-endian integer, which is the same bit order as low-first fields in
little-endian words. Unlike the original, it checks the end: a read past the last bit returns
zero and latches an overrun flag the decoder inspects once per record, and the bit position is
clamped. The encoder (`TadBitWriter`, `tad_encoders.cpp` about 178) builds the record in a
growable byte vector (bit n at byte n/8), so the growth defect and the 1024-byte inline ceiling have no counterpart;
the record's length is the packed size rounded up to whole bytes. The re-encoding of the 5,153 ground
`0x2c` records of the captures is byte-exact for the Python codec and a standing check (`ta-capture.py --check`;
`TA-NETWORK.md`, "In game"). Nothing to fix. The one caution the chapter adds for any new writer: a field wider than
thirty-two bits or a zero-width signed read is undefined in the original, so a record that
needs one is not a TA record.

## 8. Traffic counters and the formatted rates

**Reference.** A message counter takes a **direction index** (not a player): zero for received,
one for sent; kinds two to forty-four also keep a per-kind count and byte total, and kinds zero,
one and forty-five or above count only in the aggregate. A packet counter takes the packet size
and an "overhead" value: the sender passes the **uncompressed size plus three** as the first and
the **encoded size** as the second, so the "compressed bytes sent" figure accumulates *encoded*
packet sizes and the "packet bytes sent" figure accumulates *uncompressed* sizes; simulated
drops are counted. The reset clears the logical and per-kind totals but **not** the compressed
counter, row zero or the sampling baselines. Rates are recomputed only when more than thirty
ticks have elapsed, as delta times thirty over elapsed ticks (integer), and the compression
percentage is `(logical sent delta minus compressed delta) * 100 / logical sent delta`, unclamped
(so it can fall outside zero to one hundred). The formatted line gives message-byte rates
labelled `pS` and `pR` (not packets per second), then sent and received packet-count and
packet-byte rates and the compression percentage. A second sampler with its own timestamp feeds
the byte rates shown on the in-game network bar (`BT-INGAME-INFO-PANEL-PARITY.md` rule 9:
a full bar is 5600 bytes per rate unit).

**RWE: not applicable.** RWE's F9 overlay measures its own lockstep (round trip, silence, tick
lead, stalls) and is a pure observer. Nothing in the TA-compatible path counts bytes by kind or
direction, and nothing displays a compression ratio. If a TA-style status line is ever wanted,
the two accounting facts above are the ones that differ from what the labels suggest:
"compressed bytes" is the encoded size, and the percentage is not clamped. The VTOL order
registration and GAF sequence stepper that share this address range are other audits.

---

## 9. Gaps to fix

Ranked by what could hurt a TA game. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **A packet with a bad checksum is parsed and applied on the live path.** The original drops it. | DIFFERS (robustness) | code: reject on `!checksumValid` in `TaBattleroom::handleAppData` and the in-game handler; leave the demo reader lenient |
| 2 | **No bundle size bound**: a TA's encoded-packet array is 2800 bytes and a bundle is one 1066-byte block. | DIFFERS (see packets rule 5) | code: early flush |
| 3 | **Compress threshold is three bytes looser than the reference's wording.** | UNCLEAR, unobservable | none unless the chapter's "compressed length" is clarified |
| 4 | **Minimum packet size seven against four.** | DIFFERS, trivial | none |
| 5 | **No counters or rate text.** | not applicable | none |

No source file was changed.
