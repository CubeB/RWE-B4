#!/usr/bin/env python3
"""Tests for ``q.py``, the agent-facing event-log query tool (design §9).

Run with ``python3 tools/playtest/tests_q.py``. Stdlib only; all fixtures are
synthetic event logs under ``testdata/`` with the pinned field names.
"""

import contextlib
import io
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import q  # noqa: E402

TESTDATA = HERE / "testdata"


def run_q(argv):
    """Call ``q.main`` and return ``(rc, stdout, stderr)``."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = q.main(argv)
    return rc, out.getvalue(), err.getvalue()


class QTestCase(unittest.TestCase):
    def make_root(self, runs):
        """``runs`` maps ``<scenario>/seed<N>-<arm>`` to a fixture name."""
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        for run_id, fixture in runs.items():
            run_dir = root / run_id
            run_dir.mkdir(parents=True)
            shutil.copyfile(TESTDATA / fixture, run_dir / "event-log.jsonl")
        return root


class QDiscoveryTests(QTestCase):
    def test_missing_event_log_reports_error(self):
        empty = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, empty, ignore_errors=True)
        rc, out, err = run_q([str(empty)])
        self.assertEqual(rc, 1)
        self.assertEqual(out, "")
        self.assertIn("event-log.jsonl", err)

    def test_run_root_scans_every_run(self):
        root = self.make_root({
            "s/seed1-control": "ev-decisions.jsonl",
            "s/seed2-control": "ev-status.jsonl",
        })
        rc, out, _ = run_q([str(root), "--limit", "100"])
        self.assertEqual(rc, 0)
        self.assertIn("s/seed1-control: ", out)
        self.assertIn("s/seed2-control: ", out)

    def test_every_line_is_prefixed_with_the_run_id(self):
        root = self.make_root({"s/seed1-control": "ev-decisions.jsonl"})
        _rc, out, _ = run_q([str(root), "--limit", "100"])
        lines = out.splitlines()
        self.assertTrue(lines)
        for line in lines:
            self.assertTrue(line.startswith("s/seed1-control: "), line)

    def test_direct_run_dir_is_accepted(self):
        root = self.make_root({"s/seed1-control": "ev-decisions.jsonl"})
        rc, out, _ = run_q([str(root / "s" / "seed1-control")])
        self.assertEqual(rc, 0)
        self.assertIn("s/seed1-control: ", out)

    def test_no_matching_events_is_clean(self):
        root = self.make_root({"s/seed1-control": "ev-decisions.jsonl"})
        rc, out, _ = run_q([str(root), "--ev", "no_such_event"])
        self.assertEqual(rc, 0)
        self.assertEqual(out, "")


class QFilterTests(QTestCase):
    def setUp(self):
        self.root = self.make_root({"s/seed1-control": "ev-status.jsonl"})
        self._rc, self.out, _ = run_q([str(self.root), "--limit", "100"])

    def test_ev_filter_single(self):
        rc, out, _ = run_q([str(self.root), "--ev", "ai_transition"])
        self.assertEqual(rc, 0)
        self.assertEqual(len(out.splitlines()), 1)
        self.assertIn("\"ev\":\"ai_transition\"", out)

    def test_ev_filter_comma_list(self):
        rc, out, _ = run_q([str(self.root), "--ev", "ai_status,ai_transition"])
        self.assertEqual(rc, 0)
        self.assertEqual(len(out.splitlines()), 3)

    def test_player_filter(self):
        _rc, out, _ = run_q([str(self.root), "--player", "1"])
        self.assertEqual(len(out.splitlines()), 1)
        self.assertIn("\"player\":1", out)

    def test_from_and_to_filter(self):
        _rc, out, _ = run_q([str(self.root), "--from", "50", "--to", "120"])
        lines = out.splitlines()
        self.assertEqual(len(lines), 1)
        self.assertIn("\"secs\":60.0", lines[0])

    def test_grep_on_detail(self):
        _rc, out, _ = run_q([str(self.root), "--grep", "phase advanced"])
        self.assertEqual(len(out.splitlines()), 1)
        self.assertIn("ai_transition", out)

    def test_why_filter(self):
        root = self.make_root({"s/seed1-control": "ev-decisions.jsonl"})
        _rc, out, _ = run_q([str(root), "--why", "no_site"])
        self.assertEqual(len(out.splitlines()), 1)
        self.assertIn("build_refusal", out)

    def test_filters_combine(self):
        root = self.make_root({"s/seed1-control": "ev-decisions.jsonl"})
        _rc, out, _ = run_q([str(root), "--ev", "build_refusal", "--player", "1"])
        self.assertEqual(len(out.splitlines()), 1)
        self.assertIn("unaffordable", out)

    def test_limit_caps_printed_lines(self):
        _rc, out, _ = run_q([str(self.root), "--limit", "1"])
        self.assertEqual(len(out.splitlines()), 1)


class QCountTests(QTestCase):
    def setUp(self):
        self.root = self.make_root({
            "s/seed1-control": "ev-decisions.jsonl",
            "s/seed2-control": "ev-status.jsonl",
        })

    def test_count_defaults_to_ev(self):
        _rc, out, _ = run_q([str(self.root), "--count"])
        lines = out.splitlines()
        self.assertEqual(lines[0], "2\tai_status")
        self.assertIn("2\tbuild_refusal", lines)
        self.assertIn("1\tai_transition", lines)

    def test_count_by_field(self):
        _rc, out, _ = run_q([str(self.root), "--count", "why"])
        mapping = {key: count for count, key in
                   (line.split("\t", 1) for line in out.splitlines())}
        self.assertEqual(mapping.get("no_site"), "1")
        self.assertEqual(mapping.get("attack"), "1")

    def test_count_respects_filters(self):
        _rc, out, _ = run_q([str(self.root), "--ev", "build_refusal", "--count", "player"])
        self.assertEqual(sorted(out.splitlines()), ["1\t0", "1\t1"])

    def test_count_limit_caps_groups(self):
        _rc, out, _ = run_q([str(self.root), "--count", "--limit", "2"])
        self.assertEqual(len(out.splitlines()), 2)


class QMalformedTests(QTestCase):
    def test_malformed_lines_are_skipped(self):
        root = self.make_root({"s/seed1-control": "ev-malformed.jsonl"})
        rc, out, _ = run_q([str(root), "--limit", "100"])
        self.assertEqual(rc, 0)
        lines = out.splitlines()
        self.assertEqual(len(lines), 2)
        self.assertTrue(any("path_stats" in line for line in lines))
        self.assertTrue(any("ai_perf" in line for line in lines))

    def test_malformed_count_does_not_crash(self):
        root = self.make_root({"s/seed1-control": "ev-malformed.jsonl"})
        rc, out, _ = run_q([str(root), "--count"])
        self.assertEqual(rc, 0)
        self.assertIn("1\tai_perf", out)
        self.assertIn("1\tpath_stats", out)


class QUsageTests(unittest.TestCase):
    def test_help_exits_zero(self):
        with contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(SystemExit) as caught:
                q.main(["--help"])
        self.assertEqual(caught.exception.code, 0)

    def test_docstring_documents_the_vocabulary(self):
        doc = q.__doc__
        for name in ("path_stats", "ai_status", "ai_transition", "ai_perf",
                     "build_refusal", "transport_refusal", "scout_assigned"):
            self.assertIn(name, doc)


if __name__ == "__main__":
    unittest.main(verbosity=2)
