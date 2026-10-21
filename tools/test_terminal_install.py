"""Exercise the Linux shell installer against local release fixtures."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


class LinuxInstall(unittest.TestCase):
    def test_install_update_and_checksum_failure(self):
        bash = os.environ.get("RWE_TEST_BASH", "sh")
        script = Path(__file__).resolve().parents[1] / "install.sh"
        with tempfile.TemporaryDirectory(prefix="rwe 'install' ") as tmp:
            root = Path(tmp)
            mock = root / "mocks"
            mock.mkdir()
            image = b'#!/bin/sh\nprintf "%s\\n" "$@"\nexit "${SETUP_EXIT:-0}"\n'
            (root / "image").write_bytes(image)
            digest = hashlib.sha256(image).hexdigest()
            release = {"assets": [{"name": "RWE-x86_64.AppImage", "digest": "sha256:" + digest,
                                   "browser_download_url": "https://github.com/CubeB/RWE-B4/releases/download/nightly/RWE-x86_64.AppImage"}]}
            (root / "release.json").write_text(json.dumps(release, indent=2))
            # Simulate Linux when testing under Git Bash on Windows too.
            (mock / "uname").write_text('#!/bin/sh\ncase "$1" in -s) echo Linux;; -m) echo x86_64;; esac\n')
            (mock / "curl").write_text('''#!/bin/sh
while [ "$#" -gt 0 ]; do
  case "$1" in
    --output) output=$2; shift 2;;
    https://api.github.com/*) source="$FIXTURES/release.json"; shift;;
    https://github.com/*) source="$FIXTURES/image"; shift;;
    *) shift;;
  esac
done
cp "$source" "$output"
''')
            for path in mock.iterdir():
                path.chmod(0o755)
            # Resolve Windows paths inside Bash; native Linux needs no conversion.
            harness = '''set -eu
fixture=$1
installer=$2
if command -v cygpath >/dev/null 2>&1; then
  fixture=$(cygpath -u "$fixture")
  installer=$(cygpath -u "$installer")
fi
export FIXTURES="$fixture"
export PATH="$fixture/mocks:$PATH"
export RWE_INSTALL_DIR="$fixture/program with spaces"
export RWE_BIN_DIR="$fixture/bin with spaces"
sh "$installer"
sh "$installer"
"$RWE_BIN_DIR/rwe-setup" --from '/game data'
'''
            result = subprocess.run([bash, "-c", harness, "test", root.as_posix(), script.as_posix()],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.splitlines()[-3:], ["setup", "--from", "/game data"])
            self.assertEqual(result.stdout.splitlines().count("setup"), 3)
            explicit = harness.replace('sh "$installer"\nsh "$installer"',
                                       'RWE_TA_DIR="/my game data" sh "$installer"')
            result = subprocess.run([bash, "-c", explicit, "test", root.as_posix(), script.as_posix()],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("setup\n--from\n/my game data\n", result.stdout)
            skipped = harness.replace('sh "$installer"\nsh "$installer"',
                                      'RWE_SKIP_SETUP=1 sh "$installer"')
            result = subprocess.run([bash, "-c", skipped, "test", root.as_posix(), script.as_posix()],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.splitlines().count("setup"), 1)
            setup_failed = subprocess.run([bash, "-c", harness, "test", root.as_posix(), script.as_posix()],
                                         env={**os.environ, "SETUP_EXIT": "7"},
                                         capture_output=True, text=True, timeout=30)
            self.assertNotEqual(setup_failed.returncode, 0)
            self.assertIn("RWE remains installed", setup_failed.stderr)
            launcher = root / "program with spaces/rwe"
            before = launcher.read_bytes()
            (root / "image").write_bytes(b"corrupt download")
            failed = subprocess.run([bash, "-c", harness, "test", root.as_posix(), script.as_posix()],
                                    capture_output=True, text=True, timeout=30)
            self.assertNotEqual(failed.returncode, 0)
            self.assertIn("checksum", failed.stderr)
            self.assertEqual(launcher.read_bytes(), before)
            self.assertEqual(list((root / "program with spaces").glob(".install-*")), [])


if __name__ == "__main__":
    unittest.main()
