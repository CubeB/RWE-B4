#!/usr/bin/env python3
"""Read a TA `.hpi` archive: list what is in one, and print a file out of one.

    python tools/hpi.py ~/ta/totala1.hpi .fbi
    python tools/hpi.py ~/ta/totala1.hpi units/armcarry.fbi --cat

An HPI is compressed throughout, so `grep` finds nothing in one -- not the
names, not the contents -- and the only other way in is the engine's own
reader. This is that reader, in a language you can run without a build.

Written against `src/rwe/io/hpi/`, which is the source of truth: every struct
in `hpi_headers.h`, the directory walk and its bounds in `HpiArchive.cpp`, and
the cipher, the checksum, `decompressLZ77` and `extractCompressed` in
`hpi_util.cpp`. A second implementation of a binary format drifts, so when one
of these two changes, change both -- and believe the C++.

Three things about the format are easy to get wrong, and each cost time to
find:

- `HpiVersion` is 8 bytes, so `HpiHeader` starts at offset **8**, not 4.
  Reading it at 4 gives a plausible `directorySize` of 65536 and garbage
  offsets after it.
- The directory block is the file's `[0, directorySize)`, the tree lives in
  `[start, directorySize)`, and that half is decrypted with a seed taken from
  `start`. The bytes before `start` are not part of the block.
- `HpiChunk` is **19** bytes packed -- 4+1+1+1+4+4+4 -- not a round number.

A reading tool only. RWE can read HPI and cannot write it, and nothing here is
a step toward writing one.

Checked against all thirteen archives of a TA 3.1 install -- `totala1-4.hpi`,
`worlds.hpi`, `tactics1-8.hpi` -- by extracting every one of their 3,571 files:
every chunk checksum verified and every file came back the length its directory
entry declares. `totala1/2/4` are encrypted (a non-zero key) and LZ77; the rest
are ZLib. **No shipped archive stores a file uncompressed**, so
`CompressionScheme::None` is written to match `HpiArchive::extract` and is
exercised only by a synthesised archive.
"""

import argparse
import struct
import sys
import zlib

HPI_MAGIC = 0x49504148  # "HAPI"
HPI_VERSION = 0x00010000  # HpiVersionNumber; "BANK" is a saved game, and is not this
CHUNK_MAGIC = 0x48535153  # "SQSH"

DIRECTORY_DATA_SIZE = 8  # HpiDirectoryData
FILE_DATA_SIZE = 9  # HpiFileData
DIRECTORY_ENTRY_SIZE = 9  # HpiDirectoryEntry
CHUNK_SIZE = 19  # HpiChunk, #pragma pack(1)
HEADER_SIZE = 20  # HpiVersion + HpiHeader, which is why the header starts at 8

# The engine's own caps, so a hostile archive cannot ask for more than the
# engine would have given it. HpiArchive.cpp for the first two, HpiArchive.h
# for MaxFileBytes, hpi_util.cpp for the last.
MAX_DIRECTORY_BYTES = 64 * 1024 * 1024
MAX_DIRECTORY_DEPTH = 128
MAX_FILE_BYTES = 512 * 1024 * 1024
MAX_CHUNK_BYTES = 1024 * 1024

SCHEME_NONE, SCHEME_LZ77, SCHEME_ZLIB = 0, 1, 2
SCHEME_NAMES = {SCHEME_NONE: 'none', SCHEME_LZ77: 'lz77', SCHEME_ZLIB: 'zlib'}


class HpiError(Exception):
    pass


def transform_key(key):
    """hpi_util.cpp transformKey. Takes the low byte of the header's key."""
    key &= 0xFF
    return ((key << 2) | (key >> 6)) & 0xFF


def decrypt(key, seed, buf):
    """hpi_util.cpp decrypt. The seed is the offset the read started at, which
    is why it is a parameter and not a running counter: two reads that begin at
    the same place decrypt the same way, and the directory read's seed is the
    root's offset in the file."""
    if key == 0:
        return bytes(buf)
    return bytes((((seed + i) & 0xFF) ^ key) ^ b for i, b in enumerate(buf))


def decrypt_inner(buf):
    """hpi_util.cpp decryptInner: the second pass, over a chunk body."""
    return bytes(((b - i) & 0xFF) ^ (i & 0xFF) for i, b in enumerate(buf))


def compute_checksum(buf):
    """hpi_util.cpp computeChecksum: a wrapping sum of the bytes as unsigned."""
    return sum(buf) & 0xFFFFFFFF


def decompress_lz77(src, max_bytes):
    """hpi_util.cpp decompressLZ77. A 4 KiB window seeded at position 1, and a
    copy that reads forward through the window as it writes, so a run longer
    than the distance back repeats what it has only just written."""
    window = bytearray(4096)
    out = bytearray()
    in_pos = 0
    window_pos = 1
    n = len(src)

    while True:
        if in_pos >= n:
            raise HpiError('LZ77 expected a tag byte and got end of input')
        tag = src[in_pos]
        in_pos += 1

        for _ in range(8):
            if (tag & 1) == 0:  # a literal byte
                if in_pos >= n:
                    raise HpiError('LZ77 expected a literal byte and got end of input')
                if len(out) >= max_bytes:
                    raise HpiError('LZ77 ran over its declared output size')
                out.append(src[in_pos])
                window[window_pos] = src[in_pos]
                window_pos = (window_pos + 1) & 0xFFF
                in_pos += 1
            else:  # an offset and a length into the window
                if in_pos >= n - 1:
                    raise HpiError('LZ77 expected a window offset and got end of input')
                packed = src[in_pos] | (src[in_pos + 1] << 8)
                in_pos += 2
                offset = packed >> 4
                if offset == 0:
                    return bytes(out)
                count = (packed & 0x0F) + 2
                if len(out) + count > max_bytes:
                    raise HpiError('LZ77 ran over its declared output size')
                for _ in range(count):
                    out.append(window[offset])
                    window[window_pos] = window[offset]
                    offset = (offset + 1) & 0xFFF
                    window_pos = (window_pos + 1) & 0xFFF
            tag >>= 1


def decompress_zlib(src, max_bytes):
    """hpi_util.cpp decompressZLib, including its second definition of done: a
    stream that ends without an adler32 trailer counts, so long as every input
    byte was consumed and the declared number of output bytes came out.
    HPIZ Archiver wrote thousands of those into the V Maps pack, and the engine
    reads them. `zlib.decompress` refuses them, so this does not use it."""
    stream = zlib.decompressobj()
    try:
        out = stream.decompress(src, max_bytes)
    except zlib.error as e:
        raise HpiError('ZLib decompress failed: %s' % e)
    if not stream.eof and (stream.unconsumed_tail or stream.unused_data or len(out) != max_bytes):
        raise HpiError('ZLib decompress failed')
    return out


class Cursor:
    """A read cursor over the raw archive. Every read is decrypted, seeded from
    the offset it starts at, which is what readAndDecrypt does with tellg."""

    def __init__(self, data, key, pos=0):
        self.data = data
        self.key = key
        self.pos = pos

    def read(self, n):
        seed = self.pos
        if seed + n > len(self.data):
            raise HpiError('read of %d bytes at %d runs past the end of the archive' % (n, seed))
        self.pos = seed + n
        return decrypt(self.key, seed & 0xFF, self.data[seed:seed + n])


class Entry:
    """One file in the archive. Lookups are case-insensitive, as the engine's
    findFile is: TA's own archives are inconsistent about it."""

    def __init__(self, path, offset, size, scheme):
        self.path = path
        self.offset = offset
        self.size = size
        self.scheme = scheme


class HpiArchive:
    """A whole archive, read and walked on open. `entries` maps a lower-cased
    path to an Entry; `read` extracts one."""

    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.raw = f.read()

        if len(self.raw) < HEADER_SIZE:
            raise HpiError('%s is too short to be an HPI' % path)
        marker, version = struct.unpack_from('<II', self.raw, 0)
        if marker != HPI_MAGIC:
            raise HpiError('invalid HPI marker %08x' % marker)
        if version != HPI_VERSION:
            raise HpiError('unsupported HPI version %08x' % version)

        # HpiHeader follows the 8-byte HpiVersion.
        directory_size, header_key, start = struct.unpack_from('<III', self.raw, 8)
        self.key = transform_key(header_key)

        # Both checked before the read, not after it, as in HpiArchive.cpp: with
        # start past the end, the length directorySize - start wraps round to
        # four billion.
        if directory_size > MAX_DIRECTORY_BYTES:
            raise HpiError('directory block of %d bytes is over the cap' % directory_size)
        if start + DIRECTORY_DATA_SIZE > directory_size:
            raise HpiError('root directory at %d is past the end of the %d-byte block' % (start, directory_size))

        # The block is the file's first directorySize bytes; the tree is the
        # part from start on. The bytes before start are whatever the archiver
        # left there, and the engine leaves them alone too. An archive that
        # stops short of its own directorySize is zero-filled to it, as the
        # engine's buffer is, and then fails the walk below rather than reading
        # off the end of anything.
        self.directory_size = directory_size
        block = bytearray(self.raw[0:directory_size])
        block += bytes(directory_size - len(block))
        block[start:directory_size] = decrypt(self.key, start & 0xFF, self.raw[start:directory_size])
        self.block = bytes(block)

        # The engine's DirectoryWalkBudget: every entry in a real archive is a
        # distinct record in the block, so a count past that means entries
        # pointing back at their own ancestors. Issue #75.
        self.entries = {}
        self._walk(start, '', 0, [directory_size // DIRECTORY_ENTRY_SIZE])

    def _walk(self, offset, prefix, depth, budget):
        """convertDirectory and convertDirectoryEntry. Refuses rather than
        returning quietly: an archive that lies about its own shape is not one
        to carry on reading."""
        if offset + DIRECTORY_DATA_SIZE > self.directory_size:
            raise HpiError('directory data at %d is past the end of the block' % offset)
        number_of_entries, entry_list = struct.unpack_from('<II', self.block, offset)
        if entry_list + number_of_entries * DIRECTORY_ENTRY_SIZE > self.directory_size:
            raise HpiError('runaway directory entry list at %d' % entry_list)
        if number_of_entries > budget[0]:
            raise HpiError('directory tree has more entries than the archive holds')
        budget[0] -= number_of_entries
        if depth + 1 > MAX_DIRECTORY_DEPTH:
            raise HpiError('directory tree nested more than %d deep' % MAX_DIRECTORY_DEPTH)

        for i in range(number_of_entries):
            name_offset, data_offset, is_directory = struct.unpack_from(
                '<IIB', self.block, entry_list + i * DIRECTORY_ENTRY_SIZE)
            if name_offset >= self.directory_size:
                raise HpiError('runaway directory entry name at %d' % name_offset)
            end = self.block.find(b'\0', name_offset)
            if end < 0:
                raise HpiError('unterminated directory entry name at %d' % name_offset)
            name = self.block[name_offset:end].decode('latin-1')
            path = prefix + '/' + name if prefix else name

            if is_directory:
                self._walk(data_offset, path, depth + 1, budget)
            else:
                if data_offset + FILE_DATA_SIZE > self.directory_size:
                    raise HpiError('runaway file data offset at %d' % data_offset)
                offset_of_data, size, scheme = struct.unpack_from('<IIB', self.block, data_offset)
                self.entries[path.lower()] = Entry(path, offset_of_data, size, scheme)

    def read(self, path):
        """The bytes of one entry, or a refusal. A reader that quietly returns
        the wrong bytes is worse than one that will not answer."""
        entry = self.entries.get(path.lower())
        if entry is None:
            raise HpiError('no %s in %s' % (path, self.path))
        # Taken from the archive and used to size an allocation, so it is
        # checked here rather than where the engine checks it (HpiFileSystem).
        if entry.size > MAX_FILE_BYTES:
            raise HpiError('%s declares %d bytes, over the cap' % (entry.path, entry.size))

        if entry.scheme == SCHEME_NONE:
            return Cursor(self.raw, self.key, entry.offset).read(entry.size)
        if entry.scheme not in (SCHEME_LZ77, SCHEME_ZLIB):
            raise HpiError('%s has compression scheme %d' % (entry.path, entry.scheme))
        return self._extract_compressed(entry)

    def _extract_compressed(self, entry):
        """hpi_util.cpp extractCompressed: a chunkCount of sizes, then an
        HpiChunk header and a body per chunk. The sizes are read and, as in the
        engine, not used -- chunkCount is what says how many chunks there are.
        """
        cursor = Cursor(self.raw, self.key, entry.offset)
        chunk_count = entry.size // 65536 + (0 if entry.size % 65536 == 0 else 1)
        cursor.read(4 * chunk_count)

        out = bytearray()
        for _ in range(chunk_count):
            marker, _version, scheme, encrypted, compressed, decompressed, checksum = \
                struct.unpack('<IBBBIII', cursor.read(CHUNK_SIZE))
            if marker != CHUNK_MAGIC:
                raise HpiError('%s: invalid chunk header' % entry.path)
            if len(out) + decompressed > entry.size:
                raise HpiError('%s: chunks decompress past the declared %d bytes' % (entry.path, entry.size))
            if compressed > MAX_CHUNK_BYTES:
                raise HpiError('%s: chunk of %d bytes is over the cap' % (entry.path, compressed))

            body = cursor.read(compressed)
            # Over the compressed bytes, before the inner pass, and a refusal
            # rather than a correction: a silent wrong answer is the one
            # failure a reader like this must not have.
            if compute_checksum(body) != checksum:
                raise HpiError('%s: chunk checksum mismatch' % entry.path)
            if encrypted:
                body = decrypt_inner(body)

            # The chunk's own scheme, which is not the entry's: an entry
            # declares LZ77 or ZLib and each chunk inside it may be stored.
            if scheme == SCHEME_NONE:
                if compressed != decompressed:
                    raise HpiError('%s: uncompressed chunk with two different sizes' % entry.path)
                out += body
            elif scheme == SCHEME_LZ77:
                out += decompress_lz77(body, decompressed)
            elif scheme == SCHEME_ZLIB:
                out += decompress_zlib(body, decompressed)
            else:
                raise HpiError('%s: invalid chunk compression scheme %d' % (entry.path, scheme))

        if len(out) != entry.size:
            raise HpiError('%s: chunks gave %d bytes, not the declared %d' % (entry.path, len(out), entry.size))
        return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('archive', help='path to a .hpi')
    ap.add_argument('pattern', nargs='?', default='',
                    help='substring to match entry paths against; with --cat, the path of the entry to print')
    ap.add_argument('--cat', action='store_true',
                    help='print the entry named by the pattern instead of listing matches')
    args = ap.parse_args()

    archive = HpiArchive(args.archive)
    needle = args.pattern.lower()

    if args.cat:
        entry = archive.entries.get(needle)
        if entry is None:
            print('hpi.py: no entry %r in %s' % (args.pattern, args.archive), file=sys.stderr)
            near = [e.path for e in archive.entries.values() if needle in e.path.lower()]
            if near:
                print('hpi.py: did you mean one of:', file=sys.stderr)
                for p in near[:20]:
                    print('hpi.py:   ' + p, file=sys.stderr)
            return 1
        sys.stdout.buffer.write(archive.read(entry.path))
        sys.stdout.buffer.flush()
        return 0

    for path in sorted(archive.entries):
        if needle in path:
            e = archive.entries[path]
            print('%-64s %9d  %s' % (e.path, e.size, SCHEME_NAMES.get(e.scheme, e.scheme)))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (HpiError, OSError) as e:
        sys.exit('hpi.py: %s' % e)
