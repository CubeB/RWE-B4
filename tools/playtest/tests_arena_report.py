#!/usr/bin/env python3
"""Tests for tools/arena-report.py's "AI review" sections.

Run with ``python3 tools/playtest/tests_arena_report.py``. Stdlib only; every
fixture (CSV rows, event-log.jsonl lines) is built inline rather than read
from testdata, since the module under test is small enough that the fixtures
read better sitting next to the assertions that use them.

``arena-report.py`` lives at ``tools/arena-report.py`` -- a dash in the
filename, so it cannot be imported with a plain ``import`` statement.
``tools/tad-stalltime.py`` already solves this with
``importlib.util.spec_from_file_location``; the same trick is used below.
"""

import contextlib
import importlib.util
import io
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
MODULE_PATH = REPO_ROOT / "tools" / "arena-report.py"

_spec = importlib.util.spec_from_file_location("arena_report", str(MODULE_PATH))
ar = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ar)


CSV_HEADER = (
    "tick,seconds,player,side,status,metal,energy,maxMetal,maxEnergy,"
    "metalIncome,energyIncome,metalDemand,energyDemand,"
    "units,buildings,army,builders,unitsLost,buildingsLost,"
    "idleBuilders,factories,idleFactories,armyMetal,"
    "metalProduced,metalExcess,energyProduced,energyExcess,phase"
)

EVENTS_HEADER = (
    "player,unitType,category,isBuilding,startedTick,startedSeconds,"
    "completedTick,completedSeconds,diedTick,diedSeconds,"
    "x,z,enemiesNear,nearestEnemyType,friendlyArmyNear,friendlyTowersNear,"
    "deathCause,killerType,killerPlayer"
)


def csv_row(tick, player, side, seconds, metal_income=5.0, energy_income=20.0,
           metal_demand=4.0, energy_demand=18.0, phase="Opening"):
    return ("%d,%d,%d,%s,alive,500,500,1000,1000,%.1f,%.1f,%.1f,%.1f,"
            "3,2,1,1,0,0,0,1,0,20,10,0,50,0,%s" %
            (tick, seconds, player, side, metal_income, energy_income,
             metal_demand, energy_demand, phase))


def events_row(player, unit_type, category, is_building=0, started=0, completed=1,
              died=None, x=0, z=0):
    died_ticks = "%d,%d" % (died * 30, died) if died is not None else ","
    return ("%d,%s,%s,%d,%d,%d,%d,%d,%s,%d,%d,,,,,,,\n" %
            (player, unit_type, category, is_building, started * 30, started,
             completed * 30, completed, died_ticks, x, z)).rstrip("\n")


def make_run_dir(csv_lines, events_lines, event_log_lines=None, run_meta=None):
    """A temp directory laid out like ``ai_arena --out`` writes one:
    ``ai-arena.csv``, ``ai-arena-events.csv``, and optionally
    ``event-log.jsonl`` / ``run.json``."""
    d = Path(tempfile.mkdtemp(prefix="rwe-arena-report-test-"))
    (d / "ai-arena.csv").write_text(CSV_HEADER + "\n" + "\n".join(csv_lines) + "\n", encoding="utf-8")
    (d / "ai-arena-events.csv").write_text(EVENTS_HEADER + "\n" + "\n".join(events_lines) + "\n", encoding="utf-8")
    if event_log_lines is not None:
        (d / "event-log.jsonl").write_text("\n".join(event_log_lines) + "\n", encoding="utf-8")
    if run_meta is not None:
        (d / "run.json").write_text(json.dumps(run_meta), encoding="utf-8")
    return d


def ev(ev_name, player=0, secs=0.0, tick=None, **extra):
    e = {"schema": 1, "ev": ev_name, "secs": secs, "tick": tick if tick is not None else int(secs * 30),
        "detail": ev_name}
    if player is not None:
        e["player"] = player
    e.update(extra)
    return json.dumps(e)


class ArenaReportTestCase(unittest.TestCase):
    def setUp(self):
        self._dirs = []

    def tearDown(self):
        for d in self._dirs:
            shutil.rmtree(d, ignore_errors=True)

    def make_run(self, *args, **kwargs):
        d = make_run_dir(*args, **kwargs)
        self._dirs.append(d)
        return d


# --------------------------------------------------------------------------
# Event-log parsing.
# --------------------------------------------------------------------------

class EventLogParsingTests(ArenaReportTestCase):
    def test_tolerant_of_malformed_and_unknown_events(self):
        d = self.make_run([], [])
        log = d / "event-log.jsonl"
        log.write_text(
            "\n".join([
                ev("build_order", player=0, secs=1.0, subject="ARMMEX"),
                "{not json at all",
                "42",              # valid JSON, not an object
                "[1, 2, 3]",       # valid JSON, not an object
                "",                # blank line
                json.dumps({"schema": 1, "secs": 2.0}),  # object with no "ev"
                ev("some_future_event_type", player=1, secs=3.0, why="mystery"),
            ]) + "\n",
            encoding="utf-8",
        )
        events = ar.read_event_log(str(log))
        self.assertEqual(len(events), 2)
        self.assertEqual(events[0]["ev"], "build_order")
        self.assertEqual(events[1]["ev"], "some_future_event_type")

    def test_missing_file_returns_empty(self):
        self.assertEqual(ar.read_event_log("/no/such/file/event-log.jsonl"), [])


# --------------------------------------------------------------------------
# Area derivation and refusal detection.
# --------------------------------------------------------------------------

class AreaDerivationTests(unittest.TestCase):
    def test_prefixes(self):
        cases = {
            "build_order": "build",
            "build_refusal": "build",
            "factory_start": "factory",
            "factory_hold_t2": "factory",
            "army_reinforce": "army",
            "navy_sail": "navy",
            "scout_assigned": "scout",
            "transport_refusal": "transport",
        }
        for ev_name, area in cases.items():
            self.assertEqual(ar.derive_area(ev_name), area, ev_name)

    def test_commander_events_are_their_own_lane(self):
        self.assertEqual(ar.derive_area("army_commander_danger"), "commander")
        self.assertEqual(ar.derive_area("army_commander_dgun"), "commander")

    def test_unknown_prefix_falls_back_to_other(self):
        self.assertEqual(ar.derive_area("ai_land_route"), "other")
        self.assertEqual(ar.derive_area("something_odd"), "other")
        self.assertEqual(ar.derive_area(None), "other")
        self.assertEqual(ar.derive_area(""), "other")


class RefusalDetectionTests(unittest.TestCase):
    def test_named_families_are_refusals(self):
        for name in ("build_refusal", "transport_refusal", "build_unaffordable",
                     "build_not_worth", "build_site_contested", "build_site_search"):
            self.assertTrue(ar.is_refusal(name), name)

    def test_ordinary_decisions_are_not_refusals(self):
        for name in ("build_order", "factory_start", "scout_assigned", "army_reinforce"):
            self.assertFalse(ar.is_refusal(name), name)

    def test_none_and_empty_are_not_refusals(self):
        self.assertFalse(ar.is_refusal(None))
        self.assertFalse(ar.is_refusal(""))


class DecisionEventFilterTests(unittest.TestCase):
    def test_status_and_telemetry_events_are_excluded(self):
        for name in ("ai_status", "ai_transition", "ai_perf", "ai_reachability", "path_stats", "unit_death"):
            self.assertFalse(ar.is_decision_event({"ev": name, "player": 0}), name)

    def test_decision_events_with_a_player_are_included(self):
        self.assertTrue(ar.is_decision_event({"ev": "build_order", "player": 0}))

    def test_decision_events_without_a_resolvable_player_are_excluded(self):
        self.assertFalse(ar.is_decision_event({"ev": "build_site_search"}))
        self.assertFalse(ar.is_decision_event({"ev": "build_order", "player": "not-a-number"}))


# --------------------------------------------------------------------------
# Fight clustering.
# --------------------------------------------------------------------------

def death(player, secs, x, z, subject="ARMPW", category="army"):
    return {
        "player": player, "secs": secs, "x": float(x), "z": float(z), "subject": subject,
        "cause": "weapon", "killer_type": "COR" + subject[3:], "killer_player": 1 - player,
        "category": category, "commander": False, "cost": None,
        "size": ar.death_marker_size(category, False, None),
    }


class FightClusteringTests(unittest.TestCase):
    def test_close_deaths_become_one_fight(self):
        deaths = [
            death(0, 100.0, 0, 0),
            death(1, 105.0, 50, 30),
            death(0, 112.0, -40, 10),
        ]
        fights = ar.cluster_fights(deaths, players=[0, 1])
        self.assertEqual(len(fights), 1)
        self.assertEqual(fights[0]["deaths"], 3)
        self.assertAlmostEqual(fights[0]["start"], 100.0)
        self.assertAlmostEqual(fights[0]["end"], 112.0)

    def test_far_apart_deaths_are_two_fights(self):
        deaths = [
            death(0, 100.0, 0, 0),
            death(1, 105.0, 40, 20),
            death(0, 110.0, 5000, 5000),
            death(1, 115.0, 5040, 5020),
        ]
        fights = ar.cluster_fights(deaths, players=[0, 1])
        self.assertEqual(len(fights), 2)
        self.assertEqual(sorted(f["deaths"] for f in fights), [2, 2])

    def test_time_gap_over_threshold_starts_a_new_fight(self):
        deaths = [
            death(0, 100.0, 0, 0),
            death(1, 105.0, 30, 10),
            # same place, but 25s after the cluster's last death (> 20s gap)
            death(0, 130.0, 10, 5),
            death(1, 135.0, 20, 15),
        ]
        fights = ar.cluster_fights(deaths, players=[0, 1])
        self.assertEqual(len(fights), 2)

    def test_continued_fight_within_gap_stays_one(self):
        deaths = [
            death(0, 100.0, 0, 0),
            death(1, 105.0, 30, 10),
            # 19s after the cluster's last death: still continues it
            death(0, 124.0, 10, 5),
        ]
        fights = ar.cluster_fights(deaths, players=[0, 1])
        self.assertEqual(len(fights), 1)
        self.assertEqual(fights[0]["deaths"], 3)

    def test_single_death_clusters_are_not_fights(self):
        deaths = [death(0, 100.0, 0, 0), death(1, 500.0, 9000, 9000)]
        fights = ar.cluster_fights(deaths, players=[0, 1])
        self.assertEqual(fights, [])

    def test_winner_is_the_side_that_lost_less_value(self):
        deaths = [
            death(0, 100.0, 0, 0, category="army"),
            death(0, 101.0, 10, 10, category="army"),
            death(1, 102.0, 20, 20, category="scout"),
        ]
        fights = ar.cluster_fights(deaths, players=[0, 1])
        self.assertEqual(len(fights), 1)
        self.assertEqual(fights[0]["winner"], 1)

    def test_one_sided_wipeout_gives_the_untouched_side_the_win(self):
        # Player 1 loses nobody in this cluster; player 0's losses alone
        # should still resolve to "player 1 lost less" because `players`
        # names every side in the game, not just the ones who died here.
        deaths = [death(0, 100.0, 0, 0), death(0, 105.0, 10, 10)]
        fights = ar.cluster_fights(deaths, players=[0, 1])
        self.assertEqual(len(fights), 1)
        self.assertEqual(fights[0]["winner"], 1)


# --------------------------------------------------------------------------
# Moments ordering.
# --------------------------------------------------------------------------

class MomentsTests(unittest.TestCase):
    def test_chronological_and_end_is_last(self):
        log_events = [
            json.loads(ev("ai_status", player=0, secs=10.0, known_enemies=1, idle_builders=0,
                          metal_stalled=False, energy_stalled=False, phase="Opening")),
            json.loads(ev("ai_status", player=0, secs=40.0, known_enemies=1, idle_builders=0,
                          metal_stalled=True, energy_stalled=False, phase="Opening")),
            json.loads(ev("army_commander_danger", player=1, secs=25.0, x=5, z=5)),
        ]
        deaths = []
        fights = []
        moments = ar.build_moments(log_events, deaths, fights, [0, 1], seconds_max=100.0, run_meta=None)
        secs = [m["secs"] for m in moments]
        self.assertEqual(secs, sorted(secs))
        self.assertTrue(moments[-1].get("_end"))
        self.assertAlmostEqual(moments[-1]["secs"], 100.0)

    def test_end_mentions_run_meta(self):
        moments = ar.build_moments([], [], [], [0, 1], seconds_max=50.0,
                                   run_meta={"ended": "elimination", "winner": 0})
        self.assertIn("elimination", moments[-1]["text"])
        self.assertIn("winner", moments[-1]["text"])

    def test_first_extractor_and_factory_losses_are_reported_once(self):
        deaths = [
            death(0, 10.0, 0, 0, subject="ARMMEX", category="economy"),
            death(0, 20.0, 0, 0, subject="ARMMEX", category="economy"),  # second loss: not repeated
            death(1, 15.0, 0, 0, subject="ARMLAB", category="factory"),
        ]
        moments = ar.build_moments([], deaths, [], [0, 1], seconds_max=100.0, run_meta=None)
        texts = [m["text"] for m in moments]
        extractor_mentions = [t for t in texts if "extractor" in t and "player 0" in t]
        self.assertEqual(len(extractor_mentions), 1)
        self.assertTrue(any("factory" in t and "player 1" in t for t in texts))


# --------------------------------------------------------------------------
# HTML section presence.
# --------------------------------------------------------------------------

class HtmlSectionTests(ArenaReportTestCase):
    def _log_lines(self):
        return [
            ev("ai_transition", player=0, secs=0.0, kind="phase", on=True, **{"from": None, "to": "Opening"}),
            ev("ai_status", player=0, secs=30.0, phase="Opening", metal_stalled=False, energy_stalled=False,
               idle_builders=0, known_enemies=0, army=0),
            ev("build_order", player=0, secs=1.0, subject="ARMMEX", why="issued", x=10, z=10),
            ev("build_refusal", player=0, secs=5.0, subject="ARMMEX", why="no_patch"),
            ev("transport_refusal", player=1, secs=6.0, why="no_transport"),
            ev("army_commander_danger", player=0, secs=8.0, x=1, z=1, why="enemy_close"),
            ev("unit_death", player=1, secs=12.0, subject="ARMPW", x=50, z=60, cause="weapon",
               killer_type="CORPW", killer_player=0),
            ev("unit_death", player=1, secs=13.0, subject="ARMPW", x=55, z=62, cause="weapon",
               killer_type="CORPW", killer_player=0),
        ]

    def test_sections_present_when_event_log_exists(self):
        d = self.make_run(
            [csv_row(300, 0, "ARM", 10), csv_row(300, 1, "CORE", 10),
             csv_row(600, 0, "ARM", 20), csv_row(600, 1, "CORE", 20)],
            [events_row(0, "ARMCOM", "commander", started=0, completed=0),
             events_row(1, "CORCOM", "commander", started=0, completed=0),
             events_row(0, "ARMMEX", "economy", started=1, completed=10),
             events_row(1, "ARMPW", "army", started=1, completed=5, died=13, x=55, z=62)],
            event_log_lines=self._log_lines(),
            run_meta={"ended": "timeout", "winner": None},
        )
        out = d / "out.html"
        ar.build(str(d / "ai-arena.csv"), str(out))
        html = out.read_text(encoding="utf-8")
        self.assertIn('class="ai-review"', html)
        self.assertIn("Decision timeline", html)
        self.assertIn("Why-tags", html)
        self.assertIn("Death map", html)
        self.assertIn("Fights", html)
        self.assertIn("Moments", html)
        self.assertIn("death-dot", html)

    def test_sections_absent_when_no_event_log(self):
        d = self.make_run(
            [csv_row(300, 0, "ARM", 10), csv_row(300, 1, "CORE", 10)],
            [events_row(0, "ARMCOM", "commander", started=0, completed=0),
             events_row(1, "CORCOM", "commander", started=0, completed=0)],
            event_log_lines=None,
        )
        out = d / "out.html"
        ar.build(str(d / "ai-arena.csv"), str(out))
        html = out.read_text(encoding="utf-8")
        self.assertNotIn('class="ai-review"', html)
        self.assertNotIn("Decision timeline", html)
        self.assertNotIn("Death map", html)
        self.assertNotIn("@@REVIEW@@", html)

    def test_run_flag_finds_ai_arena_csv_in_directory(self):
        d = self.make_run(
            [csv_row(300, 0, "ARM", 10), csv_row(300, 1, "CORE", 10)],
            [events_row(0, "ARMCOM", "commander", started=0, completed=0),
             events_row(1, "CORCOM", "commander", started=0, completed=0)],
            event_log_lines=self._log_lines(),
        )
        out = d / "out.html"
        rc = 0
        old_argv = sys.argv
        try:
            sys.argv = ["arena-report.py", "--run", str(d), "--out", str(out)]
            ar.main()
        finally:
            sys.argv = old_argv
        self.assertTrue(out.exists())
        html = out.read_text(encoding="utf-8")
        self.assertIn('class="ai-review"', html)


# --------------------------------------------------------------------------
# --text output.
# --------------------------------------------------------------------------

class TextOutputTests(ArenaReportTestCase):
    def test_text_report_has_all_three_sections(self):
        d = self.make_run(
            [csv_row(300, 0, "ARM", 10), csv_row(300, 1, "CORE", 10)],
            [events_row(0, "ARMCOM", "commander", started=0, completed=0),
             events_row(1, "CORCOM", "commander", started=0, completed=0),
             events_row(1, "ARMPW", "army", started=1, completed=5, died=13, x=55, z=62)],
            event_log_lines=[
                ev("build_refusal", player=0, secs=5.0, subject="ARMMEX", why="no_patch"),
                ev("build_refusal", player=0, secs=6.0, subject="ARMMEX", why="no_patch"),
                ev("unit_death", player=1, secs=12.0, subject="ARMPW", x=50, z=60, cause="weapon"),
                ev("unit_death", player=1, secs=13.0, subject="ARMPW", x=55, z=62, cause="weapon"),
            ],
        )
        text = ar.text_report(str(d / "ai-arena.csv"))
        self.assertIn("=== why-tags ===", text)
        self.assertIn("=== fights ===", text)
        self.assertIn("=== moments ===", text)
        self.assertIn("no_patch", text)
        self.assertIn("the game ends", text)

    def test_text_report_without_event_log_says_so(self):
        d = self.make_run(
            [csv_row(300, 0, "ARM", 10), csv_row(300, 1, "CORE", 10)],
            [events_row(0, "ARMCOM", "commander", started=0, completed=0),
             events_row(1, "CORCOM", "commander", started=0, completed=0)],
            event_log_lines=None,
        )
        text = ar.text_report(str(d / "ai-arena.csv"))
        self.assertIn("nothing to show", text)

    def test_main_text_flag_prints_and_skips_html_without_out(self):
        d = self.make_run(
            [csv_row(300, 0, "ARM", 10), csv_row(300, 1, "CORE", 10)],
            [events_row(0, "ARMCOM", "commander", started=0, completed=0),
             events_row(1, "CORCOM", "commander", started=0, completed=0)],
            event_log_lines=[ev("build_refusal", player=0, secs=5.0, subject="ARMMEX", why="no_patch")],
        )
        old_argv = sys.argv
        buf = io.StringIO()
        try:
            sys.argv = ["arena-report.py", str(d / "ai-arena.csv"), "--text"]
            with contextlib.redirect_stdout(buf):
                ar.main()
        finally:
            sys.argv = old_argv
        self.assertIn("=== why-tags ===", buf.getvalue())
        self.assertFalse((d / "ai-arena.html").exists())

    def test_main_text_and_out_writes_both(self):
        d = self.make_run(
            [csv_row(300, 0, "ARM", 10), csv_row(300, 1, "CORE", 10)],
            [events_row(0, "ARMCOM", "commander", started=0, completed=0),
             events_row(1, "CORCOM", "commander", started=0, completed=0)],
            event_log_lines=[ev("build_refusal", player=0, secs=5.0, subject="ARMMEX", why="no_patch")],
        )
        out = d / "out.html"
        old_argv = sys.argv
        buf = io.StringIO()
        try:
            sys.argv = ["arena-report.py", str(d / "ai-arena.csv"), "--text", "--out", str(out)]
            with contextlib.redirect_stdout(buf):
                ar.main()
        finally:
            sys.argv = old_argv
        self.assertIn("=== why-tags ===", buf.getvalue())
        self.assertTrue(out.exists())


# --------------------------------------------------------------------------
# Lane thinning (dense-lane aggregation).
# --------------------------------------------------------------------------

class ThinLaneTests(unittest.TestCase):
    def test_small_lane_is_unchanged(self):
        markers = [{"secs": float(i), "refusal": False} for i in range(5)]
        thinned = ar.thin_lane(markers, seconds_max=100.0, width=50, max_markers=50)
        self.assertEqual(len(thinned), 5)
        self.assertTrue(all(m["count"] == 1 for m in thinned))

    def test_dense_lane_is_bucketed_and_counts_are_preserved(self):
        markers = [{"secs": float(i) * 0.01, "refusal": False} for i in range(2000)]
        thinned = ar.thin_lane(markers, seconds_max=20.0, width=50, max_markers=50)
        self.assertLessEqual(len(thinned), 50)
        self.assertEqual(sum(m["count"] for m in thinned), 2000)


if __name__ == "__main__":
    unittest.main()
