#!/usr/bin/env python3
"""Tests for the playtest runner and checkers.

Run with ``python3 tools/playtest/tests.py``. Everything is stdlib and every
engine interaction goes through ``testdata/fake-ai-arena`` -- no build, no real
ai_arena.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

import run as runner  # noqa: E402
from checkers import CHECKERS, economy  # noqa: E402
from checkers.economy import Finding  # noqa: E402

TESTDATA = HERE / "testdata"
FAKE = TESTDATA / "fake-ai-arena"
RUN_PY = HERE / "run.py"

CSV_HEADER = (
    "tick,seconds,player,side,status,metal,energy,maxMetal,maxEnergy,"
    "metalIncome,energyIncome,metalDemand,energyDemand,"
    "units,buildings,army,builders,unitsLost,buildingsLost,"
    "idleBuilders,factories,idleFactories,armyMetal,"
    "metalProduced,metalExcess,energyProduced,energyExcess,phase"
).split(",")


def write_csv(path, samples):
    """Build a well-formed arena CSV from dicts; missing columns default to 0."""
    with open(path, "w") as fh:
        fh.write(",".join(CSV_HEADER) + "\n")
        for sample in samples:
            fh.write(",".join(str(sample.get(col, 0)) for col in CSV_HEADER) + "\n")


def sample(seconds, player=0, **overrides):
    row = {"tick": seconds * 10, "seconds": seconds, "player": player,
           "side": "ARM", "status": "alive"}
    row.update(overrides)
    return row


class MatrixTests(unittest.TestCase):
    def setUp(self):
        self.matrix = runner.load_matrix(HERE / "matrix.toml")

    def test_two_scenarios(self):
        ids = [s["id"] for s in self.matrix["scenarios"]]
        self.assertEqual(ids, ["standard-arms-coast", "tuned-naval-vs-control"])
        for scenario in self.matrix["scenarios"]:
            self.assertEqual(scenario["map"], "Coast To Coast")
            self.assertEqual(scenario["duration_s"], 900)
            self.assertEqual(scenario["seeds"], [1, 2, 3, 4, 5, 6])

    def test_tuned_scenario_tune_table(self):
        tuned = self.matrix["scenarios"][1]
        self.assertEqual(tuned["tune"], {"0": {"navalAggression": 1.5}})

    def test_players_normalized(self):
        players = runner.normalize_players(self.matrix["scenarios"][0])
        self.assertEqual([p["name"] for p in players], ["A", "B"])
        self.assertEqual([p["side"] for p in players], ["ARM", "CORE"])
        self.assertEqual([p["colour"] for p in players], [0, 1])


class PlanTests(unittest.TestCase):
    def setUp(self):
        self.matrix = runner.load_matrix(HERE / "matrix.toml")

    def test_full_plan_has_control_twins(self):
        runs = runner.expand(self.matrix)
        self.assertEqual(len(runs), 18)
        arms = {}
        for run in runs:
            arms.setdefault(run.scenario, set()).add(run.arm)
        self.assertEqual(arms["standard-arms-coast"], {"control"})
        self.assertEqual(arms["tuned-naval-vs-control"], {"tuned", "control"})

    def test_tuned_seed_has_both_arms(self):
        runs = runner.expand(self.matrix, scenario_filter=["tuned-naval-vs-control"], seeds_override=[3])
        ids = sorted(r.run_id for r in runs)
        self.assertEqual(ids, [
            "tuned-naval-vs-control/seed3-control",
            "tuned-naval-vs-control/seed3-tuned",
        ])

    def test_scenario_filter_and_seed_override(self):
        runs = runner.expand(self.matrix, scenario_filter=["standard-arms-coast"], seeds_override=[4])
        self.assertEqual(len(runs), 1)
        self.assertEqual(runs[0].run_id, "standard-arms-coast/seed4-control")
        self.assertEqual(runs[0].seed, 4)

    def test_run_dir_layout(self):
        runs = runner.expand(self.matrix, seeds_override=[1])
        self.assertEqual(
            runs[0].run_dir("/root").as_posix(),
            "/root/standard-arms-coast/seed1-control",
        )

    def test_command_tune_only_for_tuned_arm(self):
        tuned = next(r for r in runner.expand(self.matrix) if r.arm == "tuned")
        control = next(r for r in runner.expand(self.matrix) if r.arm == "control")
        cmd = runner.build_command("/bin/ai_arena", tuned, "/tmp/x")
        self.assertIn("--ai-tune", cmd)
        self.assertIn("0:navalAggression=1.5", cmd)
        self.assertIn("A;Computer;ARM;0", cmd)
        self.assertIn("B;Computer;CORE;1", cmd)
        self.assertIn("--ai-difficulty", cmd)
        self.assertIn("standard", cmd)
        self.assertNotIn("--ai-tune", runner.build_command("/bin/ai_arena", control, "/tmp/x"))

    def test_command_formats_boolean_knob(self):
        run = runner.PlannedRun("s/seed1-tuned", "s", 1, "tuned", "tuned", "m", 10,
                                [{"name": "A", "side": "ARM", "colour": 0, "difficulty": "standard"}],
                                tune={"1": {"someKnob": False}})
        cmd = runner.build_command("/bin/ai_arena", run, "/tmp/x")
        self.assertIn("1:someKnob=false", cmd)

    def test_plan_cli_against_real_matrix(self):
        proc = subprocess.run(
            [sys.executable, str(RUN_PY), "--plan"],
            cwd=str(REPO_ROOT), capture_output=True, text=True, timeout=60)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("standard-arms-coast", proc.stdout)
        self.assertIn("tuned-naval-vs-control", proc.stdout)
        self.assertIn("total: 18 runs", proc.stdout)
        self.assertIn("run root:", proc.stdout)

    def test_dry_run_is_plan_alias(self):
        proc = subprocess.run(
            [sys.executable, str(RUN_PY), "--dry-run", "--scenarios", "standard-arms-coast", "--seeds", "1"],
            cwd=str(REPO_ROOT), capture_output=True, text=True, timeout=60)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("total: 1 runs", proc.stdout)


class EconomyRuleTests(unittest.TestCase):
    def _fixture_dir(self, name):
        tmp = Path(tempfile.mkdtemp())
        shutil.copyfile(TESTDATA / name, tmp / "ai-arena.csv")
        self.addCleanup(shutil.rmtree, tmp, ignore_errors=True)
        return tmp

    def _rules(self, findings, prefix):
        return [f for f in findings if f.summary.startswith(prefix)]

    def test_e1_hit_is_likely_bug(self):
        findings = economy.check(self._fixture_dir("stall.csv"))
        e1 = self._rules(findings, "E1:")
        self.assertEqual(len(e1), 1)
        self.assertEqual(e1[0].severity, "likely-bug")
        self.assertEqual(e1[0].evidence["player"], 0)
        self.assertGreaterEqual(e1[0].evidence["samples"], 12)
        self.assertTrue(all(isinstance(r, int) for r in e1[0].evidence["rows"]))

    def test_e1_clean(self):
        self.assertEqual(self._rules(economy.check(self._fixture_dir("healthy.csv")), "E1:"), [])

    def test_e1_suspicious_short_run(self):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp, ignore_errors=True)
        rows = [sample(s, metal=0, maxMetal=1000, metalIncome=0, metalDemand=5, idleBuilders=1)
                for s in (300, 310, 320, 330)]
        rows.append(sample(340, metal=300, maxMetal=1000, metalIncome=2, metalDemand=1))
        write_csv(tmp / "ai-arena.csv", rows)
        e1 = self._rules(economy.check(tmp), "E1:")
        self.assertEqual(len(e1), 1)
        self.assertEqual(e1[0].severity, "suspicious")

    def test_e2_twin_delta(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        tuned = root / "seed1-tuned"
        control = root / "seed1-control"
        tuned.mkdir()
        control.mkdir()
        shutil.copyfile(TESTDATA / "twin-stall.csv", tuned / "ai-arena.csv")
        shutil.copyfile(TESTDATA / "twin-control.csv", control / "ai-arena.csv")
        context = runner.RunContext("s", 1, "tuned", {"tuned": str(tuned), "control": str(control)})
        e2 = self._rules(economy.check(tuned, context), "E2:")
        self.assertEqual(len(e2), 1)
        self.assertEqual(e2[0].severity, "suspicious")
        evidence = e2[0].evidence
        self.assertGreater(evidence["delta_points"], 25)
        self.assertIn("share", evidence["run"])
        self.assertIn("share", evidence["twin"])

    def test_e2_missing_twin_is_skipped(self):
        tuned = self._fixture_dir("twin-stall.csv")
        context = runner.RunContext("s", 1, "tuned", {})
        self.assertEqual(self._rules(economy.check(tuned, context), "E2:"), [])

    def test_e2_identical_twin_is_clean(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        tuned = root / "seed1-tuned"
        control = root / "seed1-control"
        tuned.mkdir()
        control.mkdir()
        shutil.copyfile(TESTDATA / "twin-control.csv", tuned / "ai-arena.csv")
        shutil.copyfile(TESTDATA / "twin-control.csv", control / "ai-arena.csv")
        context = runner.RunContext("s", 1, "tuned", {"tuned": str(tuned), "control": str(control)})
        self.assertEqual(self._rules(economy.check(tuned, context), "E2:"), [])

    def test_e3_hit_is_info(self):
        findings = economy.check(self._fixture_dir("waste-spike.csv"))
        e3 = self._rules(findings, "E3:")
        self.assertTrue(e3)
        self.assertEqual(e3[0].severity, "info")
        self.assertGreater(e3[0].evidence["jump_points"], 20)

    def test_e3_clean(self):
        self.assertEqual(self._rules(economy.check(self._fixture_dir("healthy.csv")), "E3:"), [])

    def test_malformed_csv_does_not_crash(self):
        findings = economy.check(self._fixture_dir("malformed.csv"))
        self.assertIsInstance(findings, list)
        for finding in findings:
            self.assertIsInstance(finding, Finding)

    def test_missing_csv_returns_empty(self):
        self.assertEqual(economy.check(Path(tempfile.mkdtemp())), [])

    def test_registry_exposes_economy(self):
        self.assertIn("economy", CHECKERS)
        self.assertTrue(callable(CHECKERS["economy"]))


class ExecuteTests(unittest.TestCase):
    def _run(self, run, run_dir, watchdog=30.0):
        return runner.execute_run(FAKE, run, run_dir, watchdog, False)

    def _planned(self, scenario="standard-arms-coast", seed=1, arm="control"):
        return runner.PlannedRun(
            f"{scenario}/seed{seed}-{arm}", scenario, seed, arm, arm,
            "Coast To Coast", 900,
            [{"name": "A", "side": "ARM", "colour": 0, "difficulty": "standard"},
             {"name": "B", "side": "CORE", "colour": 1, "difficulty": "standard"}])

    def test_status_ok(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self._run(self._planned(), Path(tmp))
            self.assertEqual(result.status, "ok")
            self.assertEqual(result.exit_code, 0)
            self.assertTrue((Path(tmp) / "ai-arena.csv").exists())
            self.assertIn("AI-ARENA-RESULT", (Path(tmp) / "game.log").read_text())

    def test_status_missing_run_json(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"FAKE_ARENA_RUN_JSON": "0"}):
            self.assertEqual(self._run(self._planned(), Path(tmp)).status, "missing-run-json")

    def test_status_no_result(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"FAKE_ARENA_NO_RESULT": "1"}):
            self.assertEqual(self._run(self._planned(), Path(tmp)).status, "no-result")

    def test_status_exit_code(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"FAKE_ARENA_EXIT": "3"}):
            result = self._run(self._planned(), Path(tmp))
            self.assertEqual(result.status, "exit-3")
            self.assertEqual(result.exit_code, 3)

    def test_watchdog_kills_and_marks_timeout(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"FAKE_ARENA_SLEEP": "10"}):
            started = time.monotonic()
            result = self._run(self._planned(), Path(tmp), watchdog=1.0)
            elapsed = time.monotonic() - started
            self.assertEqual(result.status, "timeout")
            self.assertLess(elapsed, 5.0)

    def test_binary_missing_is_a_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = runner.execute_run("/nonexistent/ai_arena", self._planned(), Path(tmp), 5.0, True)
            self.assertTrue(result.status.startswith("exit-"))
            self.assertTrue(result.binary_mtime_stale)

    def test_binary_is_stale_for_missing_binary(self):
        self.assertTrue(runner.binary_is_stale("/nonexistent/ai_arena"))

    def _integration_env(self, tmp, csv):
        return {**os.environ, "RWE_LOCAL_DATA": tmp, "FAKE_ARENA_CSV": str(csv)}

    def test_execute_end_to_end_healthy(self):
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.dict(os.environ, {"RWE_LOCAL_DATA": tmp}):
                proc = subprocess.run(
                    [sys.executable, str(RUN_PY), "--execute", "--scenarios", "standard-arms-coast",
                     "--seeds", "1", "--binary", str(FAKE), "--jobs", "1"],
                    cwd=str(REPO_ROOT), env=self._integration_env(tmp, TESTDATA / "healthy.csv"),
                    capture_output=True, text=True, timeout=120)
                self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
                root = runner.run_root_for(HERE / "matrix.toml")
                index = [json.loads(line) for line in (root / "index.jsonl").read_text().splitlines()]
                self.assertEqual(len(index), 1)
                entry = index[0]
                for key in ("run_id", "scenario", "seed", "arm", "status", "exit_code",
                            "wall_seconds", "binary_mtime_stale"):
                    self.assertIn(key, entry)
                self.assertEqual(entry["status"], "ok")
                self.assertEqual(entry["exit_code"], 0)
                self.assertIsInstance(entry["binary_mtime_stale"], bool)
                self.assertEqual((root / "findings.jsonl").read_text(), "")

    def test_execute_end_to_end_findings_exit_2(self):
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.dict(os.environ, {"RWE_LOCAL_DATA": tmp}):
                proc = subprocess.run(
                    [sys.executable, str(RUN_PY), "--execute", "--scenarios", "standard-arms-coast",
                     "--seeds", "1", "--binary", str(FAKE), "--jobs", "1"],
                    cwd=str(REPO_ROOT), env=self._integration_env(tmp, TESTDATA / "stall.csv"),
                    capture_output=True, text=True, timeout=120)
                self.assertEqual(proc.returncode, 2, proc.stdout + proc.stderr)
                root = runner.run_root_for(HERE / "matrix.toml")
                findings = [json.loads(line) for line in (root / "findings.jsonl").read_text().splitlines()]
                self.assertTrue(findings)
                self.assertEqual(findings[0]["checker"], "economy")
                self.assertIn("run_id", findings[0])


class CheckOnlyTests(unittest.TestCase):
    def _twin_root(self, stall_name, control_name):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        tuned = root / "tuned-naval-vs-control" / "seed1-tuned"
        control = root / "tuned-naval-vs-control" / "seed1-control"
        tuned.mkdir(parents=True)
        control.mkdir(parents=True)
        shutil.copyfile(TESTDATA / stall_name, tuned / "ai-arena.csv")
        shutil.copyfile(TESTDATA / control_name, control / "ai-arena.csv")
        return root

    def test_check_only_findings_exit_2(self):
        root = self._twin_root("twin-stall.csv", "twin-control.csv")
        proc = subprocess.run(
            [sys.executable, str(RUN_PY), "--check-only", str(root)],
            cwd=str(REPO_ROOT), capture_output=True, text=True, timeout=60)
        self.assertEqual(proc.returncode, 2, proc.stdout + proc.stderr)
        lines = (root / "findings.jsonl").read_text().splitlines()
        self.assertTrue(any(json.loads(line)["summary"].startswith("E2:") for line in lines))

    def test_check_only_clean_exit_0(self):
        root = self._twin_root("twin-control.csv", "twin-control.csv")
        proc = subprocess.run(
            [sys.executable, str(RUN_PY), "--check-only", str(root)],
            cwd=str(REPO_ROOT), capture_output=True, text=True, timeout=60)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertEqual((root / "findings.jsonl").read_text(), "")

    def test_scan_only_picks_newest_root(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp) / "playtest" / "runs"
            older = base / "2026-01-01-aaaaaaaa"
            newer = base / "2026-01-02-bbbbbbbb"
            for root, csv_name in ((older, "twin-stall.csv"), (newer, "twin-control.csv")):
                run_dir = root / "standard-arms-coast" / "seed1-control"
                run_dir.mkdir(parents=True)
                shutil.copyfile(TESTDATA / csv_name, run_dir / "ai-arena.csv")
            os.utime(older, (1000, 1000))
            os.utime(newer, (2000, 2000))
            with mock.patch.dict(os.environ, {"RWE_LOCAL_DATA": tmp}):
                self.assertEqual(runner.newest_run_root(), newer)
                proc = subprocess.run(
                    [sys.executable, str(RUN_PY), "--scan-only"],
                    cwd=str(REPO_ROOT), env={**os.environ, "RWE_LOCAL_DATA": tmp},
                    capture_output=True, text=True, timeout=60)
                self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
                self.assertTrue((newer / "findings.jsonl").exists())
                self.assertFalse((older / "findings.jsonl").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
