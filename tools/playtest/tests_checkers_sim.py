#!/usr/bin/env python3
"""Tests for the sim-facing playtest checkers: production, unitdeath, invariant.

Run with ``python3 tools/playtest/tests_checkers_sim.py``. Everything is stdlib;
every input is a synthetic fixture under ``testdata/`` with the engine's real
CSV headers, so no build and no real ai_arena run is needed.
"""

import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import run as runner  # noqa: E402
from checkers import invariant, production, unitdeath  # noqa: E402
from checkers.economy import Finding  # noqa: E402

TESTDATA = HERE / "testdata"

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
    """Build a well-formed arena CSV from dicts; missing columns default to 0."""
    with open(path, "w") as fh:
        fh.write(",".join(CSV_HEADER) + "\n")
        for sample in samples:
            fh.write(",".join(str(sample.get(col, 0)) for col in CSV_HEADER) + "\n")


def sample(seconds, player=0, **overrides):
    row = {
        "tick": seconds * 10,
        "seconds": seconds,
        "player": player,
        "side": "ARM" if player == 0 else "CORE",
        "status": "alive",
        "metal": 500.0,
        "energy": 3000.0,
        "maxMetal": 1000.0,
        "maxEnergy": 5000.0,
        "metalIncome": 2.0,
        "metalDemand": 1.0,
        "units": 10,
        "buildings": 5,
        "army": 4,
        "builders": 2,
        "factories": 2,
        "idleFactories": 0,
        "phase": "mid",
    }
    row.update(overrides)
    return row


def write_events(path, events):
    with open(path, "w") as fh:
        fh.write(",".join(EVENTS_HEADER) + "\n")
        for event in events:
            fh.write(
                ",".join("" if event.get(col) is None else str(event.get(col)) for col in EVENTS_HEADER)
                + "\n"
            )


def event(player=0, unitType="ARMX", category="army", **overrides):
    row = {
        "player": player,
        "unitType": unitType,
        "category": category,
        "isBuilding": 0,
        "startedTick": 0,
        "startedSeconds": 0,
        "completedTick": 100,
        "completedSeconds": 10,
        "diedTick": None,
        "diedSeconds": None,
        "x": 100,
        "z": 200,
        "enemiesNear": None,
        "nearestEnemyType": None,
        "friendlyArmyNear": None,
        "friendlyTowersNear": None,
    }
    row.update(overrides)
    return row


def result_line(ended, units):
    parts = ["AI-ARENA-RESULT ticks=9000 seconds=900"]
    for player, side in ((0, "ARM"), (1, "CORE")):
        parts.append(
            f"p{player}={side} alive units={units.get(player, 0)} "
            f"buildings=1 army=0 lost=0 metalIncome=0 types=ARMCOMx1"
        )
    if ended is not None:
        parts.append(f"ended={ended}")
    return " | ".join(parts) + "\n"


def rules(findings, prefix):
    return [f for f in findings if f.summary.startswith(prefix)]


class SimCheckerTestCase(unittest.TestCase):
    """Run-dir layout is ``<root>/<scenario>/seed1-control/``, as the runner makes it."""

    def make_dir(self) -> Path:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        run_dir = Path(tmp.name) / "sim" / "seed1-control"
        run_dir.mkdir(parents=True)
        return run_dir

    def copy(self, run_dir: Path, target: str, fixture: str) -> None:
        shutil.copyfile(TESTDATA / fixture, run_dir / target)

    def write(self, run_dir: Path, name: str, text: str) -> None:
        (run_dir / name).write_text(text)


class ProductionTests(SimCheckerTestCase):
    def test_p1_hit_is_suspicious(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "prod-idle-factories.csv")
        found = rules(production.check(run_dir), "P1:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].severity, "suspicious")
        self.assertGreater(found[0].evidence["share"], 0.5)
        self.assertGreater(found[0].evidence["samples"], 0)
        self.assertGreaterEqual(len(found[0].evidence["examples"]), 1)
        self.assertLessEqual(len(found[0].evidence["examples"]), 3)
        self.assertIn("player", found[0].evidence["examples"][0])
        self.assertIsInstance(found[0], Finding)

    def test_p1_clean(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "prod-healthy.csv")
        self.assertEqual(rules(production.check(run_dir), "P1:"), [])

    def test_p1_threshold_from_context_suppresses(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "prod-idle-factories.csv")
        context = runner.RunContext(
            "sim", 1, "control",
            thresholds={"production": {"p1_after_seconds": 600, "p1_share": 2.0, "p1_income_floor": 0.5}},
        )
        self.assertEqual(rules(production.check(run_dir, context), "P1:"), [])

    def test_p2_hit_without_events(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "prod-builder-collapse.csv")
        found = rules(production.check(run_dir), "P2:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].severity, "suspicious")
        self.assertGreaterEqual(found[0].evidence["samples"], 3)
        self.assertFalse(found[0].evidence["builder_deaths_checked"])

    def test_p2_clean_when_builder_died_in_window(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "prod-builder-collapse.csv")
        self.copy(run_dir, "ai-arena-events.csv", "prod-builder-death-events.csv")
        self.assertEqual(rules(production.check(run_dir), "P2:"), [])

    def test_p2_clean_when_builders_present(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "prod-healthy.csv")
        self.assertEqual(rules(production.check(run_dir), "P2:"), [])

    def test_p3_clean(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "prod-healthy.csv")
        self.copy(run_dir, "ai-arena-events.csv", "prod-healthy-events.csv")
        self.assertEqual(rules(production.check(run_dir), "P3:"), [])

    def test_p3_missing_economy(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "prod-healthy.csv")
        self.copy(run_dir, "ai-arena-events.csv", "prod-no-economy-events.csv")
        found = rules(production.check(run_dir), "P3:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].evidence["milestone"], "economy")
        self.assertEqual(found[0].severity, "suspicious")

    def test_p3_missing_factory(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "prod-healthy.csv")
        self.copy(run_dir, "ai-arena-events.csv", "prod-no-factory-events.csv")
        found = rules(production.check(run_dir), "P3:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].evidence["milestone"], "factory")

    def test_p3_missing_both(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "prod-healthy.csv")
        self.copy(run_dir, "ai-arena-events.csv", "prod-no-milestones-events.csv")
        found = rules(production.check(run_dir), "P3:")
        self.assertEqual({f.evidence["milestone"] for f in found}, {"economy", "factory"})

    def test_p3_skipped_for_short_run(self):
        run_dir = self.make_dir()
        rows = []
        for seconds in range(0, 601, 30):
            for player in (0, 1):
                rows.append(sample(seconds, player))
        write_csv(run_dir / "ai-arena.csv", rows)
        write_events(
            run_dir / "ai-arena-events.csv",
            [event(0, "ARMPW", "army", completedTick=100, completedSeconds=10)],
        )
        self.assertEqual(rules(production.check(run_dir), "P3:"), [])

    def test_p3_duration_from_run_json(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "prod-no-milestones-events.csv")
        self.write(run_dir, "run.json", json.dumps({"schema": 1, "durationSeconds": 900, "simTicks": 13500}))
        found = rules(production.check(run_dir), "P3:")
        self.assertEqual({f.evidence["milestone"] for f in found}, {"economy", "factory"})

    def test_missing_csv_returns_empty(self):
        self.assertEqual(production.check(self.make_dir()), [])

    def test_malformed_csv_does_not_crash(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "prod-malformed.csv")
        self.copy(run_dir, "ai-arena-events.csv", "prod-malformed-events.csv")
        findings = production.check(run_dir)
        self.assertIsInstance(findings, list)
        for finding in findings:
            self.assertIsInstance(finding, Finding)


class UnitDeathTests(SimCheckerTestCase):
    def test_d1_two_deaths_likely_bug(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "death-economy-alone.csv")
        found = rules(unitdeath.check(run_dir), "D1:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].severity, "likely-bug")
        self.assertEqual(found[0].evidence["count"], 2)
        death = found[0].evidence["deaths"][0]
        for key in ("player", "unitType", "diedSeconds", "x", "z", "nearestEnemyType"):
            self.assertIn(key, death)
        self.assertIsInstance(found[0], Finding)

    def test_d1_one_death_suspicious(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "death-economy-alone-one.csv")
        found = rules(unitdeath.check(run_dir), "D1:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].severity, "suspicious")

    def test_d1_clean(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "death-clean.csv")
        self.assertEqual(rules(unitdeath.check(run_dir), "D1:"), [])

    def test_d2_five_detached_suspicious(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "death-detached-army.csv")
        found = rules(unitdeath.check(run_dir), "D2:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].severity, "suspicious")
        self.assertEqual(found[0].evidence["count"], 5)
        self.assertEqual(len(found[0].evidence["deaths"]), 3)

    def test_d2_clean(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "death-clean.csv")
        self.assertEqual(rules(unitdeath.check(run_dir), "D2:"), [])

    def test_d3_early_scouts_suspicious(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "death-early-scouts.csv")
        found = rules(unitdeath.check(run_dir), "D3:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].severity, "suspicious")
        self.assertEqual(found[0].evidence["count"], 2)

    def test_d3_clean(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "death-clean.csv")
        self.assertEqual(rules(unitdeath.check(run_dir), "D3:"), [])

    def test_missing_events_returns_empty(self):
        self.assertEqual(unitdeath.check(self.make_dir()), [])

    def test_malformed_events_does_not_crash(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "death-malformed.csv")
        findings = unitdeath.check(run_dir)
        self.assertIsInstance(findings, list)
        for finding in findings:
            self.assertIsInstance(finding, Finding)


class InvariantTests(SimCheckerTestCase):
    def test_negative_resources_likely_bug(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "inv-negative.csv")
        found = rules(invariant.check(run_dir), "I1:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].severity, "likely-bug")
        self.assertEqual(found[0].evidence["count"], 2)
        self.assertIsInstance(found[0], Finding)

    def test_negative_clean(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "inv-healthy.csv")
        self.assertEqual(rules(invariant.check(run_dir), "I1:"), [])

    def test_result_units_match_clean(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "inv-events.csv")
        self.write(run_dir, "game.log", result_line("decided", {0: 2, 1: 1}))
        self.assertEqual(rules(invariant.check(run_dir), "I2:"), [])

    def test_result_units_mismatch_likely_bug(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "inv-events.csv")
        self.write(run_dir, "game.log", result_line("decided", {0: 2, 1: 2}))
        found = rules(invariant.check(run_dir), "I2:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].severity, "likely-bug")
        mismatch = found[0].evidence["mismatches"][0]
        self.assertEqual(mismatch["player"], 1)
        self.assertEqual(mismatch["result_line_units"], 2)
        self.assertEqual(mismatch["events_csv_units"], 1)

    def test_commander_death_timeout_suspicious(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "inv-commander-dead-events.csv")
        self.write(run_dir, "game.log", result_line("timeout", {0: 0, 1: 1}))
        found = rules(invariant.check(run_dir), "I3:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].severity, "suspicious")

    def test_commander_death_absent_result_suspicious(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "inv-commander-dead-events.csv")
        found = rules(invariant.check(run_dir), "I3:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].severity, "suspicious")

    def test_commander_death_decided_clean(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "inv-commander-dead-events.csv")
        self.write(run_dir, "game.log", result_line("decided", {0: 0, 1: 1}))
        self.assertEqual(rules(invariant.check(run_dir), "I3:"), [])

    def test_commander_death_timeout_multiplayer_run_json_clean(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "inv-commander-dead-events.csv")
        self.write(run_dir, "game.log", result_line("timeout", {0: 0, 1: 1, 2: 1, 3: 1}))
        self.write(run_dir, "run.json", json.dumps({
            "schema": 1,
            "players": [{"index": i} for i in range(4)],
        }))
        self.assertEqual(rules(invariant.check(run_dir), "I3:"), [])

    def test_commander_death_timeout_multiplayer_events_fallback_clean(self):
        run_dir = self.make_dir()
        write_events(run_dir / "ai-arena-events.csv", [
            event(0, "ARMCOM", "commander", diedTick=4000, diedSeconds=400,
                  enemiesNear=2, nearestEnemyType="CORCOM"),
            event(1, "COREMEX", "economy"),
            event(2, "COREMEX", "economy"),
        ])
        self.write(run_dir, "game.log", result_line("timeout", {0: 0, 1: 1}))
        self.assertEqual(rules(invariant.check(run_dir), "I3:"), [])

    def test_commander_death_timeout_two_player_run_json_still_fires(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena-events.csv", "inv-commander-dead-events.csv")
        self.write(run_dir, "game.log", result_line("timeout", {0: 0, 1: 1}))
        self.write(run_dir, "run.json", json.dumps({
            "schema": 1,
            "players": [{"index": 0}, {"index": 1}],
        }))
        found = rules(invariant.check(run_dir), "I3:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].severity, "suspicious")

    def test_run_json_zero_suspicious(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "run.json", "inv-run-zero.json")
        found = rules(invariant.check(run_dir), "I4:")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].severity, "suspicious")

    def test_run_json_healthy_clean(self):
        run_dir = self.make_dir()
        self.write(run_dir, "run.json", json.dumps({"schema": 1, "durationSeconds": 900, "simTicks": 13500}))
        self.assertEqual(rules(invariant.check(run_dir), "I4:"), [])

    def test_missing_files_returns_empty(self):
        self.assertEqual(invariant.check(self.make_dir()), [])

    def test_malformed_inputs_do_not_crash(self):
        run_dir = self.make_dir()
        self.copy(run_dir, "ai-arena.csv", "inv-malformed.csv")
        self.copy(run_dir, "ai-arena-events.csv", "inv-malformed-events.csv")
        self.write(run_dir, "game.log", "AI-ARENA-RESULT ticks=0 seconds=0 | p0=ARM alive\n")
        self.write(run_dir, "run.json", "{not json")
        findings = invariant.check(run_dir)
        self.assertIsInstance(findings, list)
        for finding in findings:
            self.assertIsInstance(finding, Finding)


if __name__ == "__main__":
    unittest.main(verbosity=2)
