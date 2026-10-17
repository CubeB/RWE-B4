# SQSH chunks, the LZSS stream and the archive-chunk reader parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md` and `BT-UTIL-TDF-PARITY.md`, it allocates no
section number in the main register. It covers one chapter of the reference, `util-sqsh` (22
services from `0x4d0ab0` to before `0x4d80b0`, plus the zlib routines linked into the same interval):
the raw 4,096-position LZSS stream, the SQSH chunk wrapper with its 19-byte header, checksum and
optional byte transform, the preset dictionary, and the size and error helpers. In the original the
same code packs the saved-game bank (`HapiBank`), unpacks archive chunks, and compresses network
packets. RWE shares only one of those uses: **the archive chunk**, `src/rwe/io/hpi/`. This is the
finding that implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

The save container is `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md`; the TA packet codec RWE uses for demos
and for hosting a joining TA (`io/tad/`) is a different consumer of the same stream format and is
covered in rule 6 below only for how it reads the stream.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\util-sqsh.md`). Nothing else under
  that tree was opened: no source, no decompilation, no source links. Rules are restated in prose.
  The spec-room discrepancy register was read, read-only; the only row near this chapter is BT-29
  (archive tie classes, the extra `.gpf`/`.gp3`, the ten-archive cap), which is about which archive
  wins and is not re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker than
  `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `src/rwe/io/hpi/` (`hpi_util.cpp`, `hpi_headers.h`,
  `HpiArchive.cpp`), `src/rwe/vfs/HpiFileSystem.cpp`, `io/tad/tad_util.cpp`, `tools/hpi.py` (read as a
  second implementation, which `CLAUDE.md` says must be kept in step) and the test files
  `io/hpi/hpi_util.test.cpp`, `tools/test_hpi.py` and `io/malformed_input.test.cpp` at the working
  tree of 2026-10-17. Nothing was built, run or changed; line numbers are "about".
- **No census was needed:** the retail archives were decoded in full by `hpi.py` and by the engine's
  own `hpi_test extract-all` when `tools/hpi.py` was written (its header says all thirteen archives'
  3,676 files extract identically); `hpi.py` also records that every shipped file in the original
  archives is chunked and none is stored uncompressed, and that `totala1/2/4` use LZ77 and the rest
  zlib.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and which consumers RWE has
2. The chunk header and the checks around it
3. The LZSS stream
4. Decoded length, the zlib path and what an error means
5. Packing, the preset dictionary, locking (not applicable)
6. The TA packet codec reads the same stream
7. `tools/hpi.py` against the C++
8. Malformed input
9. Gaps to fix

---

## 1. Shape and which consumers RWE has

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| 1 consumers (bank bodies, archive chunks, network condenser) | `TOTALA-EXE-EXTERNAL.md` near 262 (the bank container) | RWE reads archive chunks only; saves are JSON (`BT-GAME-SETTINGS-CAMPAIGN-PARITY.md`) |
| 2 workspace, locking, 3 tree operations | none | not applicable (rule 5) |
| 4, 5 raw stream, compress and expand | `TA-DEMOS.md` 295-350 (the packet condenser: a clear bit is a literal, a set bit a 16-bit word, absolute offsets) | the format agrees; the window rule differs in one detail (rule 6) |
| 6 preset dictionary | none | rule 6 |
| 7 to 9 SQSH header, pack, unpack, error codes | none | rules 2, 4 |
| 10 zlib method | `hpi_util.test.cpp` (a stream without an adler trailer is accepted) | rule 4 |
| 11 size, encrypt, checksum helpers | none | rule 2 |

## 2. The chunk header and the checks around it

**Reference (sections 7, 9, 11).** The 19-byte header: magic `SQSH`, version byte (writer uses 2),
method byte, encryption flag byte, compressed payload length (excluding the header), expected decoded
length, and a **sum of the stored payload bytes**. The unpacker checks the magic, the method bound
(values of four or more are refused), the checksum over exactly the stored payload bytes **before**
decrypting, and afterwards the decoded length against the header. It **does not** validate the version
byte or any buffer length. The transform for the flag is, per byte index (low byte of the index): XOR
with the index, then add the index; decryption subtracts the index and XORs it back. It mutates the
caller's buffer. Method one is the raw LZSS stream, method two zlib; zero and three have no case.

**RWE.** `HpiChunk` (`hpi_headers.h`, packed to 19 bytes) and `extractCompressed` (`hpi_util.cpp`
about 197-265).

- **Layout: MATCHES.** Fields in the chapter's order and widths; 19 bytes.
- **Magic: MATCHES.** A wrong magic throws.
- **Version byte: MATCHES.** Not checked.
- **Checksum: MATCHES.** A wrapping sum of the bytes as unsigned, taken over the compressed body before
  the inner transform; a mismatch throws.
- **Inner transform: MATCHES.** `decryptInner` subtracts the low byte of the index and XORs it back,
  the chapter's decryption; applied only when the flag byte is non-zero.
- **Method byte: DIFFERS at zero and three.** RWE accepts **0** (a stored chunk, copied after checking
  that its two sizes are equal) and refuses 3 and above. The chapter's unpacker has no case for 0 or 3 and
  its result for them is an uninitialised length. No shipped archive stores a chunk uncompressed
  (`tools/hpi.py` says so), so the zero case is a feature RWE added for `HPIZ`-style tools; the
  original would not read such a chunk. A **DIVERGENCE** in RWE's favour with no retail effect.
- **Chunk size and cap: DIFFERS in RWE's favour.** The original has no destination capacity; RWE
  refuses a body over 1 MiB, a chunk that decodes past the file's declared size, and (at the file level)
  a size over 512 MiB (`HpiFileSystem.cpp` about 20).
- **Chunk size array: MATCHES in effect.** The array of per-chunk sizes at the start of a compressed
  file is read and not used; the chunk count is `ceil(size / 65536)` (the same in `hpi.py`).

## 3. The LZSS stream

**Reference (section 4).** A flag byte controls eight tokens, **low bit first**. A clear bit is one
literal byte; a set bit is a little-endian 16-bit word whose **high twelve bits are an absolute index
into a 4,096-byte ring** and whose low four bits are the length minus two (lengths 2 to 17). **An index
of zero ends the stream** whatever the low bits say. Output bytes enter the ring starting at position
1 and wrap at 4,096. The window is allocated **zeroed**. Match copies may overlap the bytes being
written.

**RWE.** `decompressLZ77` (about 66-138).

- **Token coding: MATCHES.** Low bit first; literal on a clear bit; word read as 16 bits; index in the
  high twelve; length is the low four bits plus two; a zero index returns before the length is used;
  the ring position starts at 1 and wraps by masking with 0xfff; a copy reads the ring byte by byte
  and writes it back at the write position, so overlap repeats.
- **Window contents: DIFFERS.** RWE's ring is a **stack array that is never initialised** (about
  line 68); the original and `tools/hpi.py` both start from zeros. A conforming stream never reads a
  ring position it has not written (the compressor can only reference strings it has inserted), so
  **no shipped chunk is affected**. A damaged or hostile stream that names an unwritten position copies
  whatever was on the stack into the decoded file: nondeterministic output, and a (small) information
  leak. See rule 8.
- **Truncation: DIFFERS in RWE's favour.** A stream that ends before its terminator, or inside a token,
  throws; the original would read past the end of the source.
- **Overrun: DIFFERS in RWE's favour.** Each literal and each copy checks the remaining output capacity
  and throws; the original has no destination bound at all.

## 4. Decoded length, the zlib path and what an error means

**Reference (sections 5, 9, 10).** After expansion the unpacker compares the number of bytes produced
with the header's expected length and returns error 3 on a mismatch (method one: the codec's returned
count; method two: the length initialised to the expected size and updated only by a **successful**
zlib call, whose return status is ignored, so a failed zlib decode can still look right). Result codes:
0 success, 1 wrong magic, 2 checksum, 3 length, 4 method, 5 packer capacity, 6 packer input.

**RWE.**

- **Length check: DIFFERS (a gap).** RWE checks only that a chunk does not exceed its declared size. A
  chunk whose stream ends (terminator for LZ77, a complete stream for zlib) **before** producing its
  declared number of bytes is accepted: the offset still advances by the declared size
  (`hpi_util.cpp` about 253 and 258) and the unfilled tail is zero (the caller's buffer is
  value-initialised, `HpiFileSystem.cpp` about 24), so a file decodes silently with zero holes. The
  chapter's method-one path rejects this. The zlib half of the chapter agrees with RWE's looseness in a
  different way: the original ignores the library's status, RWE accepts a complete stream of any length.
- **The sum of the chunks' decoded sizes is never compared with the file's size.** If the chunks add up
  to less than the file's declared size the remainder is zero. `tools/hpi.py` does check it.
- **zlib strictness: DIVERGENCE (documented in the tests).** RWE accepts a stream without its adler32
  trailer if every input byte was consumed and exactly the declared output came out (`HPIZ Archiver`
  wrote thousands, `hpi_util.test.cpp`); otherwise any inflate failure throws. The original ignores
  the status. Net: RWE refuses some damaged chunks the original passes, and accepts what the original
  accepts for shipped data.
- **Error codes: not applicable.** RWE reports by exception.

## 5. Packing, the preset dictionary, locking (not applicable)

**Reference (sections 2, 3, 5, 6, 8).** The packer, the compressor (look-ahead of 17, a binary tree of 4,096
positions, a trailing extra token after the terminator), the preset dictionary (up to 4,000 bytes,
copied from ring offset 0x13, snapshot kept in static buffers, enable and disable flags that survive),
the ticket lock keyed by thread id with re-entry, the workspace globals, `SquashMaxPackedSize`
(120 percent plus 119 for method one), the header dump and error-string helpers.

**RWE: not applicable.** RWE only expands, one archive at a time on one thread, with a stack ring
and no globals. It never writes SQSH, so the packer's output-capacity fault, the ignored zlib status
on pack, the extra trailing token and the negative-length encryption loop have no counterpart. The
reentrancy hazard (nested codec calls on shared pointers) cannot occur.

## 6. The TA packet codec reads the same stream

**Reference (sections 1, 4, 6).** The network condenser brackets its calls to the raw codec with the
preset enabled and uses its own checksum, byte transform and framing.

**Register.** `TA-DEMOS.md` 320-350 and `io/tad/tad_util.cpp` (`tadDecompress`): a control byte, eight
tokens, a set bit a little-endian word with the offset in the top twelve bits and the length minus two
below; **the offset is an absolute position in the output** (`offset + headerSize - 1`), reads past the
end of the output produce zero, offset zero ends. All 89 status messages and the demo corpus decode.

- **Format: MATCHES.** Same token layout and terminator.
- **Window model: UNCLEAR in two details.** The chapter's decoder indexes a **ring of 4,096 bytes**;
  RWE indexes the **whole output**. They agree while fewer than 4,096 bytes have been produced. For a
  longer payload the ring would have wrapped and overwritten early positions, so an offset near the
  start would mean the later bytes. TA packets are small and no shipped capture exceeds the limit, so
  nothing shows; a decoder meant for arbitrary packets should wrap. Second, **"reads past the end give
  zero"** matches the zeroed ring of the chapter *only if the preset is not used*. The chapter says the
  condenser enables a preset (its bytes are placed at ring offset 0x13 onwards); a packet that
  referenced the preset region would decode to the preset's bytes, not zeros. The corpus decoding
  cleanly shows that no captured packet needed it; whether TA's compressor ever references a preset
  that RWE's decoder does not hold is a spec-room question (what the preset bytes are).
- **The encoder: UNCLEAR, harmless.** `tadCompress` is a transcription from the demo project's reference,
  not the chapter's compressor (it matches only in the first 2,000 bytes, uses a linear search, and
  has no extra trailing token). The decoder stops at the terminator, so the differences cannot change
  what a real TA decodes; byte-for-byte equality with TA's own packets is not needed and was not
  checked.

## 7. `tools/hpi.py` against the C++

`CLAUDE.md` says the Python reader is a second implementation that "will drift" and that the C++ is the
one to believe. Five differences found, none affecting a shipped archive:

| Point | C++ | `tools/hpi.py` |
| --- | --- | --- |
| LZ77 ring | uninitialised | zeroed (the original's) |
| Chunk that stops short | accepted, tail zero, offset advances by the declared size | the short bytes are appended, so the final size check fails or later chunks shift |
| Chunks that add up to less than the file | accepted, tail zero | refused ("chunks gave ... not the declared") |
| A stored file cut off by the end of the archive | returns zeros for the missing tail | refused (its own test `test_a_stored_file_cut_short_by_the_end_of_the_archive_is_refused`) |
| Chunk header, checksum, inner transform, caps, depth and entry budget | as above | the same |

These are the places where the two should be made to agree; the Python one is the stricter and
closer to the chapter in the length check, the C++ is wrong in its ring.

## 8. Malformed input

`CLAUDE.md` ("Anything read from a file or a socket is someone else's") and `SECURITY-AUDIT.md` apply:
archives come from mods and downloads. `malformed_input.test.cpp` has three HPI cases (a root past its
block, a directory that contains itself, an entry name past the directory); `hpi_util.test.cpp` has the
zlib cases above; `tools/test_hpi.py` has a few dozen. The directory walk, the chunk header and the
output writes are bounded. What is not:

- **Unwritten LZ77 ring positions are read** (rule 3): a stream naming one copies uninitialised stack
  bytes into the decoded file. No memory-safety fault (the index is always 0 to 4,095); the harm is
  nondeterminism in what a mod pack contains, and stack contents leaking into a file the game may
  later write or display.
- **Short output is silent** (rule 4): a damaged chunk that still passes its checksum yields a file with
  a zero tail rather than an error.
- **The first reads of the archive trust `readRaw`.** The version, the header and the directory use
  `readRaw` (`io/io_util.h`), whose short-read test is an `assert`: a file shorter than 20 bytes gives
  an uninitialised marker and header. The marker test then almost always refuses it, and the directory
  size is capped at 64 MiB, so the effect is a refusal or a bounded, zero-filled allocation, not an
  overrun. The same helper underlies the GAF and FNT readers (`BT-GRAPHICS-GAF-PARITY.md` rule 8).
- **Directory memory.** The entry budget is the number of 9-byte records the block can hold (up to
  about 7.4 million at the 64 MiB cap); each decoded entry owns a string and a variant, so a hostile
  block near the cap can ask for several hundred megabytes. Bounded, not fixed.
- **Extraction errors reach `main`.** `HpiFileSystem::readFile` throws `HpiException`; the callers are
  the whole data-loading path, so one corrupt chunk in a mod archive ends the launch rather than
  hiding that file.

## 9. Gaps to fix

Ranked by what a modder or a player of a damaged download would hit. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Chunk decode length is not checked**: a chunk that stops early leaves a silent zero tail; chunk sizes need not add up to the file's. | DIFFERS (code) | code: require the produced count to equal the declared size, and the chunks to sum to it; add cases to `hpi_util.test.cpp` |
| 2 | **LZ77 ring is uninitialised** in C++ (zero in the original and in `hpi.py`). | fault | code: zero the ring; add a case that names an unwritten position |
| 3 | `tools/hpi.py` and the C++ disagree on short chunks, short files and the ring (rule 7). | drift | make them agree after rank 1 and 2 |
| 4 | One corrupt chunk aborts the launch (extraction exceptions are not caught per file). | fault | code: catch in the VFS and treat the file as absent |
| 5 | Stored chunks (method 0) are accepted; the original has no case for them. | DIVERGENCE | none |
| 6 | Packet decoder: absolute positions instead of a 4,096 ring, and zeros instead of a preset. | UNCLEAR | spec-room read of the preset bytes and of the ring wrap in the condenser |
| 7 | Directory memory near the 64 MiB cap. | UNCLEAR | cap the entry count by a smaller budget if it matters |

No source file was changed.
