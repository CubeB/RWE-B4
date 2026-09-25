#!/usr/bin/env python3
"""Tests for tools/ai-experiment.py.

Run with ``python3 tools/playtest/tests_ai_experiment.py``. Everything is
stdlib; no engine build, no real ai_arena -- synthetic run roots are built at
test time in a temp dir, in both the current (run.py) and the older
(ai-arena.ps1) layouts.
"""

from __future__ import annotations

import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
TOOLS_DIR = REPO_ROOT / "tools"

if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import run as playtest_run  # noqa: E402


def _load_ai_experiment():
    spec = importlib.util.spec_from_file_location("ai_experiment", TOOLS_DIR / "ai-experiment.py")
    module = importlib.util.module_from_spec(spec)
    # dataclass() looks its owning module up in sys.modules by __module__ name;
    # it must be registered there before exec_module runs the class bodies.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


ai_experiment = _load_ai_experiment()

CSV_HEADER = (
    "tick,seconds,player,side,status,metal,energy,maxMetal,maxEnergy,"
    "metalIncome,energyIncome,metalDemand,energyDemand,"
    "units,buildings,army,builders,unitsLost,buildingsLost,"
    "idleBuilders,factories,idleFactories,armyMetal,"
    "metalProduced,metalExcess,energyProduced,energyExcess,phase"
).split(",")

OLD_CSV_HEADER = (
    "tick,seconds,player,side,status,metal,energy,maxMetal,maxEnergy,"
    "metalIncome,energyIncome,metalDemand,energyDemand,"
    "units,buildings,army,builders,unitsLost,buildingsLost"
).split(",")

EVENTS_HEADER = (
    "player,unitType,category,isBuilding,startedTick,startedSeconds,"
    "completedTick,completedSeconds,diedTick,diedSeconds,"
    "x,z,enemiesNear,nearestEnemyType,friendlyArmyNear,friendlyTowersNear,"
    "deathCause,killerType,killerPlayer"
).split(",")

OLD_EVENTS_HEADER = (
    "player,unitType,category,isBuilding,startedTick,startedSeconds,"
    "completedTick,completedSeconds,diedTick,diedSeconds,"
    "x,z,enemiesNear,nearestEnemyType,friendlyArmyNear,friendlyTowersNear"
).split(",")


def write_csv(path, header, rows):
    with open(path, "w") as fh:
        fh.write(",".join(header) + "\n")
        for row in rows:
            fh.write(",".join(str(row.get(col, "")) for col in header) + "\n")


def sample(seconds, player, army_metal, army_units=2, metal_income=5.0, energy_income=50.0, **overrides):
    row = {
        "tick": seconds * 10, "seconds": seconds, "player": player, "side": "ARM", "status": "alive",
        "metal": 100, "energy": 1000, "maxMetal": 200, "maxEnergy": 2000,
        "metalIncome": metal_income, "energyIncome": energy_income, "metalDemand": metal_income,
        "energyDemand": energy_income, "units": army_units + 2, "buildings": 5, "army": army_units,
        "builders": 2, "unitsLost": 0, "buildingsLost": 0, "idleBuilders": 0, "factories": 1,
        "idleFactories": 0, "armyMetal": army_metal, "metalProduced": metal_income * seconds,
        "metalExcess": 0, "energyProduced": energy_income * seconds, "energyExcess": 0, "phase": "buildup",
    }
    row.update(overrides)
    return row


def event(player, unit_type, category, completed_seconds=None, died_seconds=None, killer_player=None, is_building=0):
    return {
        "player": player, "unitType": unit_type, "category": category, "isBuilding": is_building,
        "startedTick": 0, "startedSeconds": 0,
        "completedTick": (completed_seconds * 10) if completed_seconds is not None else "",
        "completedSeconds": completed_seconds if completed_seconds is not None else "",
        "diedTick": (died_seconds * 10) if died_seconds is not None else "",
        "diedSeconds": died_seconds if died_seconds is not None else "",
        "x": 0, "z": 0, "enemiesNear": "", "nearestEnemyType": "", "friendlyArmyNear": "", "friendlyTowersNear": "",
        "deathCause": "", "killerType": "", "killerPlayer": killer_player if killer_player is not None else "",
    }


def result_line(p0_status, p0_units, p0_army, p1_status, p1_units, p1_army):
    return (
        f"AI-ARENA-RESULT ticks=90000 seconds=900 | "
        f"p0=ARM {p0_status} units={p0_units} buildings=5 army={p0_army} lost=1 metalIncome=5 types=ARMSOLARx1 | "
        f"p1=CORE {p1_status} units={p1_units} buildings=5 army={p1_army} lost=1 metalIncome=5 types=CORSOLARx1\n"
    )


def make_new_run(root, scenario, seed, arm, tuned_player, tune, result, samples_p0, samples_p1, events, header=CSV_HEADER, events_header=EVENTS_HEADER):
    run_dir = Path(root) / scenario / f"seed{seed}-{arm}"
    run_dir.mkdir(parents=True, exist_ok=True)
    write_csv(run_dir / "ai-arena.csv", header, samples_p0 + samples_p1)
    write_csv(run_dir / "ai-arena-events.csv", events_header, events)
    (run_dir / "game.log").write_text(result)
    players = [
        {"index": 0, "tune": (tune if (arm == "tuned" and tuned_player == 0) else {})},
        {"index": 1, "tune": (tune if (arm == "tuned" and tuned_player == 1) else {})},
    ]
    run_json = {"schema": 1, "map": "Coast To Coast", "seed": seed, "durationSeconds": 900, "players": players}
    (run_dir / "run.json").write_text(json.dumps(run_json))
    return run_dir


def make_old_run(root, seed, arm, result, samples_p0, samples_p1, events):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    tag = f"game-{seed}" + ("-control" if arm == "control" else "")
    (root / f"{tag}.log").write_text(result)
    write_csv(root / f"{tag}-ai-arena.csv", CSV_HEADER, samples_p0 + samples_p1)
    write_csv(root / f"{tag}-ai-arena-events.csv", EVENTS_HEADER, events)


# --------------------------------------------------------------------------
# statistics
# --------------------------------------------------------------------------

class StatsTests(unittest.TestCase):
    def test_wilson_95_for_7_of_10(self):
        lo, hi = ai_experiment.wilson_interval(7, 10)
        self.assertAlmostEqual(lo, 0.397, places=3)
        self.assertAlmostEqual(hi, 0.892, places=3)

    def test_wilson_bounds_stay_in_0_1(self):
        lo, hi = ai_experiment.wilson_interval(0, 5)
        self.assertGreaterEqual(lo, 0.0)
        lo, hi = ai_experiment.wilson_interval(5, 5)
        self.assertLessEqual(hi, 1.0)

    def test_wilson_empty_n(self):
        self.assertEqual(ai_experiment.wilson_interval(0, 0), (0.0, 1.0))

    def test_sign_test_8_of_10_two_sided(self):
        p = ai_experiment.sign_test_two_sided(8, 10)
        self.assertAlmostEqual(p, 0.109375, places=6)

    def test_sign_test_symmetric(self):
        # 2 of 10 (the minority side) must give the same p as 8 of 10.
        self.assertAlmostEqual(
            ai_experiment.sign_test_two_sided(2, 10),
            ai_experiment.sign_test_two_sided(8, 10),
            places=9,
        )

    def test_sign_test_even_split_is_1(self):
        self.assertAlmostEqual(ai_experiment.sign_test_two_sided(5, 10), 1.0, places=6)

    def test_bootstrap_reproducible_under_fixed_seed(self):
        diffs = [1.0, 2.0, -1.0, 3.0, 0.5, -0.5, 2.5, 1.5]
        a = ai_experiment.paired_bootstrap_ci(diffs, n_resamples=2000, seed=42)
        b = ai_experiment.paired_bootstrap_ci(diffs, n_resamples=2000, seed=42)
        self.assertEqual(a, b)

    def test_bootstrap_different_seed_can_differ(self):
        diffs = [1.0, 2.0, -1.0, 3.0, 0.5, -0.5, 2.5, 1.5, 4.0, -2.0]
        a = ai_experiment.paired_bootstrap_ci(diffs, n_resamples=500, seed=1)
        b = ai_experiment.paired_bootstrap_ci(diffs, n_resamples=500, seed=2)
        # Not asserting inequality (they could coincide), just that both are
        # well-formed and bracket the mean roughly.
        mean = sum(diffs) / len(diffs)
        self.assertLessEqual(a[0], mean + 1e-9)
        self.assertGreaterEqual(a[1], mean - 1e-9)
        self.assertLessEqual(b[0], mean + 1e-9)

    def test_bootstrap_single_value(self):
        self.assertEqual(ai_experiment.paired_bootstrap_ci([3.0]), (3.0, 3.0))

    def test_bootstrap_empty(self):
        self.assertEqual(ai_experiment.paired_bootstrap_ci([]), (None, None))

    def test_power_n_for_proportion_zero_effect(self):
        self.assertIsNone(ai_experiment.power_n_for_proportion(0.5))

    def test_power_n_for_proportion_positive(self):
        n = ai_experiment.power_n_for_proportion(0.8)
        self.assertIsNotNone(n)
        self.assertGreater(n, 0)

    def test_power_n_for_paired_mean_zero_sd(self):
        self.assertIsNone(ai_experiment.power_n_for_paired_mean(5.0, 0.0))


# --------------------------------------------------------------------------
# result-line parsing and outcome classification
# --------------------------------------------------------------------------

class OutcomeTests(unittest.TestCase):
    def test_parse_result_line(self):
        line = result_line("alive", 20, 10, "dead", 0, 0)
        parsed = ai_experiment.parse_result_line(line)
        self.assertEqual(parsed["players"][0]["status"], "alive")
        self.assertEqual(parsed["players"][1]["status"], "dead")
        self.assertEqual(parsed["players"][0]["army"], 10)

    def test_win_by_elimination(self):
        result = ai_experiment.parse_result_line(result_line("alive", 20, 10, "dead", 0, 0))
        self.assertEqual(ai_experiment.classify_outcome(result, 0), "win")
        self.assertEqual(ai_experiment.classify_outcome(result, 1), "loss")

    def test_draw_when_army_close(self):
        result = ai_experiment.parse_result_line(result_line("alive", 20, 10, "alive", 20, 11))
        self.assertEqual(ai_experiment.classify_outcome(result, 0), "draw")

    def test_win_by_army_margin_when_undecided(self):
        result = ai_experiment.parse_result_line(result_line("alive", 20, 20, "alive", 20, 5))
        self.assertEqual(ai_experiment.classify_outcome(result, 0), "win")
        self.assertEqual(ai_experiment.classify_outcome(result, 1), "loss")

    def test_no_result_line(self):
        self.assertIsNone(ai_experiment.parse_result_line("nothing to see here"))

    def test_unknown_player(self):
        result = ai_experiment.parse_result_line(result_line("alive", 20, 10, "dead", 0, 0))
        self.assertIsNone(ai_experiment.classify_outcome(result, 5))


# --------------------------------------------------------------------------
# pairing and tuned-player detection: both layouts
# --------------------------------------------------------------------------

class PairingNewLayoutTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rwe-ai-exp-")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def test_tuned_player_from_run_json(self):
        root = Path(self.tmp) / "root"
        tune = {"navalAttackFleetSize": "1"}
        make_new_run(root, "sc", 2, "tuned", tuned_player=1, tune=tune,
                     result=result_line("alive", 10, 5, "dead", 0, 0),
                     samples_p0=[sample(300, 0, 40)], samples_p1=[sample(300, 1, 30)], events=[])
        make_new_run(root, "sc", 2, "control", tuned_player=None, tune={},
                     result=result_line("dead", 0, 0, "alive", 10, 5),
                     samples_p0=[sample(300, 0, 20)], samples_p1=[sample(300, 1, 35)], events=[])
        locs = ai_experiment.discover_new_layout(root)
        self.assertEqual(len(locs), 2)
        pairs = ai_experiment.build_pairs(locs)
        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0].tuned_player, 1)
        self.assertEqual(pairs[0].scenario, "sc")
        self.assertEqual(pairs[0].seed, 2)

    def test_unpaired_run_is_skipped(self):
        root = Path(self.tmp) / "root"
        make_new_run(root, "sc", 1, "tuned", tuned_player=0, tune={"x": "1"},
                     result=result_line("alive", 10, 5, "dead", 0, 0),
                     samples_p0=[], samples_p1=[], events=[])
        locs = ai_experiment.discover_new_layout(root)
        pairs = ai_experiment.build_pairs(locs)
        self.assertEqual(pairs, [])

    def test_scenario_dir_passed_directly(self):
        root = Path(self.tmp) / "root"
        make_new_run(root, "sc", 1, "tuned", tuned_player=0, tune={"x": "1"},
                     result=result_line("alive", 10, 5, "dead", 0, 0),
                     samples_p0=[sample(300, 0, 10)], samples_p1=[sample(300, 1, 10)], events=[])
        make_new_run(root, "sc", 1, "control", tuned_player=None, tune={},
                     result=result_line("alive", 10, 5, "dead", 0, 0),
                     samples_p0=[sample(300, 0, 10)], samples_p1=[sample(300, 1, 10)], events=[])
        locs = ai_experiment.discover_new_layout(root / "sc")
        self.assertEqual(len(locs), 2)


class PairingOldLayoutTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rwe-ai-exp-old-")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def test_odd_seed_tunes_p0_even_tunes_p1(self):
        root = Path(self.tmp) / "batch"
        make_old_run(root, 1, "tuned", result_line("alive", 10, 5, "dead", 0, 0),
                     [sample(300, 0, 10)], [sample(300, 1, 10)], [])
        make_old_run(root, 1, "control", result_line("alive", 10, 5, "dead", 0, 0),
                     [sample(300, 0, 10)], [sample(300, 1, 10)], [])
        make_old_run(root, 2, "tuned", result_line("dead", 0, 0, "alive", 10, 5),
                     [sample(300, 0, 10)], [sample(300, 1, 10)], [])
        make_old_run(root, 2, "control", result_line("dead", 0, 0, "alive", 10, 5),
                     [sample(300, 0, 10)], [sample(300, 1, 10)], [])

        locs = ai_experiment.discover(root)
        self.assertEqual(len(locs), 4)
        self.assertTrue(all(loc.layout == "old" for loc in locs))
        pairs = {p.seed: p for p in ai_experiment.build_pairs(locs)}
        self.assertEqual(pairs[1].tuned_player, 0)
        self.assertEqual(pairs[2].tuned_player, 1)

    def test_old_layout_csv_paths(self):
        root = Path(self.tmp) / "batch"
        make_old_run(root, 3, "tuned", result_line("alive", 10, 5, "dead", 0, 0),
                     [sample(300, 0, 10)], [sample(300, 1, 10)], [])
        make_old_run(root, 3, "control", result_line("alive", 10, 5, "dead", 0, 0),
                     [sample(300, 0, 10)], [sample(300, 1, 10)], [])
        locs = {loc.arm: loc for loc in ai_experiment.discover(root)}
        self.assertTrue(locs["tuned"].csv_path.name == "game-3-ai-arena.csv")
        self.assertTrue(locs["control"].csv_path.name == "game-3-control-ai-arena.csv")
        self.assertTrue(locs["tuned"].csv_path.exists())
        self.assertTrue(locs["control"].csv_path.exists())


# --------------------------------------------------------------------------
# missing-column root: never a crash
# --------------------------------------------------------------------------

class MissingColumnsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rwe-ai-exp-missing-")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def test_old_16_column_events_drops_killer_metric_but_keeps_others(self):
        root = Path(self.tmp) / "root"
        tune = {"navalAttackFleetSize": "1"}
        events = [
            event(0, "ARMMEX", "economy", completed_seconds=90),
            event(0, "ARMMEX", "economy", completed_seconds=150),
            event(0, "ARMMEX", "economy", completed_seconds=210),
            event(1, "CORLAB", "factory", completed_seconds=30, is_building=1),
        ]
        make_new_run(root, "sc", 1, "tuned", tuned_player=0, tune=tune,
                     result=result_line("alive", 20, 15, "dead", 0, 0),
                     samples_p0=[sample(t, 0, 10 * t / 100) for t in (60, 300, 600, 900)],
                     samples_p1=[sample(t, 1, 5 * t / 100) for t in (60, 300, 600, 900)],
                     events=events, events_header=OLD_EVENTS_HEADER)
        make_new_run(root, "sc", 1, "control", tuned_player=None, tune={},
                     result=result_line("alive", 15, 10, "dead", 0, 0),
                     samples_p0=[sample(t, 0, 8 * t / 100) for t in (60, 300, 600, 900)],
                     samples_p1=[sample(t, 1, 5 * t / 100) for t in (60, 300, 600, 900)],
                     events=events, events_header=OLD_EVENTS_HEADER)

        locs = ai_experiment.discover_new_layout(root)
        pairs = ai_experiment.build_pairs(locs)
        self.assertEqual(len(pairs), 1)
        tuned_bundle = ai_experiment.build_bundle(pairs[0].tuned, 0)
        # killerPlayer column absent entirely -> units_killed is reported absent, not a crash.
        self.assertIsNone(ai_experiment._units_killed(tuned_bundle))
        # but other metrics, which don't depend on the killer columns, still read.
        self.assertIsNotNone(ai_experiment._end(tuned_bundle, "armyMetal"))
        self.assertIsNotNone(ai_experiment._nth_extractor(tuned_bundle, 3))

    def test_missing_csv_file_does_not_crash(self):
        root = Path(self.tmp) / "root"
        run_dir = root / "sc" / "seed1-tuned"
        run_dir.mkdir(parents=True)
        (run_dir / "game.log").write_text(result_line("alive", 10, 5, "dead", 0, 0))
        (run_dir / "run.json").write_text(json.dumps({
            "players": [{"index": 0, "tune": {"x": "1"}}, {"index": 1, "tune": {}}],
        }))
        control_dir = root / "sc" / "seed1-control"
        control_dir.mkdir(parents=True)
        (control_dir / "game.log").write_text(result_line("alive", 10, 5, "dead", 0, 0))
        (control_dir / "run.json").write_text(json.dumps({
            "players": [{"index": 0, "tune": {}}, {"index": 1, "tune": {}}],
        }))

        locs = ai_experiment.discover_new_layout(root)
        pairs = ai_experiment.build_pairs(locs)
        self.assertEqual(len(pairs), 1)
        # No ai-arena.csv / ai-arena-events.csv on disk at all.
        bundle = ai_experiment.build_bundle(pairs[0].tuned, 0)
        self.assertEqual(bundle.samples, [])
        self.assertIsNone(bundle.analysis)
        self.assertIsNone(ai_experiment._end(bundle, "armyMetal"))

        # compare must still run to completion off the log-derived outcome alone.
        parser = ai_experiment.build_parser()
        args = parser.parse_args(["compare", str(root)])
        rc = ai_experiment.cmd_compare(args)
        self.assertEqual(rc, 0)


# --------------------------------------------------------------------------
# sweep grouping
# --------------------------------------------------------------------------

class SweepTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rwe-ai-exp-sweep-")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.root = Path(self.tmp) / "root"

        # Value "1": tuned wins both seeds. Value "5": tuned loses both seeds.
        for seed, value, win in ((1, "1", True), (2, "1", True), (3, "5", False), (4, "5", False)):
            tune = {"navalAttackFleetSize": value}
            if win:
                tuned_result = result_line("alive", 20, 15, "dead", 0, 0)
                control_result = result_line("alive", 12, 8, "dead", 0, 0)
            else:
                tuned_result = result_line("dead", 0, 0, "alive", 20, 15)
                control_result = result_line("alive", 12, 8, "dead", 0, 0)
            make_new_run(self.root, "sweep-navalAttackFleetSize-" + value, seed, "tuned", tuned_player=0, tune=tune,
                         result=tuned_result, samples_p0=[sample(300, 0, 50)], samples_p1=[sample(300, 1, 10)],
                         events=[])
            make_new_run(self.root, "sweep-navalAttackFleetSize-" + value, seed, "control", tuned_player=None,
                         tune={}, result=control_result, samples_p0=[sample(300, 0, 30)],
                         samples_p1=[sample(300, 1, 10)], events=[])

    def test_groups_by_single_tuned_knob(self):
        parser = ai_experiment.build_parser()
        args = parser.parse_args(["sweep", str(self.root)])
        rc = ai_experiment.cmd_sweep(args)
        self.assertEqual(rc, 0)

    def test_grouping_values_directly(self):
        locs = ai_experiment.discover_new_layout(self.root)
        pairs = ai_experiment.build_pairs(locs)
        self.assertEqual(len(pairs), 4)
        by_value = {}
        for pair in pairs:
            data = json.loads(pair.tuned.run_json_path.read_text())
            tune = next(p["tune"] for p in data["players"] if p["index"] == pair.tuned_player)
            by_value.setdefault(tune["navalAttackFleetSize"], []).append(pair)
        self.assertEqual(set(by_value.keys()), {"1", "5"})
        self.assertEqual(len(by_value["1"]), 2)
        self.assertEqual(len(by_value["5"]), 2)


# --------------------------------------------------------------------------
# plan: TOML round-trip through run.py's own loader/expand, and unknown-knob rejection
# --------------------------------------------------------------------------

class PlanTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rwe-ai-exp-plan-")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def test_plan_writes_a_matrix_run_py_can_load_and_expand(self):
        out = Path(self.tmp) / "sweep.toml"
        parser = ai_experiment.build_parser()
        args = parser.parse_args([
            "plan", "--knob", "navalAttackFleetSize", "--values", "1,3,5",
            "--seeds", "1-4", "--map", "Coast To Coast", "--duration", "300", "--out", str(out),
        ])
        rc = ai_experiment.cmd_plan(args)
        self.assertEqual(rc, 0)
        self.assertTrue(out.exists())

        matrix = playtest_run.load_matrix(out)
        ids = [s["id"] for s in matrix["scenarios"]]
        self.assertEqual(ids, [
            "sweep-navalAttackFleetSize-1",
            "sweep-navalAttackFleetSize-3",
            "sweep-navalAttackFleetSize-5",
        ])
        for scenario in matrix["scenarios"]:
            self.assertEqual(scenario["duration_s"], 300)
            self.assertEqual(scenario["seeds"], [1, 2, 3, 4])
            self.assertIn("0", scenario["tune"])
            self.assertIn("navalAttackFleetSize", scenario["tune"]["0"])

        runs = playtest_run.expand(matrix)
        # Every scenario carries a tune table, so each gets its automatic control twin.
        arms = {}
        for run in runs:
            arms.setdefault(run.scenario, set()).add(run.arm)
        for sid in ids:
            self.assertEqual(arms[sid], {"tuned", "control"})
        self.assertEqual(len(runs), 3 * 4 * 2)

    def test_plan_writes_float_and_bool_values_toml_valid(self):
        out = Path(self.tmp) / "sweep2.toml"
        parser = ai_experiment.build_parser()
        args = parser.parse_args([
            "plan", "--knob", "solarOnDemand", "--values", "true,false",
            "--seeds", "1", "--out", str(out),
        ])
        rc = ai_experiment.cmd_plan(args)
        self.assertEqual(rc, 0)
        matrix = playtest_run.load_matrix(out)
        values = [s["tune"]["0"]["solarOnDemand"] for s in matrix["scenarios"]]
        self.assertEqual(values, [True, False])

    def test_unknown_knob_is_rejected(self):
        out = Path(self.tmp) / "bad.toml"
        parser = ai_experiment.build_parser()
        args = parser.parse_args([
            "plan", "--knob", "thisKnobDoesNotExist", "--values", "1,2", "--out", str(out),
        ])
        rc = ai_experiment.cmd_plan(args)
        self.assertEqual(rc, 1)
        self.assertFalse(out.exists())

    def test_known_knob_via_source_fallback(self):
        self.assertTrue(ai_experiment.validate_knob("navalAttackFleetSize", None))
        self.assertFalse(ai_experiment.validate_knob("notARealKnobAtAll", None))

    def test_plan_cli_subprocess(self):
        out = Path(self.tmp) / "cli.toml"
        proc = subprocess.run(
            [sys.executable, str(TOOLS_DIR / "ai-experiment.py"), "plan",
             "--knob", "navalAttackFleetSize", "--values", "1,2", "--out", str(out)],
            cwd=str(REPO_ROOT), capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertTrue(out.exists())
        self.assertIn("wrote", proc.stdout)


if __name__ == "__main__":
    unittest.main()
