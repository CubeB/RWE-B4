"""Unit tests for tools/ai-autotune.py -- no engine, no subprocess.

Every game outcome here comes from a fake play_fn: a plain Python function
that fabricates an AI-ARENA-RESULT-shaped record from the candidate's own
knob values, deterministically, so the tests exercise the search/aggregation/
resume machinery without ever invoking ai_arena.exe. Run with the MinGW
python on this machine (stdlib only, no pip install):

    /d/msys64/mingw64/bin/python.exe tools/test_ai_autotune.py -v
"""
import importlib.util
import os
import random
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))


def _load(name, filename):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, filename))
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod  # dataclasses resolves annotations via sys.modules[cls.__module__]
    spec.loader.exec_module(mod)
    return mod


autotune = _load("ai_autotune_mod", "ai-autotune.py")
arena_common = _load("arena_common_mod", "arena_common.py")


FIXTURE_SOURCE = '''
namespace rwe {
namespace {
constexpr IntKnob intKnobs[] = {
    {"attackArmySize", &AiTuningProfile::attackArmySize},
    {"retreatArmySize", &AiTuningProfile::retreatArmySize},
    {"labRaiderShare", &AiTuningProfile::labRaiderShare},
};
constexpr BoolKnob boolKnobs[] = {
    {"techLevelTwo", &AiTuningProfile::techLevelTwo},
};
constexpr FloatKnob floatKnobs[] = {
    {"outpostResponseStrength", &AiTuningProfile::outpostResponseStrength},
};
constexpr ScalarKnob scalarKnobs[] = {
    {"kiteRangeMargin", &AiTuningProfile::kiteRangeMargin},
};
}
}
'''


class KnobTableParsingTest(unittest.TestCase):
    def test_parses_all_four_tables(self):
        knobs = arena_common.parse_knob_tables(FIXTURE_SOURCE)
        self.assertEqual(knobs["attackArmySize"], "int")
        self.assertEqual(knobs["retreatArmySize"], "int")
        self.assertEqual(knobs["labRaiderShare"], "int")
        self.assertEqual(knobs["techLevelTwo"], "bool")
        self.assertEqual(knobs["outpostResponseStrength"], "float")
        self.assertEqual(knobs["kiteRangeMargin"], "scalar")
        self.assertEqual(len(knobs), 6)

    def test_result_line_parsing_matches_real_format(self):
        line = (
            "[2026-09-26 22:30:00.643] [info] AI-ARENA-RESULT ticks=3600 seconds=120"
            " | p0=ARM alive units=8 buildings=7 army=0 lost=0 metalIncome=6 types=ARMCOMx1;ARMMEXx3"
            " | p1=CORE dead units=0 buildings=0 army=0 lost=15 metalIncome=0 types=-"
            " | ended=decided"
        )
        r = arena_common.parse_arena_result_line(line)
        self.assertEqual(r.ticks, 3600)
        self.assertEqual(r.seconds, 120)
        self.assertEqual(r.ended, "decided")
        self.assertEqual(r.players[0].side, "ARM")
        self.assertTrue(r.players[0].alive)
        self.assertEqual(r.players[0].types, {"ARMCOM": 1, "ARMMEX": 3})
        self.assertFalse(r.players[1].alive)
        self.assertEqual(r.players[1].types, {})


class KnobValidationTest(unittest.TestCase):
    def setUp(self):
        self.known = arena_common.parse_knob_tables(FIXTURE_SOURCE)

    def test_valid_spec_passes(self):
        specs = [
            autotune.KnobSpec(name="attackArmySize", type="int", min=4, max=20),
            autotune.KnobSpec(name="labRaiderShare", type="int", min=0, max=4),
        ]
        autotune.validate_knob_spec(specs, self.known)  # must not raise

    def test_typo_knob_name_fails_fast(self):
        specs = [autotune.KnobSpec(name="atackArmySize", type="int", min=4, max=20)]
        with self.assertRaises(ValueError) as ctx:
            autotune.validate_knob_spec(specs, self.known)
        self.assertIn("atackArmySize", str(ctx.exception))

    def test_multiple_typos_all_named(self):
        specs = [
            autotune.KnobSpec(name="nope1", type="int", min=0, max=1),
            autotune.KnobSpec(name="nope2", type="int", min=0, max=1),
        ]
        with self.assertRaises(ValueError) as ctx:
            autotune.validate_knob_spec(specs, self.known)
        self.assertIn("nope1", str(ctx.exception))
        self.assertIn("nope2", str(ctx.exception))


class CandidateSamplingTest(unittest.TestCase):
    def setUp(self):
        self.specs = [
            autotune.KnobSpec(name="attackArmySize", type="int", min=4, max=20),
            autotune.KnobSpec(name="retreatArmySize", type="int", min=1, max=10),
            autotune.KnobSpec(name="labRaiderShare", type="int", min=0, max=4),
        ]

    def test_same_seed_is_byte_identical(self):
        a = autotune.generate_candidates(self.specs, 10, seed=42)
        b = autotune.generate_candidates(self.specs, 10, seed=42)
        self.assertEqual(a, b)

    def test_different_seed_differs(self):
        a = autotune.generate_candidates(self.specs, 10, seed=1)
        b = autotune.generate_candidates(self.specs, 10, seed=2)
        self.assertNotEqual(a, b)

    def test_values_within_range(self):
        candidates = autotune.generate_candidates(self.specs, 50, seed=7)
        for knobs in candidates:
            self.assertTrue(4 <= knobs["attackArmySize"] <= 20)
            self.assertTrue(1 <= knobs["retreatArmySize"] <= 10)
            self.assertTrue(0 <= knobs["labRaiderShare"] <= 4)

    def test_candidate_id_stable_and_order_independent(self):
        k1 = {"a": 1, "b": 2}
        k2 = {"b": 2, "a": 1}
        self.assertEqual(autotune.candidate_id(k1), autotune.candidate_id(k2))

    def test_candidate_id_differs_for_different_knobs(self):
        self.assertNotEqual(
            autotune.candidate_id({"a": 1}),
            autotune.candidate_id({"a": 2}),
        )


class WilsonIntervalTest(unittest.TestCase):
    def test_all_wins_interval_bounded_and_high(self):
        lo, hi = autotune.wilson_interval(10, 10)
        self.assertLessEqual(hi, 1.0)
        self.assertGreater(lo, 0.6)

    def test_half_wins_centered_near_half(self):
        lo, hi = autotune.wilson_interval(5, 10)
        self.assertLess(lo, 0.5)
        self.assertGreater(hi, 0.5)

    def test_zero_samples_is_maximally_uncertain(self):
        lo, hi = autotune.wilson_interval(0, 0)
        self.assertEqual((lo, hi), (0.0, 1.0))

    def test_more_samples_narrows_interval(self):
        lo1, hi1 = autotune.wilson_interval(7, 10)
        lo2, hi2 = autotune.wilson_interval(70, 100)
        self.assertLess(hi2 - lo2, hi1 - lo1)


def _make_result(players):
    """players: list of (side, status, units) tuples, seat order = index."""
    return {
        "ticks": 1000, "seconds": 900, "ended": "timeout",
        "players": {
            str(i): {"side": side, "status": status, "units": units, "buildings": 0,
                     "army": 0, "lost": 0, "metal_income": 0, "types": {}}
            for i, (side, status, units) in enumerate(players)
        },
    }


class FitnessMathTest(unittest.TestCase):
    def test_margin_uses_zero_for_dead_side(self):
        r = _make_result([("ARM", "alive", 50), ("CORE", "dead", 30)])
        self.assertEqual(autotune.player_margin(r["players"], 0, [1]), 50)

    def test_margin_generalises_to_many_opponents(self):
        r = _make_result([("ARM", "alive", 30), ("CORE", "alive", 10), ("CORE", "alive", 20)])
        # seat 0 vs mean(opponents) = mean(10,20) = 15
        self.assertEqual(autotune.player_margin(r["players"], 0, [1, 2]), 15)

    def test_fitness_sample_verdict_deadband(self):
        tuned = _make_result([("ARM", "alive", 60), ("CORE", "dead", 0)])
        control = _make_result([("ARM", "alive", 45), ("CORE", "dead", 0)])
        tuned["ended"] = "decided"
        control["ended"] = "decided"
        sample = autotune.make_fitness_sample(
            "mirror_arm", 1, 0,
            {"result": tuned}, {"result": control},
        )
        self.assertEqual(sample.margin_delta, 15)
        self.assertEqual(sample.verdict, "better")
        self.assertTrue(sample.tuned_decided)

    def test_aggregate_fitness_rewards_decided_rate(self):
        cid = "abc"
        knobs = {"attackArmySize": 10}
        # Two samples with identical margin delta, but one candidate's games
        # are all decided and the other's are all timeouts.
        decided_samples = [
            autotune.FitnessSample("s", i, 20, 10, 10, "same", True, False, True)
            for i in range(4)
        ]
        timeout_samples = [
            autotune.FitnessSample("s", i, 20, 10, 10, "same", False, False, False)
            for i in range(4)
        ]
        decided_score = autotune.aggregate_fitness(cid, knobs, decided_samples)
        timeout_score = autotune.aggregate_fitness(cid, knobs, timeout_samples)
        self.assertGreater(decided_score.fitness, timeout_score.fitness)

    def test_aggregate_fitness_penalises_wasted_efficiency(self):
        cid = "abc"
        knobs = {}
        wasteful = [
            autotune.FitnessSample("s", i, 20, 10, 10, "same", False, False, False, efficiency_delta=20.0)
            for i in range(4)
        ]
        frugal = [
            autotune.FitnessSample("s", i, 20, 10, 10, "same", False, False, False, efficiency_delta=0.0)
            for i in range(4)
        ]
        self.assertLess(
            autotune.aggregate_fitness(cid, knobs, wasteful).fitness,
            autotune.aggregate_fitness(cid, knobs, frugal).fitness,
        )


class PromotionTest(unittest.TestCase):
    def _score(self, cid, fitness, wilson_lo=0.5):
        s = autotune.CandidateScore(candidate_id=cid, knobs={})
        s.fitness = fitness
        s.wilson_lo = wilson_lo
        return s

    def test_keeps_top_n_by_fitness(self):
        scores = [self._score("a", 1.0), self._score("b", 5.0), self._score("c", 3.0)]
        kept = autotune.promote(scores, 2)
        self.assertEqual([s.candidate_id for s in kept], ["b", "c"])

    def test_ties_broken_by_wilson_lo_then_id(self):
        scores = [
            self._score("z", 5.0, wilson_lo=0.4),
            self._score("a", 5.0, wilson_lo=0.6),
            self._score("m", 5.0, wilson_lo=0.6),
        ]
        kept = autotune.promote(scores, 2)
        # equal fitness -> higher wilson_lo wins; equal wilson_lo -> lower id wins
        self.assertEqual([s.candidate_id for s in kept], ["a", "m"])

    def test_keep_n_larger_than_pool_returns_everything(self):
        scores = [self._score("a", 1.0)]
        self.assertEqual(len(autotune.promote(scores, 5)), 1)


class ResultStoreResumeTest(unittest.TestCase):
    def test_written_records_are_visible_to_a_fresh_store(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "results.jsonl")
            store1 = autotune.ResultStore(path)
            store1.append("k1", {"ok": True, "value": 1})
            store2 = autotune.ResultStore(path)
            self.assertTrue(store2.has("k1"))
            self.assertEqual(store2.get("k1")["value"], 1)

    def test_has_is_false_for_unknown_key(self):
        with tempfile.TemporaryDirectory() as d:
            store = autotune.ResultStore(os.path.join(d, "results.jsonl"))
            self.assertFalse(store.has("missing"))


class EvaluateRoundResumeTest(unittest.TestCase):
    """Exercises the orchestration layer with a fake play_fn, so the search's
    resume/caching behaviour is tested without any subprocess or engine."""

    def _fake_play_fn(self, calls):
        def play(task):
            calls.append(task)
            rng = random.Random((task.scenario, task.seed, task.arm, tuple(sorted((task.candidate or {}).items()))).__hash__())
            base_units = 40 + (sum(task.candidate.values()) if task.candidate else 0) % 20
            seat = task.tuned_seat if task.tuned_seat >= 0 else 0
            players = []
            for i, _side in enumerate(task.sides):
                units = base_units if i == seat else 30
                players.append((task.sides[i], "alive", units))
            result = _make_result(players)
            result["ended"] = "decided" if rng.random() < 0.5 else "timeout"
            return {
                "exit_code": 0, "wall_time": 0.01, "ok": True, "result": result,
                "scenario": task.scenario, "seed": task.seed, "seconds": task.seconds,
                "arm": task.arm, "tuned_seat": task.tuned_seat, "map": task.map_name,
                "sides": list(task.sides), "out_dir": task.out_dir, "knobs": task.candidate,
            }
        return play

    def test_control_games_are_shared_across_candidates(self):
        specs = [autotune.KnobSpec(name="attackArmySize", type="int", min=4, max=20)]
        candidates = [("c1", {"attackArmySize": 10}), ("c2", {"attackArmySize": 15})]
        with tempfile.TemporaryDirectory() as d:
            store = autotune.ResultStore(os.path.join(d, "results.jsonl"))
            calls = []
            play = self._fake_play_fn(calls)
            out_counter = {"n": 0}

            def make_out(scenario, cid, seed, arm):
                out_counter["n"] += 1
                return os.path.join(d, f"g{out_counter['n']}")

            scores = autotune.evaluate_round(
                candidates, [1, 2], 900, ["mirror_arm"], "Great Divide", "standard", "random",
                store, play, make_out, jobs=2,
            )
            self.assertEqual(len(scores), 2)
            control_calls = [c for c in calls if c.arm == "control"]
            tuned_calls = [c for c in calls if c.arm == "tuned"]
            # 2 seeds worth of controls, NOT one per candidate.
            self.assertEqual(len(control_calls), 2)
            # 2 candidates x 2 seeds of tuned games.
            self.assertEqual(len(tuned_calls), 4)

    def test_resuming_does_not_replay_completed_games(self):
        specs = [autotune.KnobSpec(name="attackArmySize", type="int", min=4, max=20)]
        candidates = [("c1", {"attackArmySize": 10})]
        with tempfile.TemporaryDirectory() as d:
            results_path = os.path.join(d, "results.jsonl")
            store = autotune.ResultStore(results_path)
            calls = []
            play = self._fake_play_fn(calls)

            def make_out(scenario, cid, seed, arm):
                return os.path.join(d, scenario, cid, f"{seed}-{arm}")

            autotune.evaluate_round(
                candidates, [1, 2], 900, ["mirror_arm"], "Great Divide", "standard", "random",
                store, play, make_out, jobs=2,
            )
            first_call_count = len(calls)
            self.assertGreater(first_call_count, 0)

            # Fresh store re-reading the same file, same candidates/seeds: a
            # restarted run must not replay any completed game.
            store2 = autotune.ResultStore(results_path)
            calls2 = []
            play2 = self._fake_play_fn(calls2)
            autotune.evaluate_round(
                candidates, [1, 2], 900, ["mirror_arm"], "Great Divide", "standard", "random",
                store2, play2, make_out, jobs=2,
            )
            self.assertEqual(len(calls2), 0)

    def test_new_seed_only_runs_the_new_games(self):
        candidates = [("c1", {"attackArmySize": 10})]
        with tempfile.TemporaryDirectory() as d:
            results_path = os.path.join(d, "results.jsonl")
            store = autotune.ResultStore(results_path)
            calls = []
            play = self._fake_play_fn(calls)

            def make_out(scenario, cid, seed, arm):
                return os.path.join(d, scenario, cid, f"{seed}-{arm}")

            autotune.evaluate_round(candidates, [1], 900, ["mirror_arm"], "Great Divide",
                                     "standard", "random", store, play, make_out, jobs=1)
            store2 = autotune.ResultStore(results_path)
            calls2 = []
            play2 = self._fake_play_fn(calls2)
            autotune.evaluate_round(candidates, [1, 2], 900, ["mirror_arm"], "Great Divide",
                                     "standard", "random", store2, play2, make_out, jobs=1)
            # Only seed 2's games (control + tuned) should have been played.
            self.assertEqual(len(calls2), 2)


if __name__ == "__main__":
    unittest.main()
