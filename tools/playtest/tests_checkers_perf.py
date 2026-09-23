#!/usr/bin/env python3
"""Tests for the pathfind, aiperf and desync checkers.

Run with ``python3 tools/playtest/tests_checkers_perf.py``. Stdlib only; all
fixtures are synthetic and live under ``testdata/``. The aiperf F3 baseline
rule is exercised only in its guarded-absent path -- never against the real
``baselines`` module, which a sibling owns.
"""

import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

import run as runner  # noqa: E402
from checkers import aiperf, desync, pathfind  # noqa: E402
from checkers.economy import Finding  # noqa: E402

TESTDATA = HERE / "testdata"


def rules(findings, prefix):
    return [f for f in findings if f.summary.startswith(prefix)]


def tmp_dir(test) -> Path:
    tmp = Path(tempfile.mkdtemp())
    test.addCleanup(shutil.rmtree, tmp, ignore_errors=True)
    return tmp


def copy_fixture(name, dest: Path, dest_name=None) -> Path:
    dest.mkdir(parents=True, exist_ok=True)
    target = dest / (dest_name or name)
    shutil.copyfile(TESTDATA / name, target)
    return target


class PathfindRuleTests(unittest.TestCase):
    def _with(self, fixture, name="path-profile.log"):
        return copy_fixture(fixture, tmp_dir(self), name).parent

    def test_h1_hit_is_suspicious(self):
        findings = pathfind.check(self._with("path-h1.log"))
        h1 = rules(findings, "H1:")
        self.assertEqual(len(h1), 1)
        self.assertEqual(h1[0].severity, "suspicious")
        self.assertGreaterEqual(h1[0].evidence["windows"], 6)
        self.assertTrue(all(isinstance(v, int) for v in h1[0].evidence["exhausted"]))
        self.assertTrue(all(isinstance(r, int) for r in h1[0].evidence["rows"]))

    def test_h1_clean(self):
        self.assertEqual(rules(pathfind.check(self._with("path-clean.log")), "H1:"), [])

    def test_h2_hit_names_the_counter(self):
        findings = pathfind.check(self._with("path-h2.log"))
        h2 = rules(findings, "H2:")
        self.assertEqual(len(h2), 1)
        self.assertEqual(h2[0].evidence["counter"], "bugwalk")
        self.assertGreaterEqual(h2[0].evidence["windows"], 10)
        values = h2[0].evidence["values"]
        self.assertEqual(values, sorted(values))
        self.assertGreater(values[0], 0)

    def test_h2_clean(self):
        self.assertEqual(rules(pathfind.check(self._with("path-clean.log")), "H2:"), [])

    def test_h3_hit_reports_peak(self):
        findings = pathfind.check(self._with("path-h3.log"))
        h3 = rules(findings, "H3:")
        self.assertEqual(len(h3), 1)
        self.assertGreaterEqual(h3[0].evidence["windows"], 6)
        self.assertGreater(h3[0].evidence["max_queued_per_tick"], 0.5)

    def test_h3_clean(self):
        self.assertEqual(rules(pathfind.check(self._with("path-clean.log")), "H3:"), [])

    def test_extracts_from_game_log_when_no_profile_log(self):
        findings = pathfind.check(self._with("path-h1.log", name="game.log"))
        self.assertEqual(len(rules(findings, "H1:")), 1)
        self.assertTrue(findings[0].evidence["file"].endswith("game.log"))

    def test_missing_profile_and_log_returns_empty(self):
        self.assertEqual(pathfind.check(tmp_dir(self)), [])

    def test_malformed_lines_do_not_crash(self):
        findings = pathfind.check(self._with("path-malformed.log"))
        self.assertIsInstance(findings, list)
        for finding in findings:
            self.assertIsInstance(finding, Finding)


class AiPerfRuleTests(unittest.TestCase):
    def _with(self, fixture, name="ai-profile.log"):
        return copy_fixture(fixture, tmp_dir(self), name).parent

    def test_f1_hit_is_suspicious(self):
        findings = aiperf.check(self._with("perf-f1.log"))
        f1 = rules(findings, "F1:")
        self.assertEqual(len(f1), 1)
        self.assertEqual(f1[0].severity, "suspicious")
        self.assertEqual(f1[0].evidence["player"], 0)
        self.assertEqual(f1[0].evidence["pass"], "economy")
        self.assertGreater(f1[0].evidence["mean_ms"], 5.0)
        self.assertEqual(f1[0].evidence["calls"], 15)

    def test_f1_clean(self):
        self.assertEqual(rules(aiperf.check(self._with("perf-clean.log")), "F1:"), [])

    def test_f2_hit_sums_spikes_over_the_game(self):
        findings = aiperf.check(self._with("perf-f2.log"))
        f2 = rules(findings, "F2:")
        self.assertEqual(len(f2), 1)
        self.assertEqual(f2[0].evidence["player"], 1)
        self.assertEqual(f2[0].evidence["pass"], "army")
        self.assertEqual(f2[0].evidence["spikes"], 55)

    def test_f2_clean(self):
        self.assertEqual(rules(aiperf.check(self._with("perf-clean.log")), "F2:"), [])

    def test_extracts_from_game_log_when_no_profile_log(self):
        findings = aiperf.check(self._with("perf-f1.log", name="game.log"))
        self.assertEqual(len(rules(findings, "F1:")), 1)
        self.assertTrue(findings[0].evidence["file"].endswith("game.log"))

    def test_missing_profile_and_log_returns_empty(self):
        self.assertEqual(aiperf.check(tmp_dir(self)), [])

    def test_malformed_lines_do_not_crash(self):
        findings = aiperf.check(self._with("perf-malformed.log"))
        self.assertIsInstance(findings, list)
        for finding in findings:
            self.assertIsInstance(finding, Finding)


class AiPerfBaselineTests(unittest.TestCase):
    def _clean_dir_with_baselines(self, baselines):
        tmp = copy_fixture("perf-clean.log", tmp_dir(self)).parent
        context = runner.RunContext("standard-arms-coast", 1, "control", {}, str(tmp), {})
        patcher = mock.patch.object(aiperf, "baselines", baselines)
        patcher.start()
        self.addCleanup(patcher.stop)
        return tmp, context

    def test_f3_skipped_when_baselines_absent(self):
        tmp, context = self._clean_dir_with_baselines(None)
        self.assertEqual(rules(aiperf.check(tmp, context), "F3:"), [])

    def test_f3_skipped_when_current_sha_is_empty(self):
        class NoSha:
            @staticmethod
            def current_sha():
                return ""

            @staticmethod
            def closest_baseline(scenario, sha):
                raise AssertionError("must not be called with an empty sha")

        tmp, context = self._clean_dir_with_baselines(NoSha())
        self.assertEqual(rules(aiperf.check(tmp, context), "F3:"), [])

    def test_f3_skipped_when_git_raises(self):
        class Broken:
            @staticmethod
            def current_sha():
                raise RuntimeError("git unavailable")

            @staticmethod
            def closest_baseline(scenario, sha):
                raise AssertionError("must not be called after current_sha raised")

        tmp, context = self._clean_dir_with_baselines(Broken())
        findings = aiperf.check(tmp, context)
        self.assertIsInstance(findings, list)
        self.assertEqual(rules(findings, "F3:"), [])


class DesyncPairTests(unittest.TestCase):
    SCENARIO = "standard-arms-coast"

    def _pair(self, csv_r1, csv_r2, log="desync-result.log"):
        root = tmp_dir(self)
        r1 = root / self.SCENARIO / "seed1-control-r1"
        r2 = root / self.SCENARIO / "seed1-control-r2"
        for directory, csv in ((r1, csv_r1), (r2, csv_r2)):
            directory.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(TESTDATA / log, directory / "game.log")
            if csv is not None:
                shutil.copyfile(TESTDATA / csv, directory / "ai-arena.csv")
        return root, r1, r2

    def _context(self, root, r1, r2, arm):
        return runner.RunContext(
            self.SCENARIO, 1, arm,
            {"control-r1": str(r1), "control-r2": str(r2)},
            str(root), {})

    def test_identical_pair_is_clean(self):
        root, r1, r2 = self._pair("desync-a.csv", "desync-a.csv")
        findings = desync.check(r1, self._context(root, r1, r2, "control-r1"))
        self.assertEqual(rules(findings, "desync:"), [])

    def test_divergent_pair_is_likely_bug(self):
        root, r1, r2 = self._pair("desync-a.csv", "desync-b.csv")
        findings = desync.check(r1, self._context(root, r1, r2, "control-r1"))
        divergence = [f for f in findings if "divergence" in f.summary]
        self.assertEqual(len(divergence), 1)
        self.assertEqual(divergence[0].severity, "likely-bug")
        offset = divergence[0].evidence["first_diff_offset"]
        self.assertIsInstance(offset, int)
        self.assertGreater(offset, 0)

    def test_r2_does_not_report_the_pair(self):
        root, r1, r2 = self._pair("desync-a.csv", "desync-b.csv")
        findings = desync.check(r2, self._context(root, r1, r2, "control-r2"))
        self.assertEqual([f for f in findings if "divergence" in f.summary], [])

    def test_missing_csv_in_one_arm_is_suspicious(self):
        root, r1, r2 = self._pair("desync-a.csv", None)
        findings = desync.check(r1, self._context(root, r1, r2, "control-r1"))
        missing = [f for f in findings if "missing" in f.summary]
        self.assertEqual(len(missing), 1)
        self.assertEqual(missing[0].severity, "suspicious")
        self.assertEqual(len(missing[0].evidence["missing"]), 1)

    def test_non_repeated_arm_is_skipped(self):
        root = tmp_dir(self)
        run = root / self.SCENARIO / "seed1-control"
        copy_fixture("desync-result.log", run, "game.log")
        copy_fixture("desync-a.csv", run, "ai-arena.csv")
        context = runner.RunContext(self.SCENARIO, 1, "control", {}, str(root), {})
        self.assertEqual(desync.check(run, context), [])


class DesyncResultTests(unittest.TestCase):
    def test_missing_result_line_is_suspicious(self):
        run = copy_fixture("desync-noresult.log", tmp_dir(self), "game.log").parent
        context = runner.RunContext("s", 1, "control", {}, str(run.parent), {})
        findings = desync.check(run, context)
        no_result = [f for f in findings if "RESULT" in f.summary]
        self.assertEqual(len(no_result), 1)
        self.assertEqual(no_result[0].severity, "suspicious")

    def test_present_result_line_is_clean(self):
        run = copy_fixture("desync-result.log", tmp_dir(self), "game.log").parent
        context = runner.RunContext("s", 1, "control", {}, str(run.parent), {})
        self.assertEqual(desync.check(run, context), [])

    def test_absent_game_log_is_skipped(self):
        run = tmp_dir(self)
        context = runner.RunContext("s", 1, "control", {}, str(run), {})
        self.assertEqual(desync.check(run, context), [])


class DesyncIndexTests(unittest.TestCase):
    SCENARIO = "standard-arms-coast"

    def _root_with_index(self, status):
        root = tmp_dir(self)
        run = root / self.SCENARIO / "seed1-control"
        copy_fixture("desync-result.log", run, "game.log")
        record = {"run_id": f"{self.SCENARIO}/seed1-control", "status": status}
        (root / "index.jsonl").write_text(json.dumps(record) + "\n")
        return root, run

    def _context(self, root):
        return runner.RunContext(self.SCENARIO, 1, "control", {}, str(root), {})

    def test_timeout_status_is_suspicious(self):
        root, run = self._root_with_index("timeout")
        findings = [f for f in desync.check(run, self._context(root))
                    if "harness status" in f.summary]
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0].severity, "suspicious")
        self.assertEqual(findings[0].evidence["status"], "timeout")

    def test_no_result_status_is_suspicious(self):
        root, run = self._root_with_index("no-result")
        findings = [f for f in desync.check(run, self._context(root))
                    if "harness status" in f.summary]
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0].severity, "suspicious")

    def test_exit_status_is_likely_bug(self):
        root, run = self._root_with_index("exit-13")
        findings = [f for f in desync.check(run, self._context(root))
                    if "harness status" in f.summary]
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0].severity, "likely-bug")

    def test_ok_status_is_clean(self):
        root, run = self._root_with_index("ok")
        self.assertEqual([f for f in desync.check(run, self._context(root))
                          if "harness status" in f.summary], [])

    def test_other_runs_record_is_ignored(self):
        root, run = self._root_with_index("timeout")
        record = {"run_id": f"{self.SCENARIO}/seed1-control-r1", "status": "timeout"}
        (root / "index.jsonl").write_text(json.dumps(record) + "\n")
        self.assertEqual([f for f in desync.check(run, self._context(root))
                          if "harness status" in f.summary], [])

    def test_absent_index_is_skipped(self):
        run = copy_fixture("desync-result.log", tmp_dir(self), "game.log").parent
        context = runner.RunContext(self.SCENARIO, 1, "control", {}, str(run), {})
        self.assertEqual(desync.check(run, context), [])

    def test_malformed_index_does_not_crash(self):
        root, run = self._root_with_index("timeout")
        (root / "index.jsonl").write_text("{not json}\n[]\n\n")
        context = runner.RunContext(self.SCENARIO, 1, "control", {}, str(root), {})
        findings = desync.check(run, context)
        self.assertIsInstance(findings, list)
        for finding in findings:
            self.assertIsInstance(finding, Finding)


class PathfindEventLogTests(unittest.TestCase):
    """When event-log.jsonl exists the path_stats events are the only source."""

    def _events(self, fixture):
        return copy_fixture(fixture, tmp_dir(self), "event-log.jsonl").parent

    def test_h1_hit_from_events(self):
        findings = pathfind.check(self._events("ev-path-h1.jsonl"))
        h1 = rules(findings, "H1:")
        self.assertEqual(len(h1), 1)
        self.assertEqual(h1[0].severity, "suspicious")
        self.assertGreaterEqual(h1[0].evidence["windows"], 6)
        self.assertEqual(h1[0].evidence["exhausted"], [3, 4, 2, 5, 3, 6])
        self.assertTrue(h1[0].evidence["file"].endswith("event-log.jsonl"))

    def test_h1_clean_from_events(self):
        self.assertEqual(rules(pathfind.check(self._events("ev-path-clean.jsonl")), "H1:"), [])

    def test_h2_hit_from_events(self):
        h2 = rules(pathfind.check(self._events("ev-path-h2.jsonl")), "H2:")
        self.assertEqual(len(h2), 1)
        self.assertEqual(h2[0].evidence["counter"], "bugwalk")
        self.assertGreaterEqual(h2[0].evidence["windows"], 10)
        values = h2[0].evidence["values"]
        self.assertEqual(values, sorted(values))
        self.assertEqual(values[0], 1)

    def test_h3_hit_from_events(self):
        h3 = rules(pathfind.check(self._events("ev-path-h3.jsonl")), "H3:")
        self.assertEqual(len(h3), 1)
        self.assertGreaterEqual(h3[0].evidence["windows"], 6)
        self.assertGreater(h3[0].evidence["max_queued_per_tick"], 0.5)

    def test_event_log_takes_priority_over_prose(self):
        run = self._events("ev-path-clean.jsonl")
        copy_fixture("path-h1.log", run, "path-profile.log")
        self.assertEqual(rules(pathfind.check(run), "H1:"), [])

    def test_malformed_event_lines_are_skipped(self):
        findings = pathfind.check(self._events("ev-malformed.jsonl"))
        self.assertIsInstance(findings, list)
        for finding in findings:
            self.assertIsInstance(finding, Finding)


class AiPerfEventLogTests(unittest.TestCase):
    """When event-log.jsonl exists the ai_perf summary events are the only source."""

    def _events(self, fixture):
        return copy_fixture(fixture, tmp_dir(self), "event-log.jsonl").parent

    def test_f1_hit_from_events(self):
        f1 = rules(aiperf.check(self._events("ev-perf-f1.jsonl")), "F1:")
        self.assertEqual(len(f1), 1)
        self.assertEqual(f1[0].evidence["player"], 0)
        self.assertEqual(f1[0].evidence["pass"], "economy")
        self.assertEqual(f1[0].evidence["calls"], 15)
        self.assertAlmostEqual(f1[0].evidence["mean_ms"], 800.0 / 15.0, places=4)
        self.assertTrue(f1[0].evidence["file"].endswith("event-log.jsonl"))

    def test_f2_hit_sums_spikes_from_events(self):
        f2 = rules(aiperf.check(self._events("ev-perf-f2.jsonl")), "F2:")
        self.assertEqual(len(f2), 1)
        self.assertEqual(f2[0].evidence["player"], 1)
        self.assertEqual(f2[0].evidence["pass"], "army")
        self.assertEqual(f2[0].evidence["spikes"], 55)

    def test_clean_events_report_nothing(self):
        findings = aiperf.check(self._events("ev-perf-clean.jsonl"))
        self.assertEqual(rules(findings, "F1:"), [])
        self.assertEqual(rules(findings, "F2:"), [])

    def test_spike_events_are_not_double_counted(self):
        # A summary with 2 spikes plus a spike event for one of them must still
        # count 2, not 3.
        run = tmp_dir(self)
        with open(run / "event-log.jsonl", "w") as fh:
            fh.write(json.dumps({"schema": 1, "secs": 200.0, "tick": 3000, "ev": "ai_perf",
                                 "player": 0, "pass": "economy", "ms": 12.0, "calls": 2,
                                 "kind": "summary", "worst": 30.0, "spikes": 2}) + "\n")
            fh.write(json.dumps({"schema": 1, "secs": 200.0, "tick": 3000, "ev": "ai_perf",
                                 "player": 0, "pass": "economy", "ms": 30.0, "calls": 0,
                                 "kind": "spike", "worst": 30.0, "spikes": 1}) + "\n")
        f2 = rules(aiperf.check(run), "F2:")
        self.assertEqual(f2, [])

    def test_event_log_takes_priority_over_prose(self):
        run = self._events("ev-perf-clean.jsonl")
        copy_fixture("perf-f1.log", run, "ai-profile.log")
        self.assertEqual(rules(aiperf.check(run), "F1:"), [])

    def test_malformed_event_lines_are_skipped(self):
        findings = aiperf.check(self._events("ev-malformed.jsonl"))
        self.assertIsInstance(findings, list)
        for finding in findings:
            self.assertIsInstance(finding, Finding)


class DesyncEventLogTests(unittest.TestCase):
    SCENARIO = "standard-arms-coast"

    def _pair(self, ev_r1, ev_r2, csv_r1="desync-a.csv", csv_r2="desync-a.csv"):
        root = tmp_dir(self)
        r1 = root / self.SCENARIO / "seed1-control-r1"
        r2 = root / self.SCENARIO / "seed1-control-r2"
        for directory, ev, csv in ((r1, ev_r1, csv_r1), (r2, ev_r2, csv_r2)):
            directory.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(TESTDATA / "desync-result.log", directory / "game.log")
            if csv is not None:
                shutil.copyfile(TESTDATA / csv, directory / "ai-arena.csv")
            if ev is not None:
                shutil.copyfile(TESTDATA / ev, directory / "event-log.jsonl")
        return root, r1, r2

    def _context(self, root, r1, r2, arm):
        return runner.RunContext(
            self.SCENARIO, 1, arm,
            {"control-r1": str(r1), "control-r2": str(r2)},
            str(root), {})

    def test_identical_event_logs_are_clean(self):
        root, r1, r2 = self._pair("ev-desync-a.jsonl", "ev-desync-a.jsonl")
        findings = desync.check(r1, self._context(root, r1, r2, "control-r1"))
        self.assertEqual(rules(findings, "desync:"), [])

    def test_divergent_event_logs_are_likely_bug(self):
        root, r1, r2 = self._pair("ev-desync-a.jsonl", "ev-desync-b.jsonl")
        findings = desync.check(r1, self._context(root, r1, r2, "control-r1"))
        divergence = [f for f in findings if "event-log divergence" in f.summary]
        self.assertEqual(len(divergence), 1)
        self.assertEqual(divergence[0].severity, "likely-bug")
        evidence = divergence[0].evidence
        self.assertIsInstance(evidence["first_diff_offset"], int)
        self.assertGreater(evidence["first_diff_offset"], 0)
        self.assertEqual(evidence["ev"], "build_order")
        self.assertEqual(evidence["ev_b"], "build_order")

    def test_missing_event_log_keeps_csv_comparison(self):
        root, r1, r2 = self._pair(None, None, csv_r1="desync-a.csv", csv_r2="desync-b.csv")
        findings = desync.check(r1, self._context(root, r1, r2, "control-r1"))
        divergence = [f for f in findings if "divergence" in f.summary]
        self.assertEqual(len(divergence), 1)
        self.assertNotIn("event-log", divergence[0].summary)

    def test_csv_and_event_log_can_both_diverge(self):
        root, r1, r2 = self._pair("ev-desync-a.jsonl", "ev-desync-b.jsonl",
                                  csv_r1="desync-a.csv", csv_r2="desync-b.csv")
        findings = desync.check(r1, self._context(root, r1, r2, "control-r1"))
        self.assertEqual(len([f for f in findings if "divergence" in f.summary]), 2)

    def test_r2_does_not_report_event_log_divergence(self):
        root, r1, r2 = self._pair("ev-desync-a.jsonl", "ev-desync-b.jsonl")
        findings = desync.check(r2, self._context(root, r1, r2, "control-r2"))
        self.assertEqual([f for f in findings if "event-log divergence" in f.summary], [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
