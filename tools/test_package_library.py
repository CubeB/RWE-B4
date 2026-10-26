"""Native package-library command tests; no proprietary fixture assets required."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


def native_archive(files):
    """Minimal stored HPI directory tree, using the existing documented layout."""
    tree = {}
    for name, data in files.items():
        parts = name.split('/')
        node = tree
        for part in parts[:-1]:
            node = node.setdefault(part, {})
        node[parts[-1]] = data
    output = bytearray(20)
    payloads = []

    def directory(node):
        offset = len(output)
        output.extend(bytes(8))
        entries = len(output)
        output.extend(bytes(9 * len(node)))
        struct.pack_into('<II', output, offset, len(node), entries)
        for index, (name, value) in enumerate(node.items()):
            name_offset = len(output)
            output.extend(name.encode('ascii') + b'\0')
            if isinstance(value, dict):
                data_offset = directory(value)
                is_directory = 1
            else:
                data_offset = len(output)
                output.extend(bytes(9))
                payloads.append((data_offset, value))
                is_directory = 0
            struct.pack_into('<IIB', output, entries + index * 9,
                             name_offset, data_offset, is_directory)
        return offset

    start = directory(tree)
    directory_size = len(output)
    for offset, data in payloads:
        struct.pack_into('<IIB', output, offset, len(output), len(data), 0)
        output.extend(data)
    struct.pack_into('<IIIII', output, 0, 0x49504148, 0x00010000,
                     directory_size, 0, start)
    return bytes(output)


class PackageCommands(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.importer = Path(os.environ['RWE_TEST_IMPORTER']).resolve()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='rwe package cli ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.env = dict(os.environ, APPDATA=str(self.root / 'user'),
                        XDG_DATA_HOME=str(self.root / 'user'), HOME=str(self.root / 'home'))

    def run_command(self, *args):
        result = subprocess.run([str(self.importer), *map(str, args)],
                                cwd=self.root, env=self.env, capture_output=True,
                                text=True, encoding='utf-8', errors='replace', timeout=30)
        try:
            data = json.loads(result.stdout)
        except json.JSONDecodeError:
            self.fail('Expected one JSON result: ' + repr((result.returncode, result.stdout, result.stderr)))
        return result, data

    def test_missing_registry_does_not_create_a_library_or_import_ta(self):
        library = self.root / 'new library'
        result, data = self.run_command('--list-packages', '--library-root', library, '--json')
        self.assertEqual(result.returncode, 0, data)
        # PACKAGE-006: an absent registry is the generation-zero snapshot.
        self.assertEqual(data, {'schemaVersion': 1, 'generation': 0, 'packages': []})
        self.assertFalse(library.exists())
        self.assertFalse((self.root / 'user').exists())

    def test_corrupt_registry_is_preserved(self):
        library = self.root / 'library'
        library.mkdir()
        registry = library / 'registry.json'
        original = b'{"schemaVersion":999,"packages":[]}'
        registry.write_bytes(original)
        result, data = self.run_command('--list-packages', '--library-root', library)
        self.assertEqual(result.returncode, 1)
        self.assertIn('error', data)
        self.assertEqual(registry.read_bytes(), original)

    def test_native_archive_reports_paired_maps_without_claiming_terrain_validation(self):
        source = self.root / 'Two maps.ufo'
        original = native_archive({'Maps/One.OTA': b'metadata', 'Maps/one.TNT': b'terrain',
                                   'Maps/Other.ota': b'missing terrain',
                                   'Features/map.tdf': b'feature definitions'})
        source.write_bytes(original)
        result, data = self.run_command('--inspect-package', source, '--json')
        self.assertEqual(result.returncode, 0, data)
        self.assertTrue(data['complete'])
        candidate = data['candidates'][0]
        self.assertEqual(candidate['name'], 'Two maps')
        self.assertEqual(candidate['kind'], 'maps')
        self.assertEqual(candidate['compatibility'], 'unverified')
        self.assertEqual(len(candidate['maps']), 2)
        self.assertEqual(sorted(m['paired'] for m in candidate['maps']), [False, True])
        self.assertTrue(all(m['validation'] == 'not-run' for m in candidate['maps']))
        self.assertEqual(source.read_bytes(), original)

    def test_nested_unicode_folder_and_editable_metadata(self):
        source = self.root / 'M\u00e4p packages'
        actual = source / 'nested data'
        (actual / 'units').mkdir(parents=True)
        (actual / 'units' / 'EXAMPLE.fbi').write_text('[UNITINFO]{}', encoding='ascii')
        metadata = actual / 'rwe_mod.json'
        original = json.dumps({'shortname': 'example', 'name': 'Example \u2014 mod',
                               'version': '2.0', 'author': ''}).encode('utf-8')
        metadata.write_bytes(original)
        result, data = self.run_command('--inspect-package', source, '--json')
        self.assertEqual(result.returncode, 0, data)
        candidate = data['candidates'][0]
        self.assertEqual(candidate['name'], 'Example \u2014 mod')
        self.assertEqual(candidate['version'], '2.0')
        self.assertTrue(data['complete'], data)
        self.assertEqual(metadata.read_bytes(), original)

    def test_reserved_package_operations_never_fall_through_to_ta_import(self):
        for args in [('--install-package', 'not-yet-supported'),
                     ('--uninstall-package', 'not-yet-supported'),
                     ('--verify-package', 'not-yet-supported'),
                     ('--library-root', 'library-without-command'),
                     ('--inspect-package', '--json'),
                     ('--inspect-package', 'missing', '--list-packages')]:
            with self.subTest(args=args):
                result, data = self.run_command(*args)
                self.assertEqual(result.returncode, 1)
                self.assertIn('error', data)
                self.assertFalse((self.root / 'user').exists())
                self.assertFalse((self.root / 'library-without-command').exists())

    def test_rar_source_explicitly_requires_the_extraction_task(self):
        source = self.root / 'Escalation.RAR'
        source.write_bytes(b'not extracted yet')
        result, data = self.run_command('--inspect-package', source)
        self.assertEqual(result.returncode, 0, data)
        self.assertFalse(data['complete'])
        self.assertEqual(data['containers'][0]['state'], 'extraction-required')
        self.assertEqual(data['candidates'], [])
        self.assertEqual(source.read_bytes(), b'not extracted yet')

    @unittest.skipUnless(os.environ.get('RWE_TEST_TAESC'), 'set RWE_TEST_TAESC for optional real-data inspection')
    def test_real_taesc_is_recognized_without_source_writes(self):
        source = Path(os.environ['RWE_TEST_TAESC']).resolve()
        def snapshot():
            return sorted((str(p.relative_to(source)), p.stat().st_size, p.stat().st_mtime_ns)
                          for p in source.rglob('*') if p.is_file())
        before = snapshot()
        result, data = self.run_command('--inspect-package', source, '--json')
        self.assertEqual(result.returncode, 0, data)
        candidates = [c for c in data['candidates'] if c['recognition'] == 'escalation-resource-layout']
        self.assertEqual(len(candidates), 1, data)
        candidate = candidates[0]
        self.assertEqual(candidate['name'], 'Total Annihilation: Escalation')
        self.assertEqual(candidate['version'], '10.2.0')
        self.assertEqual(len(candidate['archives']), 8)
        self.assertEqual(candidate['resourceDirectories']['ai'], 'ae')
        self.assertEqual(candidate['resourceDirectories']['units'], 'unitse')
        self.assertEqual(len(candidate['resourceDirectories']), 7)
        self.assertGreater(len(candidate['maps']), 0)
        self.assertEqual(candidate['compatibility'], 'unverified')
        self.assertEqual(candidate['warnings'], [])
        self.assertEqual(snapshot(), before)


if __name__ == '__main__':
    unittest.main()
