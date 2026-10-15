"""Checks that a cross-installation run really gives each peer its own data."""
import importlib.util
import contextlib
import io
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("net_test", Path(__file__).with_name("net-test.py"))
net_test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(net_test)


class PeerDataTests(unittest.TestCase):
    def test_independent_windows_installations_and_rejoin(self):
        args = net_test.parse_args([
            "--data-path", "shared", "--ai", "--ai-engine", "classic", "--seed", "19",
            "--peer-data-path", "0:D:/GOG/Total Annihilation",
            "--peer-data-path", "1:D:/Steam/Total Annihilation",
            "--peer-data-path", "1:D:/Steam/patch",
        ])
        with tempfile.TemporaryDirectory() as directory:
            harness = net_test.Harness(args, Path("rwe"), Path(directory), [10000, 10001], -1, "")
            for peer, expected in [(0, ["D:/GOG/Total Annihilation"]),
                                   (1, ["D:/Steam/Total Annihilation", "D:/Steam/patch"])]:
                for recording in (True, False):  # initial launch and returning peer
                    command = harness.peer_args(peer, Path(directory) / "game.log", recording)
                    paths = [command[i + 1] for i, arg in enumerate(command) if arg == "--data-path"]
                    self.assertEqual(paths, expected)
                    self.assertEqual(command[command.index("--seed") + 1], "19")
                    self.assertEqual(command[command.index("--ai-engine") + 1], "classic")

    def test_unconfigured_peer_keeps_shared_paths(self):
        args = net_test.parse_args(["--data-path", "shared", "--peer-data-path", "0:private"])
        with tempfile.TemporaryDirectory() as directory:
            harness = net_test.Harness(args, Path("rwe"), Path(directory), [10000, 10001], -1, "")
            command = harness.peer_args(1, Path(directory) / "game.log", False)
            self.assertEqual(command[command.index("--data-path") + 1], "shared")

    def test_invalid_peer_paths_fail_before_launch(self):
        for value in ("2:D:/TA", "-1:D:/TA", "0:", "D:/TA"):
            with self.subTest(value=value), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                net_test.parse_args(["--peer-data-path=" + value])

    def test_stalled_identical_peers_do_not_pass_required_coverage(self):
        from unittest.mock import Mock
        args = net_test.parse_args(["--min-ticks", "10"])
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            harness = net_test.Harness(args, Path("rwe"), out, [10000, 10001], -1, "")
            for index in range(2):
                hashes = out / f"peer{index}.hashes"
                hashes.write_text("1 1234\n2 5678\n")
                proc = Mock()
                proc.poll.return_value = 0
                harness.peers.append(net_test.Peer(index, proc, out / f"peer{index}.log", hashes))
            with contextlib.redirect_stdout(io.StringIO()):
                agreed, compared = harness.compare(False)
                self.assertTrue(agreed)
                self.assertTrue(compared)
                self.assertEqual(harness.finish(agreed, compared, False), 1)
                args.min_ticks = 2
                self.assertEqual(harness.finish(agreed, compared, False), 0)


if __name__ == "__main__":
    unittest.main()
