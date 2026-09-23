#!/usr/bin/env python3
"""Tests for the baseline store (``baselines.py``) and the nightly script.

Run with ``python3 tools/playtest/tests_baselines_nightly.py``. Everything is
stdlib and no real ``ai_arena`` is ever built or run; the execute-path tests
use ``testdata/fake-ai-arena`` exactly as ``tests.py`` does.

Git-dependent paths are exercised only through their graceful branches -- a
temporary data root, a mocked ``current_sha``, or a missing baselines
directory -- so nothing here asserts on this repo's history.
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

import baselines  # noqa: E402
from testsupport import copy_fake_binary  # noqa: E402
import run as runner  # noqa: E402

TESTDATA = HERE / "testdata"
RUN_PY = HERE / "run.py"
NIGHTLY = HERE / "nightly.sh"

CSV_HEADER = (
    "tick,seconds,player,side,status,metal,energy,maxMetal,maxEnergy,"
    "metalIncome,energyIncome,metalDemand,energyDemand,"
    "units,buildings,army,builders,unitsLost,buildingsLost,"
    "idleBuilders,factories,idleFactories,armyMetal,"
    "metalProduced,metalExcess,energyProduced,energyExcess,phase"
).split(",")

EVENTS_HEADER = (
    "player,unitType,category,isBuilding,startedTick,startedSeconds,"
    "completedTick,completedSeconds,diedTick,diedSeconds,"
    "x,z,enemiesNear,nearestEnemyType,friendlyArmyNear,friendlyTowersNear"
).split(",")


def write_csv(path, samples):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w") as fh:
        fh.write(",".join(CSV_HEADER) + "\n")
        for sample in samples:
            fh.write(",".join(str(sample.get(col, 0)) for col in CSV_HEADER) + "\n")


def sample(seconds, player=0, **overrides):
    row = {"tick": seconds * 10, "seconds": seconds, "player": player,
           "side": "ARM", "status": "alive"}
    row.update(overrides)
    return row


def write_events(path, rows):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w") as fh:
        fh.write(",".join(EVENTS_HEADER) + "\n")
        for row in rows:
            fh.write(",".join(str(row.get(col, "")) for col in EVENTS_HEADER) + "\n")


def post_480(seconds_step=10):
    return list(range(480, 910, seconds_step))


def baseline_path(data_root, scenario, sha):
    return Path(data_root) / "playtest" / "baselines" / f"{scenario}.{sha}.json"


class WriteBaselinesTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.data = self.tmp / "data"
        self.run_root = self.tmp / "runs"
        self.scenario = "standard-arms-coast"

    def _single_seed_run(self):
        run_dir = self.run_root / self.scenario / "seed1-control"
        rows = [sample(100, 0, metal=0, maxMetal=1000, metalDemand=5, metalIncome=0)]
        for seconds in post_480():
            rows.append(sample(
                seconds, 0, metal=0, maxMetal=1000, energy=100, maxEnergy=1000,
                metalDemand=5, metalIncome=0, energyProduced=100, energyExcess=50,
                builders=2, idleBuilders=1, factories=1, idleFactories=0))
            rows.append(sample(
                seconds, 1, metal=500, maxMetal=1000, energy=960, maxEnergy=1000,
                metalDemand=1, metalIncome=2, energyProduced=100, energyExcess=50,
                builders=4, idleBuilders=0, factories=2, idleFactories=1))
        write_csv(run_dir / "ai-arena.csv", rows)
        write_events(run_dir / "ai-arena-events.csv", [
            {"player": 0, "unitType": "ARMMEX", "category": "economy", "completedSeconds": 200},
            {"player": 0, "unitType": "ARMMEX", "category": "economy", "completedSeconds": 120},
            {"player": 0, "unitType": "ARMVP", "category": "factory", "completedSeconds": 360},
            {"player": 0, "unitType": "ARMMEX", "category": "economy", "completedSeconds": ""},
            {"player": 0, "unitType": "ARMPW", "category": "army", "completedSeconds": 400},
        ])
        (run_dir / "ai-profile.log").write_text(
            "AI profile summary: player 0 at tick 9000, total 12.5 ms: "
            "build=3.0/10 (worst 5.0, 2 spikes) army=9.5/20 (worst 8.0, 3 spikes)\n")
        return run_dir

    def _write(self):
        with mock.patch.object(baselines, "current_sha", return_value="abc1234"):
            return baselines.write_baselines(self.run_root, REPO_ROOT, data_root=self.data)

    def test_schema_and_aggregates(self):
        self._single_seed_run()
        result = self._write()
        self.assertEqual(result, self.data / "playtest" / "baselines")

        path = baseline_path(self.data, self.scenario, "abc1234")
        self.assertTrue(path.is_file())
        doc = json.loads(path.read_text())
        self.assertEqual(doc["schema"], 1)
        self.assertEqual(doc["scenario"], self.scenario)
        self.assertEqual(doc["sha"], "abc1234")
        self.assertEqual(set(doc["arms"]), {"control"})

        arm = doc["arms"]["control"]
        self.assertEqual(set(arm["shares"]), set(baselines.SHARE_NAMES))
        self.assertAlmostEqual(arm["shares"]["stalled"], 0.5, places=4)
        self.assertAlmostEqual(arm["shares"]["capped"], 0.5, places=4)
        self.assertAlmostEqual(arm["shares"]["wasted_energy"], 0.5, places=4)
        self.assertAlmostEqual(arm["shares"]["idle_builders"], 0.25, places=4)
        self.assertAlmostEqual(arm["shares"]["idle_factories"], 0.25, places=4)
        self.assertAlmostEqual(arm["milestones"]["first_economy_seconds"], 120.0, places=3)
        self.assertAlmostEqual(arm["milestones"]["first_factory_seconds"], 360.0, places=3)
        self.assertAlmostEqual(arm["perf"]["0:build"]["mean_ms"], 0.3, places=4)
        self.assertAlmostEqual(arm["perf"]["0:build"]["spikes"], 2.0, places=2)
        self.assertAlmostEqual(arm["perf"]["0:army"]["mean_ms"], 0.475, places=4)

    def test_deterministic_bytes(self):
        self._single_seed_run()
        self._write()
        path = baseline_path(self.data, self.scenario, "abc1234")
        first = path.read_bytes()
        self._write()
        self.assertEqual(path.read_bytes(), first)

    def test_repeat_runs_average_into_base_arm(self):
        scenario = "tuned-naval-vs-control"
        stalled = self.run_root / scenario / "seed1-control-r1"
        healthy = self.run_root / scenario / "seed1-control-r2"
        write_csv(stalled / "ai-arena.csv", [
            sample(s, 0, metal=0, maxMetal=1000, metalDemand=5, metalIncome=0)
            for s in post_480()])
        write_csv(healthy / "ai-arena.csv", [
            sample(s, 0, metal=500, maxMetal=1000, metalDemand=1, metalIncome=2)
            for s in post_480()])

        with mock.patch.object(baselines, "current_sha", return_value="abc1234"):
            baselines.write_baselines(self.run_root, REPO_ROOT, data_root=self.data)
        doc = json.loads(baseline_path(self.data, scenario, "abc1234").read_text())
        self.assertEqual(set(doc["arms"]), {"control"})
        self.assertAlmostEqual(doc["arms"]["control"]["shares"]["stalled"], 0.5, places=4)

    def test_missing_csv_returns_none(self):
        (self.run_root / self.scenario / "seed1-control").mkdir(parents=True)
        self.assertIsNone(self._write())
        self.assertFalse((self.data / "playtest" / "baselines").exists())

    def test_empty_sha_returns_none(self):
        self._single_seed_run()
        with mock.patch.object(baselines, "current_sha", return_value=""):
            self.assertIsNone(
                baselines.write_baselines(self.run_root, REPO_ROOT, data_root=self.data))
        self.assertFalse((self.data / "playtest" / "baselines").exists())


class ClosestBaselineTests(unittest.TestCase):
    def test_none_without_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(
                baselines.closest_baseline("standard-arms-coast", "abc1234", data_root=tmp))

    def test_empty_sha_is_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(
                baselines.closest_baseline("standard-arms-coast", "", data_root=tmp))

    def test_current_sha_graceful_without_git(self):
        self.assertEqual(baselines.current_sha("/nonexistent/repo/path"), "")


class DriftTests(unittest.TestCase):
    def test_reports_only_shares_over_threshold(self):
        current = {"shares": {"stalled": 0.4, "capped": 0.1, "idle_builders": 0.2}}
        baseline = {"shares": {"stalled": 0.2, "capped": 0.1}}
        moved = baselines.drift(current, baseline, 10)
        self.assertEqual(set(moved), {"stalled"})
        self.assertEqual(moved["stalled"], {
            "current": 0.4, "baseline": 0.2, "delta_points": 20.0})

    def test_threshold_is_strict(self):
        self.assertEqual(baselines.drift({"shares": {"stalled": 0.3}},
                                         {"shares": {"stalled": 0.2}}, 10), {})

    def test_bare_share_mappings(self):
        moved = baselines.drift({"stalled": 0.5}, {"stalled": 0.1}, 10)
        self.assertIn("stalled", moved)

    def test_missing_inputs_skipped_gracefully(self):
        self.assertEqual(baselines.drift(None, {"shares": {"stalled": 1.0}}, 10), {})
        self.assertEqual(baselines.drift({"shares": {"stalled": 1.0}}, None, 10), {})


class RunnerFlagTests(unittest.TestCase):
    def test_no_baselines_flag_parses(self):
        parser = runner.build_parser()
        self.assertFalse(parser.parse_args([]).no_baselines)
        self.assertTrue(parser.parse_args(["--no-baselines"]).no_baselines)

    def _binary(self, directory, mtime=None):
        return copy_fake_binary(directory, mtime)

    def _args(self, binary, extra=()):
        return runner.build_parser().parse_args([
            "--execute", "--scenarios", "standard-arms-coast", "--seeds", "1",
            "--binary", str(binary), "--jobs", "1", *extra])

    def test_stale_binary_writes_no_baselines(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "data"
            # Aged to the epoch: older than anything under src/, so stale.
            binary = self._binary(tmp, mtime=1000)
            with mock.patch.dict(os.environ, {"RWE_LOCAL_DATA": str(data)}), \
                    mock.patch.object(baselines, "current_sha", return_value="deadbee"):
                self.assertEqual(runner.do_execute(self._args(binary)), 0)
            self.assertFalse((data / "playtest" / "baselines").exists())

    def test_fresh_binary_writes_baselines(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "data"
            binary = self._binary(tmp)
            with mock.patch.dict(os.environ, {"RWE_LOCAL_DATA": str(data)}), \
                    mock.patch.object(baselines, "current_sha", return_value="deadbee"):
                self.assertEqual(runner.do_execute(self._args(binary)), 0)
            files = list((data / "playtest" / "baselines").glob("standard-arms-coast.*.json"))
            self.assertEqual(len(files), 1)

    def test_no_baselines_suppresses_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "data"
            binary = self._binary(tmp)
            with mock.patch.dict(os.environ, {"RWE_LOCAL_DATA": str(data)}), \
                    mock.patch.object(baselines, "current_sha", return_value="deadbee"):
                self.assertEqual(
                    runner.do_execute(self._args(binary, ("--no-baselines",))), 0)
            self.assertFalse((data / "playtest" / "baselines").exists())


class NightlyDryRunTests(unittest.TestCase):
    def test_dry_run_prints_plan_and_touches_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            data = tmp / "data"
            worktree = tmp / "nightly"
            env = {
                **os.environ,
                "NIGHTLY_DRY_RUN": "1",
                "NIGHTLY_WORKTREE": str(worktree),
                "NIGHTLY_KEEP": "3",
                "RWE_LOCAL_DATA": str(data),
            }
            proc = subprocess.run(
                ["bash", str(NIGHTLY)], cwd=str(REPO_ROOT), env=env,
                capture_output=True, text=True, timeout=60)
            self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
            self.assertIn("dry-run", proc.stdout)
            self.assertIn("cmake --build", proc.stdout)
            self.assertIn("run.py --execute", proc.stdout)
            self.assertIn("rotation", proc.stdout)
            self.assertFalse(worktree.exists())
            self.assertFalse((data / "playtest").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
