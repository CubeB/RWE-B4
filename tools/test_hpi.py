"""Unit tests for tools/hpi.py -- no engine, no game data, no subprocess.

Run with the MinGW python on this machine (stdlib only, no pip install):

    /d/msys64/mingw64/bin/python.exe tools/test_hpi.py -v

With a data set, which also runs the last two cases:

    RWE_SCENARIO_DATA=~/ta /d/msys64/mingw64/bin/python.exe tools/test_hpi.py

`src/rwe/io/hpi/` is the source of truth, so every assertion here is about what
the engine does -- not about what the Python happens to do. The refusals are
the three HPI cases from `src/rwe/io/malformed_input.test.cpp` transcribed byte
for byte, plus the bounds `HpiArchive.cpp` and `hpi_util.cpp` hold.

Nothing here needs a copy of the game: the archives are built in memory by
`build_archive` below. That is the only way to reach `CompressionScheme::None`
-- no shipped archive stores a file uncompressed -- and the only way to put a
stored file at a file offset that is not a multiple of 256, which is where a
seed taken from zero instead of from `file.offset` shows.

`RealArchiveTest` runs when `RWE_SCENARIO_DATA` names a directory of `.hpi`
files. It is skipped, loudly, when it cannot: game data is not in the
repository, so a pass that quietly read nothing would be worse than one that
says it read nothing.

Like `tools/test_ai_autotune.py`, this is run by hand. Neither is in CI.
"""
import importlib.util
import os
import struct
import sys
import tempfile
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def _load(name, filename):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, filename))
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


hpi = _load("hpi_mod", "hpi.py")

HAPI = 0x49504148
VERSION = 0x00010000
SQSH = 0x48535153
HEADER_SIZE = 20  # HpiVersion + HpiHeader, so HpiHeader starts at 8
CHUNK_SIZE = 19  # HpiChunk, #pragma pack(1): 4+1+1+1+4+4+4

# 191 transforms to 254, the key totala1/2/4 carry: a non-zero key, so the seed
# is load-bearing rather than a no-op.
ENCRYPTED_KEY = 191


def encrypt_inner(buf):
    """The inverse of hpi_util.cpp decryptInner, which is (b - i) ^ i."""
    return bytes(((c ^ i) + i) & 0xFF for i, c in enumerate(buf))


def lz77_compress(payload):
    """A literal-only LZ77 stream. Each group of eight is a 0x00 tag byte and
    eight literals; a short last group carries a tag whose high bits are set,
    and the back-reference they name is the offset-0 terminator that ends the
    stream -- there is no way to pad a group with literals, so the terminator
    has to live inside one."""
    out = bytearray()
    i = 0
    while len(payload) - i >= 8:
        out.append(0)
        out += payload[i:i + 8]
        i += 8
    k = len(payload) - i
    if k:
        out.append(((1 << (8 - k)) - 1) << k)
        out += payload[i:]
    else:
        out.append(0xFF)  # a tag of eight back-references; the first ends it
    out += struct.pack('<H', 0)
    return bytes(out)


def chunked(payload, scheme, encrypted=1):
    """The bytes of a scheme-1 or scheme-2 entry: a chunkCount of sizes, then a
    header and a body per chunk, split at 64 KiB as extractCompressed counts.
    The inner pass is per body, because the reader restarts it at zero for each
    chunk, and the checksum covers the body as stored."""
    count = len(payload) // 65536 + (0 if len(payload) % 65536 == 0 else 1)
    out = bytearray(struct.pack('<%dI' % count, *([1] * count)))
    for i in range(count):
        piece = payload[i * 65536:(i + 1) * 65536]
        comp = lz77_compress(piece) if scheme == 'lz77' else zlib.compress(piece, 9)
        sch = 1 if scheme == 'lz77' else 2
        body = encrypt_inner(comp) if encrypted else comp
        out += struct.pack('<IBBBIII', SQSH, 0, sch, encrypted, len(body), len(piece), sum(body) & 0xFFFFFFFF)
        out += body
    return bytes(out)


def build_archive(files, key_byte=0, start=20, version=VERSION, marker=HAPI,
                   declared=None, encrypted=1):
    """Build a whole HPI in memory. This is a writer for the tests only: the
    reader under test is a reading tool, and RWE cannot write an HPI either.

    `files` is a list of (path, payload, scheme), where scheme is 0 for a
    stored file -- `HpiFileData.compressionScheme`, so no chunks at all -- or
    'lz77' / 'zlib' / 'none' for an entry carrying one chunk. `declared`
    overrides the fileSize written into the first HpiFileData, to make an
    archive that lies about itself.

    The layout is a real one: the directory block is the file's first
    `directorySize` bytes including the 20-byte header, and the file data starts
    after it. `dataOffset` is a file offset, so getting that wrong is a mistake
    this function is written to be able to make.
    """
    key = hpi.transform_key(key_byte)
    data_record_at = start + 8  # after the root HpiDirectoryData
    entry_record_at = data_record_at + 9 * len(files)
    name_at = entry_record_at + 9 * len(files)

    names = bytearray()
    for path, _, _ in files:
        names += path.encode('latin-1') + b'\0'
    directory_size = name_at + len(names)

    blobs = []
    running = directory_size
    for path, payload, scheme in files:
        if scheme == 0:
            blob = payload
        elif scheme == 'none':  # one stored chunk inside a chunked entry
            body = encrypt_inner(payload) if encrypted else payload
            blob = struct.pack('<I', 1) + struct.pack(
                '<IBBBIII', SQSH, 0, 0, encrypted, len(body), len(payload),
                sum(body) & 0xFFFFFFFF) + body
        else:
            blob = chunked(payload, scheme, encrypted)
        blobs.append((running, blob))
        running += len(blob)

    block = bytearray(directory_size - HEADER_SIZE)
    block[start - HEADER_SIZE:start - HEADER_SIZE + 8] = struct.pack('<II', len(files), entry_record_at)
    for i, (path, _, _) in enumerate(files):
        name_off = name_at + sum(len(p.encode('latin-1')) + 1 for p, _, _ in files[:i])
        block[entry_record_at + 9 * i - HEADER_SIZE:entry_record_at + 9 * i - HEADER_SIZE + 9] = \
            struct.pack('<IIB', name_off, data_record_at + 9 * i, 0)
    for i, (offset, blob) in enumerate(blobs):
        # fileSize is the size of the file, not of what is stored for it.
        size = declared if (declared is not None and i == 0) else len(files[i][1])
        at = data_record_at + 9 * i - HEADER_SIZE
        block[at:at + 9] = struct.pack('<IIB', offset, size, 0 if files[i][2] == 0 else 2)
    block[name_at - HEADER_SIZE:name_at - HEADER_SIZE + len(names)] = names
    block[start - HEADER_SIZE:] = hpi.decrypt(key, start & 0xFF, bytes(block[start - HEADER_SIZE:]))

    out = struct.pack('<II', marker, version) + struct.pack('<III', directory_size, key_byte, start) + bytes(block)
    for offset, blob in blobs:
        out += hpi.decrypt(key, offset & 0xFF, blob)
    return out


def header(directory_size, start, key_byte=0, marker=HAPI, version=VERSION):
    """Just the 20-byte header, for the hand-built cases."""
    return struct.pack('<II', marker, version) + struct.pack('<III', directory_size, key_byte, start)


class HpiTestCase(unittest.TestCase):
    """Shared plumbing: a scratch directory per test, and the one way a case
    asserts that an archive is refused."""

    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.addCleanup(self._dir.cleanup)

    def write(self, archive_bytes, name='test.hpi'):
        path = os.path.join(self._dir.name, name)
        with open(path, 'wb') as f:
            f.write(archive_bytes)
        return path

    def assertRefused(self, archive_bytes, needle=None, name='test.hpi'):
        """Opening the archive, or reading every entry in it, must raise
        HpiError -- and say why, if the caller cares which check fired. Most
        refusals happen while opening, so both are inside the try."""
        try:
            archive = hpi.HpiArchive(self.write(archive_bytes, name))
            for entry in sorted(archive.entries.values(), key=lambda e: e.path):
                archive.read(entry.path)
        except hpi.HpiError as e:
            if needle is not None:
                self.assertIn(needle, str(e))
            return str(e)
        self.fail('the archive was accepted')


class StoredFileTest(HpiTestCase):
    """`HpiFileData.compressionScheme` 0, which no shipped archive uses: a plain
    readAndDecrypt of fileSize bytes at file.offset, with no chunks."""

    PAYLOAD = b'[UNITINFO]{UnitName=STORED;}' * 7

    def test_a_stored_file_comes_back(self):
        a = hpi.HpiArchive(self.write(build_archive([('f.txt', self.PAYLOAD, 0)])))
        self.assertEqual(a.read('f.txt'), self.PAYLOAD)

    def test_a_stored_file_is_seeded_from_its_offset_not_from_zero(self):
        # The padding moves the file to an offset that is not a multiple of 256,
        # where seeding the cipher from zero gives a different answer. Reading a
        # stored file through a cursor at offset 0 is the bug this pins.
        arc = build_archive([('pad', b'x' * 300, 0), ('f.txt', self.PAYLOAD, 0)],
                            key_byte=ENCRYPTED_KEY)
        a = hpi.HpiArchive(self.write(arc))
        offset = a.entries['f.txt'].offset
        self.assertNotEqual(offset % 256, 0, 'the offset has to distinguish the two seeds')
        self.assertNotEqual(hpi.decrypt(a.key, 0, arc[offset:offset + len(self.PAYLOAD)]), self.PAYLOAD)
        self.assertEqual(a.read('f.txt'), self.PAYLOAD)

    def test_a_stored_file_in_an_unencrypted_archive(self):
        # key 0, so decrypt is a no-op and the seed cannot matter at all: the
        # other end of the range from the test above.
        a = hpi.HpiArchive(self.write(build_archive([('f.txt', self.PAYLOAD, 0)], key_byte=0)))
        self.assertEqual(a.key, 0)
        self.assertEqual(a.read('f.txt'), self.PAYLOAD)

    def test_an_empty_file_reads_as_empty(self):
        a = hpi.HpiArchive(self.write(build_archive([('empty.txt', b'', 0)])))
        self.assertEqual(a.read('empty.txt'), b'')

    def test_a_stored_file_cut_short_by_the_end_of_the_archive_is_refused(self):
        arc = build_archive([('f.txt', self.PAYLOAD, 0)])
        self.assertRefused(arc[:HEADER_SIZE + 40], 'past the end of the archive')


class ChunkedFileTest(HpiTestCase):
    """The paths a real archive exercises: LZ77, ZLib, and a stored chunk."""

    LZ = b'lz77 payload, ' * 100
    Z = b'zlib payload, ' * 100
    RAW = b'an uncompressed chunk, ' * 50
    # Three chunks, so chunkCount is past 1 and the 64 KiB split is exercised.
    # ZLib, because the split is the thing under test and not the codec, and
    # pure-Python LZ77 over 128 KiB is slow. The LZ77 decoder is exercised on
    # the small payloads here and over multi-chunk files by RealArchiveTest.
    MULTI = bytes((i * 7 + 3) & 0xFF for i in range(65536 * 2 + 100))

    EXPECT = {
        'a/stored.txt': StoredFileTest.PAYLOAD,
        'a/lz.bin': LZ,
        'a/zlib.bin': Z,
        'a/plainchunk.bin': RAW,
        'a/three-chunks.bin': MULTI,
        'top.txt': bytes(range(256)) * 3,
    }

    FILES = [
        ('a/stored.txt', StoredFileTest.PAYLOAD, 0),
        ('a/lz.bin', LZ, 'lz77'),
        ('a/zlib.bin', Z, 'zlib'),
        ('a/plainchunk.bin', RAW, 'none'),
        ('a/three-chunks.bin', MULTI, 'zlib'),
        ('top.txt', bytes(range(256)) * 3, 0),
    ]

    def _roundtrip(self, **kw):
        a = hpi.HpiArchive(self.write(build_archive(self.FILES, **kw)))
        self.assertEqual(sorted(a.entries), sorted(self.EXPECT))
        for name, want in self.EXPECT.items():
            self.assertEqual(a.read(name), want, name)

    def test_unencrypted_archive(self):
        self._roundtrip(key_byte=0)

    def test_encrypted_archive(self):
        # A non-zero key, so the directory read, the chunk-count sizes, the
        # chunk headers and the bodies are all decrypted.
        self._roundtrip(key_byte=ENCRYPTED_KEY)

    def test_a_chunk_body_that_is_not_inner_encrypted(self):
        a = hpi.HpiArchive(self.write(build_archive([('f.bin', self.LZ, 'lz77')], encrypted=0)))
        self.assertEqual(a.read('f.bin'), self.LZ)

    def test_lookup_is_case_insensitive(self):
        a = hpi.HpiArchive(self.write(build_archive([('Units/ARMCARRY.FBI', self.Z, 'zlib')])))
        self.assertEqual(a.read('units/armcarry.fbi'), self.Z)
        self.assertEqual(a.read('UNITS/ARMCARRY.fbi'), self.Z)

    def test_an_entry_that_is_not_there_is_refused(self):
        a = hpi.HpiArchive(self.write(build_archive([('f.bin', self.Z, 'zlib')])))
        with self.assertRaises(hpi.HpiError) as ctx:
            a.read('g.bin')
        self.assertIn('no g.bin', str(ctx.exception))


class WalkTest(HpiTestCase):
    """The directory walk, and the shape of the tree it produces."""

    def test_nested_paths_are_joined_with_slashes(self):
        a = hpi.HpiArchive(self.write(build_archive(
            [('a/b/c/deep.txt', b'deep', 0), ('a/b/other.txt', b'other', 0)])))
        self.assertEqual(sorted(a.entries), ['a/b/c/deep.txt', 'a/b/other.txt'])
        self.assertEqual(a.read('a/b/c/deep.txt'), b'deep')

    def test_a_name_with_a_space_in_it(self):
        # 'yerrot mountains.ota' and 'features/all worlds' are both real.
        a = hpi.HpiArchive(self.write(build_archive([('maps/a map.ota', b'map', 0)])))
        self.assertEqual(a.read('maps/a map.ota'), b'map')

    def test_a_non_ascii_name_survives(self):
        name = 'maps/ünicode.ota'
        a = hpi.HpiArchive(self.write(build_archive([(name, b'map', 0)])))
        self.assertEqual(a.read(name), b'map')


class MalformedHeaderTest(HpiTestCase):
    """The header, as HpiArchive's constructor reads it."""

    def test_a_bad_marker_is_refused(self):
        self.assertRefused(header(20, 20, marker=0xDEADBEEF) + b'\0' * 20, 'invalid HPI marker')

    def test_a_bank_saved_game_is_refused(self):
        # HpiBankMagicNumber: a saved game, which HpiArchive refuses as an
        # unsupported version.
        self.assertRefused(header(20, 20, version=0x4B4E4142) + b'\0' * 20, 'unsupported HPI version')

    def test_a_file_too_short_for_a_header_is_refused(self):
        self.assertRefused(b'HAPI', 'too short')

    def test_a_marker_but_no_header_is_refused(self):
        self.assertRefused(struct.pack('<II', HAPI, VERSION) + b'\0' * 4, 'too short')

    def test_a_directory_block_over_the_cap_is_refused(self):
        # HpiArchive::MaxDirectoryBytes, checked before the read: with start
        # past the end the length directorySize - start wraps to four billion.
        self.assertRefused(header(64 * 1024 * 1024 + 1, 20) + b'\0' * 32, 'over the cap')

    def test_a_root_past_the_end_of_its_own_block_is_refused(self):
        self.assertRefused(header(20, 20) + b'\0' * 20, 'past the end of the')

    def test_an_empty_file_is_refused(self):
        self.assertRefused(b'', 'too short')


class MalformedInputTest(HpiTestCase):
    """The three HPI cases from src/rwe/io/malformed_input.test.cpp, byte for
    byte, and then the rest of what HpiArchive.cpp holds."""

    def test_root_directory_past_its_own_end(self):
        # hpiPrefix(10, 100) then 200 bytes of filler. The read used to fill
        # from start to directorySize, a length that wrapped to four billion,
        # into a buffer of ten bytes.
        self.assertRefused(header(10, 100) + b'x' * 200, 'past the end of the')

    def test_a_directory_that_contains_itself(self):
        # Root directory data at 20: one entry, listed at 28. The entry is a
        # directory whose data is the root's own, at 20 again.
        b = header(39, 20) + struct.pack('<II', 1, 28) + struct.pack('<IIB', 37, 20, 1) + b'a\0'
        self.assertEqual(len(b), 39)
        self.assertRefused(b, 'more entries than the archive holds')

    def test_an_entry_name_past_the_directory(self):
        b = header(37, 20) + struct.pack('<II', 1, 28) + struct.pack('<IIB', 5000, 20, 0)
        self.assertRefused(b, 'runaway directory entry name')

    def test_a_directory_claiming_four_billion_entries(self):
        self.assertRefused(header(28, 20) + struct.pack('<II', 0xFFFFFFFF, 28) + b'\0' * 8,
                           'runaway directory entry list')

    def test_a_directory_entry_list_past_the_block(self):
        self.assertRefused(header(40, 20) + struct.pack('<II', 1, 0xFFFFFFF0) + b'\0' * 12,
                           'runaway directory entry list')

    def test_a_file_data_offset_past_the_block(self):
        b = header(39, 20) + struct.pack('<II', 1, 28) + struct.pack('<IIB', 37, 0xFFFFFFF0, 0) + b'a\0'
        self.assertRefused(b, 'runaway file data offset')

    def test_an_unterminated_entry_name(self):
        b = header(39, 20) + struct.pack('<II', 1, 28) + struct.pack('<IIB', 37, 20, 0) + b'abcdefgh'
        self.assertRefused(b, 'unterminated directory entry name')

    def test_a_file_size_over_the_cap(self):
        self.assertRefused(build_archive([('f.txt', b'x' * 100, 0)], declared=0xFFFFFFFF),
                           'over the cap')

    def test_a_tree_nested_past_the_depth_limit(self):
        # 200 nested directories, one entry each, every record inside the block.
        # Only MaxDirectoryDepth can stop this: the walk budget is 19*201/9 = 424
        # entries and the tree spends 201.
        levels = 200
        size = HEADER_SIZE + 19 * (levels + 1)
        deep = bytearray(size)
        deep[0:8] = struct.pack('<II', HAPI, VERSION)
        deep[8:20] = struct.pack('<III', size, 0, HEADER_SIZE)
        for i in range(levels + 1):
            at = HEADER_SIZE + 19 * i
            deep[at:at + 8] = struct.pack('<II', 1, at + 8)
            deep[at + 8:at + 17] = struct.pack('<IIB', at + 17, at + 19, 1)
            deep[at + 17:at + 19] = b'x\0'
        self.assertRefused(bytes(deep), 'nested more than 128 deep')

    def test_two_directories_that_name_each_other(self):
        # Bounded by neither a depth limit of 128 nor any one directory's size:
        # only the walk budget, which is what issue #75 is about.
        cycle = bytearray(64)
        cycle[0:8] = struct.pack('<II', HAPI, VERSION)
        cycle[8:20] = struct.pack('<III', 64, 0, 20)
        cycle[20:28] = struct.pack('<II', 1, 28)
        cycle[28:37] = struct.pack('<IIB', 54, 37, 1)
        cycle[37:45] = struct.pack('<II', 1, 45)
        cycle[45:54] = struct.pack('<IIB', 54, 20, 1)
        cycle[54:56] = b'a\0'
        self.assertRefused(bytes(cycle), 'more entries than the archive holds')

    def test_a_block_that_stops_short_of_its_own_end(self):
        # Zero-filled to directorySize, as the engine's buffer is, then refused
        # by the walk rather than read off the end of anything.
        arc = build_archive([('f.txt', b'x' * 100, 0)])
        self.assertRefused(arc[:len(arc) - 40], 'past the end of the archive')


class MalformedChunkTest(HpiTestCase):
    """The bounds inside extractCompressed. Each case corrupts one field and
    leaves the checksum right, so the check under test is the one that fires
    rather than the one in front of it."""

    PAYLOAD = b'hello, ' * 40

    def setUp(self):
        super().setUp()
        self.arc = build_archive([('f.bin', self.PAYLOAD, 'zlib')])
        archive = hpi.HpiArchive(self.write(self.arc, 'good.hpi'))
        self.at = archive.entries['f.bin'].offset + 4  # past the chunkCount size
        self.body_length = struct.unpack('<IBBBIII', self.arc[self.at:self.at + CHUNK_SIZE])[4]
        self.checksum = sum(self.arc[self.at + CHUNK_SIZE:self.at + CHUNK_SIZE + self.body_length]) & 0xFFFFFFFF

    def patched(self, **kw):
        fields = {'marker': SQSH, 'version': 0, 'scheme': 2, 'encrypted': 1,
                  'compressed': self.body_length, 'decompressed': 10, 'checksum': self.checksum}
        fields.update(kw)
        b = bytearray(self.arc)
        b[self.at:self.at + CHUNK_SIZE] = struct.pack(
            '<IBBBIII', fields['marker'], fields['version'], fields['scheme'],
            fields['encrypted'], fields['compressed'], fields['decompressed'], fields['checksum'])
        return bytes(b)

    def test_a_marker_that_is_not_sqsh(self):
        self.assertRefused(self.patched(marker=0), 'invalid chunk header')

    def test_a_chunk_that_decompresses_past_the_declared_size(self):
        self.assertRefused(self.patched(decompressed=1 << 20), 'decompress past the declared')

    def test_a_chunk_over_the_cap(self):
        self.assertRefused(self.patched(compressed=1 << 21), 'over the cap')

    def test_an_unknown_chunk_scheme(self):
        self.assertRefused(self.patched(scheme=7), 'invalid chunk compression scheme 7')

    def test_a_stored_chunk_with_two_different_sizes(self):
        self.assertRefused(self.patched(scheme=0, decompressed=self.body_length + 1),
                           'two different sizes')

    def test_a_wrong_checksum(self):
        self.assertRefused(self.patched(checksum=self.checksum ^ 1), 'checksum mismatch')

    def test_a_body_that_runs_past_the_end_of_the_archive(self):
        self.assertRefused(self.patched(compressed=0xFFFF), 'past the end of the archive')

    def test_a_body_of_no_bytes(self):
        self.assertRefused(self.patched(compressed=0, checksum=0), 'decompress failed')

    def test_chunks_that_decompress_past_the_file_size(self):
        # The entry says 30 bytes and the chunk holds 40: the size check in
        # extractCompressed fires before the decompressor is reached.
        self.assertRefused(build_archive([('f.bin', self.PAYLOAD, 'zlib')], declared=30),
                           'decompress past the declared')

    def test_a_truncated_chunk_body(self):
        self.assertRefused(self.arc[:self.at + CHUNK_SIZE + 4], 'past the end of the archive')


class Lz77Test(unittest.TestCase):
    """decompressLZ77, on the inputs that decide whether it refuses."""

    def test_a_literal_only_stream_round_trips(self):
        # The group boundaries are where an encoder that pads with literals
        # cannot terminate, and 1300 is not a multiple of 8.
        for n in (1, 3, 7, 8, 9, 100, 1300, 4096):
            payload = bytes((i * 7 + 3) & 0xFF for i in range(n))
            self.assertEqual(hpi.decompress_lz77(lz77_compress(payload), n), payload, n)

    def test_a_run_that_repeats_what_it_just_wrote(self):
        # A tag bit is read low first: 0 is a literal, 1 a back-reference. So
        # 0b110 is "a literal, then two back-references" -- the first of which
        # copies offset 1 for 17 bytes, so the one literal it reads becomes
        # eighteen, which only works if the copy reads forward through the
        # window as it writes.
        stream = bytes([0b110]) + b'Z' + struct.pack('<H', (1 << 4) | (17 - 2)) + struct.pack('<H', 0)
        self.assertEqual(hpi.decompress_lz77(stream, 18), b'Z' * 18)

    def test_the_offset_zero_terminator_ends_the_stream(self):
        # 0b010: one literal, then the back-reference with offset 0, which is
        # how a stream says it is finished rather than running out of input.
        self.assertEqual(hpi.decompress_lz77(bytes([0b010]) + b'a' + struct.pack('<H', 0), 1), b'a')

    def _refused(self, src, want):
        with self.assertRaises(hpi.HpiError):
            hpi.decompress_lz77(src, want)

    def test_no_tag_byte(self):
        self._refused(b'', 10)

    def test_a_truncated_literal(self):
        self._refused(b'\x00', 10)

    def test_a_truncated_offset(self):
        self._refused(b'\x01\x00', 10)

    def test_running_over_the_declared_output_size(self):
        self._refused(b'\x00' + b'ab' * 8, 4)


class ZlibTest(unittest.TestCase):
    """decompressZLib, including its second definition of a finished chunk:
    HPIZ Archiver wrote thousands of trailer-less streams into the V Maps pack
    and the engine reads them, so a reader that used zlib.decompress would
    refuse archives RWE opens."""

    PAYLOAD = b'no trailer here, ' * 50

    def test_a_whole_stream_decompresses(self):
        self.assertEqual(hpi.decompress_zlib(zlib.compress(self.PAYLOAD, 9), 1000), self.PAYLOAD)

    def test_a_stream_that_fits_the_declared_size_exactly(self):
        whole = zlib.compress(self.PAYLOAD, 9)
        self.assertEqual(hpi.decompress_zlib(whole, len(self.PAYLOAD)), self.PAYLOAD)

    def test_a_trailer_less_stream_decompresses(self):
        whole = zlib.compress(self.PAYLOAD, 9)[:-4]
        self.assertEqual(hpi.decompress_zlib(whole, len(self.PAYLOAD)), self.PAYLOAD)

    def test_a_trailer_less_stream_asked_for_too_few_bytes_is_refused(self):
        whole = zlib.compress(self.PAYLOAD, 9)[:-4]
        with self.assertRaises(hpi.HpiError):
            hpi.decompress_zlib(whole, 100)

    def test_a_clean_end_beats_bytes_after_it(self):
        # Z_STREAM_END wins over leftover input, in the engine and here: the
        # bytes that came out are the whole stream, and the chunk's own
        # checksum already covered the compressed side.
        whole = zlib.compress(self.PAYLOAD, 9) + b'junk'
        self.assertEqual(hpi.decompress_zlib(whole, len(self.PAYLOAD)), self.PAYLOAD)

    def _refused(self, src, want):
        with self.assertRaises(hpi.HpiError):
            hpi.decompress_zlib(src, want)

    def test_rubbish(self):
        self._refused(b'not a zlib stream at all', 10)

    def test_cut_off_before_the_declared_size(self):
        self._refused(zlib.compress(self.PAYLOAD, 9), 10)

    def test_cut_off_with_input_still_to_come(self):
        # The engine's second branch needs avail_in == 0 as well as total_out ==
        # maxBytes, so this is not complete either.
        self._refused(zlib.compress(self.PAYLOAD, 9), 800)


class CipherTest(unittest.TestCase):
    """The three small functions hpi_util.cpp is built out of."""

    def test_transform_key_keeps_the_low_byte(self):
        self.assertEqual(hpi.transform_key(ENCRYPTED_KEY), 254)
        self.assertEqual(hpi.transform_key(0), 0)
        self.assertEqual(hpi.transform_key(0x1BF), 254)  # only the low byte counts

    def test_a_zero_key_changes_nothing(self):
        buf = bytes(range(64))
        self.assertEqual(hpi.decrypt(0, 17, buf), buf)

    def test_decrypt_is_its_own_inverse(self):
        buf = bytes((i * 31 + 7) & 0xFF for i in range(200))
        for seed in (0, 1, 20, 255):
            self.assertEqual(hpi.decrypt(254, seed, hpi.decrypt(254, seed, buf)), buf)

    def test_decrypt_inner_is_the_inverse_of_its_own_inverse(self):
        buf = bytes((i * 13 + 5) & 0xFF for i in range(200))
        self.assertEqual(hpi.decrypt_inner(encrypt_inner(buf)), buf)

    def test_the_checksum_wraps(self):
        self.assertEqual(hpi.compute_checksum(b''), 0)
        self.assertEqual(hpi.compute_checksum(bytes([255, 255])), 510)
        self.assertEqual(hpi.compute_checksum(b'\xff' * 5), (5 * 255) & 0xFFFFFFFF)


class RealArchiveTest(unittest.TestCase):
    """A real archive, when one is pointed at. Skipped loudly when it is not:
    game data is not in the repository, and a pass that quietly read nothing
    would be worse than one that says it read nothing.

    `RWE_SCENARIO_DATA` is the directory the other tools in here are pointed at
    a data set with. A handful of entries, not the whole archive: this is a
    smoke test that the reader agrees with shipped data, and the full sweep is
    a thing a person runs once.
    """

    DATA = os.environ.get('RWE_SCENARIO_DATA')

    def archives(self):
        if not self.DATA:
            self.skipTest('set RWE_SCENARIO_DATA to a directory of .hpi files to run this')
        if not os.path.isdir(self.DATA):
            self.skipTest('RWE_SCENARIO_DATA is not a directory: %s' % self.DATA)
        found = sorted(f for f in os.listdir(self.DATA) if f.lower().endswith('.hpi'))
        if not found:
            self.skipTest('no .hpi files in RWE_SCENARIO_DATA: %s' % self.DATA)
        return [os.path.join(self.DATA, f) for f in found]

    def test_every_archive_opens_and_its_entries_read(self):
        for path in self.archives():
            with self.subTest(archive=os.path.basename(path)):
                a = hpi.HpiArchive(path)
                self.assertGreater(len(a.entries), 0)
                for entry in self.sample(a):
                    data = a.read(entry.path)
                    self.assertEqual(len(data), entry.size, entry.path)
                    self.assertTrue(data, entry.path)

    def sample(self, archive):
        """Three entries per archive, chosen for what they cover rather than by
        position: the smallest, the smallest that needs more than one chunk, and
        the largest that stays under a megabyte.

        Deliberately not "the biggest file in the archive". A data set's largest
        entries are its films, and pure-Python LZ77 over a hundred megabytes of
        Smacker is minutes of work for a smoke test that is meant to say the
        reader agrees with shipped data. What is wanted here is coverage of the
        shapes, not volume.
        """
        by_size = sorted(archive.entries.values(), key=lambda e: (e.size, e.path))
        small = by_size[0]
        multi = next((e for e in by_size if e.size > 65536), small)
        under = [e for e in by_size if e.size <= 1024 * 1024]
        return [small, multi, under[-1] if under else small]

    def test_a_unit_definition_reads_as_tdf(self):
        # The reason the tool exists: `units/armcarry.fbi` as text. Only a data
        # set with unit definitions in totala1 has one.
        path = next((p for p in self.archives() if os.path.basename(p).lower() == 'totala1.hpi'), None)
        if path is None:
            self.skipTest('no totala1.hpi in RWE_SCENARIO_DATA')
        a = hpi.HpiArchive(path)
        fbis = sorted((e for e in a.entries.values() if e.path.lower().endswith('.fbi')),
                      key=lambda e: e.path)
        self.assertGreater(len(fbis), 0, 'totala1.hpi has no .fbi entries')
        text = a.read(fbis[0].path).decode('latin-1')
        self.assertIn('[UNITINFO]', text)
        self.assertIn('UnitName=', text)


if __name__ == '__main__':
    unittest.main()
