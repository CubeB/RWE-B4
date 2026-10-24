"""Exercise the real launcher's native JSON bridge, including clean EOF."""
import json
import os
from pathlib import Path
import subprocess
import unittest


class LauncherBridge(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bridge = Path(os.environ["RWE_TEST_BRIDGE"]).resolve()

    def run_bridge(self, commands):
        return subprocess.run([str(self.bridge)], input=commands, text=True,
                              capture_output=True, timeout=10)

    def test_empty_stream_exits_cleanly(self):
        result = self.run_bridge("")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "")

    def test_multiple_requests_and_blank_lines_then_eof(self):
        result = self.run_bridge('\n{"command":"clear-data-paths"}\n\n{"command":"map-list"}\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        responses = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(responses[0], {"result": "ok"})
        self.assertEqual(responses[1]["maps"], [])

    def test_invalid_json_reports_error_instead_of_aborting(self):
        result = self.run_bridge("{invalid}\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("Invalid launcher request", result.stderr)


if __name__ == "__main__":
    unittest.main()
