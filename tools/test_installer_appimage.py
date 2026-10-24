"""Exercise AppImage setup routing with fake desktop/importer processes."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class SetupRouting(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="rwe installer ")
        self.root = Path(self.temporary.name)
        self.bin = self.root / "usr/bin"
        self.bin.mkdir(parents=True)
        self.repository = Path(__file__).resolve().parents[1]
        for name, source in (("AppRun", "AppRun"), ("usr/bin/rwe-setup-gui", "rwe-setup-gui")):
            target = self.root / name
            shutil.copyfile(self.repository / "cmake" / source, target)
            target.chmod(0o755)
        self.write_executable("rwe_setup", '''#!/bin/sh
if [ "${1:-}" = --check-existing ]; then
    exit "${TEST_EXISTING_EXIT:-1}"
fi
printf '%s\\n' "$@" > "$TEST_IMPORT_ARGS"
echo 'import output'
exit "${TEST_IMPORT_EXIT:-0}"
''')
        self.write_executable("rwe", '''#!/bin/sh
printf 'played\\n' > "$TEST_PLAYED"
''')
        self.write_executable("zenity", '''#!/bin/sh
printf '%s\\n' "$1" >> "$TEST_DIALOGS"
case "$1" in
  --file-selection) [ "${TEST_CANCEL:-0}" = 1 ] && exit 1; echo "$TEST_FOLDER" ;;
  --question) [ "${TEST_PLAY:-0}" = 1 ] || case "$*" in *'Ready to play'*) exit 1;; esac ;;
  --progress) cat >/dev/null ;;
esac
''')
        self.env = dict(os.environ)
        self.env.update({
            "PATH": str(self.bin) + os.pathsep + os.environ["PATH"],
            "DISPLAY": ":fake-test",
            "TEST_FOLDER": "/games/Total Annihilation & expansions",
            "TEST_IMPORT_ARGS": str(self.root / "import.args"),
            "TEST_DIALOGS": str(self.root / "dialogs"),
            "TEST_PLAYED": str(self.root / "played"),
        })
        self.bash = os.environ.get("RWE_TEST_BASH", "sh")

    def tearDown(self):
        self.temporary.cleanup()

    def write_executable(self, name, text):
        path = self.bin / name
        path.write_bytes(text.encode())
        path.chmod(0o755)

    def run_setup(self, *args):
        return subprocess.run([self.bash, str(self.root / "AppRun"), *args],
                              env=self.env, capture_output=True, text=True, timeout=15)

    def test_graphical_import_quotes_folder_and_does_not_launch_unasked(self):
        result = self.run_setup("setup")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "import.args").read_text().splitlines(),
                         ["--from", self.env["TEST_FOLDER"]])
        self.assertIn("--progress", (self.root / "dialogs").read_text())
        self.assertFalse((self.root / "played").exists())

    def test_play_is_explicit(self):
        self.env["TEST_PLAY"] = "1"
        self.assertEqual(self.run_setup("setup").returncode, 0)
        self.assertTrue((self.root / "played").exists())

    def test_existing_data_skips_picker_and_import(self):
        self.env["TEST_EXISTING_EXIT"] = "0"
        self.assertEqual(self.run_setup("setup").returncode, 0)
        self.assertFalse((self.root / "dialogs").exists())
        self.assertFalse((self.root / "import.args").exists())
        self.assertFalse((self.root / "played").exists())

    def test_failure_shows_details_and_preserves_failure(self):
        self.env["TEST_IMPORT_EXIT"] = "7"
        self.assertEqual(self.run_setup("setup").returncode, 1)
        self.assertIn("--text-info", (self.root / "dialogs").read_text())
        self.assertFalse((self.root / "played").exists())

    def test_cancel_does_not_import(self):
        self.env["TEST_CANCEL"] = "1"
        self.assertEqual(self.run_setup("setup").returncode, 0)
        self.assertFalse((self.root / "import.args").exists())

    def test_explicit_options_stay_on_cli(self):
        result = self.run_setup("setup", "--from", "/a path", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "import.args").read_text().splitlines(),
                         ["--from", "/a path", "--dry-run"])
        self.assertFalse((self.root / "dialogs").exists())

    def test_headless_setup_stays_on_cli(self):
        self.env.pop("DISPLAY", None)
        self.env.pop("WAYLAND_DISPLAY", None)
        self.assertEqual(self.run_setup("setup").returncode, 0)
        self.assertFalse((self.root / "dialogs").exists())


if __name__ == "__main__":
    unittest.main()
