#!/usr/bin/env python3
"""Tests for the death-evidence enrichment (design §4 Phase 2): the appended
``deathCause``/``killerType``/``killerPlayer`` columns ride in the unit-death
checker's evidence when present, and a death they explain is not reported as
died-alone.

A death with an attributed killer is not a D1 hit: ``enemiesNear`` counts only
``canAttack`` units, so the enemy builder or nanoframe that killed the
extractor is invisible to it. The columns still ride along in D2/D3 evidence.

Run with ``python3 tools/playtest/tests_death_evidence.py``. Stdlib only; every
input is a synthetic fixture under ``testdata/``.
"""

import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from checkers import unitdeath  # noqa: E402

TESTDATA = HERE / "testdata"


def rules(findings, marker):
    return [f for f in findings if marker in f.summary]


def finding(run_dir, marker):
    found = rules(unitdeath.check(run_dir), marker)
    assert len(found) == 1, f"expected one {marker} finding, got {found}"
    return found[0]


def run_dir_with(tmp, name):
    run_dir = Path(tmp) / "standard-arms-coast" / "seed1-control"
    run_dir.mkdir(parents=True)
    shutil.copyfile(TESTDATA / name, run_dir / "ai-arena-events.csv")
    return run_dir


class DeathEvidenceTests(unittest.TestCase):
    def test_d1_skips_a_death_with_a_killer(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = run_dir_with(tmp, "ev2-events-killed.csv")
            self.assertEqual(rules(unitdeath.check(run_dir), "D1:"), [])

    def test_covered_deaths_stay_clean(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = run_dir_with(tmp, "ev2-events-covered-clean.csv")
            self.assertEqual(unitdeath.check(run_dir), [])

    def test_old_header_keeps_behaviour_without_killer_keys(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = run_dir_with(tmp, "death-economy-alone.csv")
            found = finding(run_dir, "D1:")
            self.assertNotIn("killerType", found.evidence["deaths"][0])
            self.assertNotIn("deathCause", found.evidence["deaths"][0])

    def test_d3_carries_killer_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = run_dir_with(tmp, "ev3-events-scouts-killed.csv")
            found = finding(run_dir, "D3:")
            death = found.evidence["deaths"][0]
            self.assertEqual(death["killerType"], "ARMPW")
            self.assertEqual(death["killerPlayer"], "0")
            self.assertEqual(death["deathCause"], "weapon")

    def test_truncated_row_never_crash_and_full_rows_still_score(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = Path(tmp) / "standard-arms-coast" / "seed1-control"
            run_dir.mkdir(parents=True)
            text = (TESTDATA / "death-economy-alone.csv").read_text()
            truncated = text.splitlines(True)[:-1] + ["0,ARMSOLAR,economy\n"]
            (run_dir / "ai-arena-events.csv").write_text("".join(truncated))
            try:
                found = finding(run_dir, "D1:")
            except (KeyError, ValueError, IndexError):
                self.fail("a short row crashed the checker")
            self.assertEqual(found.evidence["count"], 2)
            for marker in ("D2:", "D3:"):
                self.assertEqual(rules(unitdeath.check(run_dir), marker), [])


if __name__ == "__main__":
    unittest.main()
