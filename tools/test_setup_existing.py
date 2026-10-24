"""Check existing-data handling using a built importer, without user data."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ExistingData(unittest.TestCase):
    def test_existing_data_and_explicit_import(self):
        importer = Path(os.environ["RWE_TEST_IMPORTER"]).resolve()
        with tempfile.TemporaryDirectory(prefix="rwe existing data ") as temporary:
            root = Path(temporary)
            target = root / "imported data"
            target.mkdir()

            def run(*args):
                return subprocess.run([str(importer), "--to", str(target), *args],
                                      capture_output=True, text=True, timeout=15)

            # An empty or incomplete import must not be accepted.
            self.assertEqual(run("--check-existing").returncode, 1)
            (target / "TOTALA1.HPI").write_bytes(b"first archive")
            self.assertEqual(run("--check-existing").returncode, 1)
            (target / "totala2.hpi").write_bytes(b"second archive")
            checked = run("--check-existing")
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
            # No source installation is needed, and matching is case insensitive.
            existing = run()
            self.assertEqual(existing.returncode, 0, existing.stdout + existing.stderr)
            self.assertIn("No import needed", existing.stdout)
            self.assertNotIn("Looking for", existing.stdout)
            # Also exercise the default data path used by installer invocations.
            env = dict(os.environ, APPDATA=str(root / "user"),
                       XDG_DATA_HOME=str(root / "user"), HOME=str(root / "home"))
            default_data = root / "user" / ("RWE" if os.name == "nt" else "rwe") / "Data"
            default_data.parent.mkdir(parents=True)
            shutil.copytree(target, default_data)
            default = subprocess.run([str(importer)], env=env, capture_output=True,
                                     text=True, timeout=15)
            self.assertEqual(default.returncode, 0, default.stdout + default.stderr)
            self.assertIn("No import needed", default.stdout)
            # Explicit imports must still validate the source and copy extras.
            self.assertNotEqual(run("--from", str(root / "missing")).returncode, 0)
            source = root / "TA installation"
            source.mkdir()
            for name in ("TOTALA1.HPI", "totala2.hpi"):
                (source / name).write_bytes((target / name).read_bytes())
            (source / "extra.ufo").write_bytes(b"extra content")
            imported = run("--from", str(source))
            self.assertEqual(imported.returncode, 0, imported.stdout + imported.stderr)
            self.assertIn("2 already in place", imported.stdout)
            self.assertEqual((target / "extra.ufo").read_bytes(), b"extra content")


if __name__ == "__main__":
    unittest.main()
