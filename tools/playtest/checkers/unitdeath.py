"""Unit-death checker (design §3.3): extractors dying with no enemy within the
death scan radius (D1), army dying detached from its own cover (D2), scouts
dying early and alone (D3).

The engine's events CSV may carry three appended columns — ``deathCause``,
``killerType``, ``killerPlayer`` — from design §4 Phase 2. When present they
ride along in each rule's evidence so a scan can say what actually killed the
unit. D1 also uses them to set aside deaths that are already explained: an
attributed killer, or a cause that is decay or a game-end wipe rather than a
kill. The other rules ignore them.

Thresholds come from ``thresholds.toml`` beside the checkers. TOML, not YAML:
stdlib only, no PyYAML on the machines that run this.
"""

from __future__ import annotations

import csv
import tomllib
from pathlib import Path

from .economy import Finding, load_thresholds, num

EVENTS_NAME = "ai-arena-events.csv"

# A file whose header lacks these is not an arena events table and is skipped
# rather than misread.
REQUIRED_EVENT_COLUMNS = ("category", "diedTick", "diedSeconds", "enemiesNear")

# A death with one of these causes was not a kill: the unit decayed unfinished
# at game end, was deliberately self-destructed (the game-end wipe), reclaimed,
# or died with its transport. "Died alone" cannot be asked of them.
EXPLAINED_DEATH_CAUSES = frozenset({
    "unfinished", "self_destruct", "reclaimed", "carrier_died",
})


def _already_explained(row) -> bool:
    # enemiesNear counts only canAttack units, so an enemy builder or
    # nanoframe on the site is invisible to it; a killer column or a non-kill
    # cause means the death is explained whatever that column says.
    if (row.get("deathCause") or "").strip() in EXPLAINED_DEATH_CAUSES:
        return True
    return any(
        (row.get(col) or "").strip() for col in ("killerPlayer", "killerType")
    )


def _thresholds(context) -> dict:
    # context.thresholds is the runner's parsed thresholds.toml. A direct call
    # (a test, a future caller) falls back to the file beside this module.
    t = getattr(context, "thresholds", None)
    if t:
        return t
    try:
        return load_thresholds()
    except (OSError, tomllib.TOMLDecodeError):
        return {}


def read_events(events_path) -> list:
    """Return ``[(line_number, row_dict), ...]``; malformed input yields []."""
    rows: list = []
    try:
        with open(events_path, newline="") as fh:
            reader = csv.DictReader(fh)
            if reader.fieldnames is None:
                return rows
            if any(col not in reader.fieldnames for col in REQUIRED_EVENT_COLUMNS):
                return rows
            try:
                for lineno, row in enumerate(reader, start=2):
                    rows.append((lineno, row))
            except csv.Error:
                pass
    except OSError:
        return rows
    return rows


def _death(lineno, row) -> dict:
    death = {
        "line": lineno,
        "player": num(row, "player"),
        "unitType": row.get("unitType") or "",
        "category": row.get("category") or "",
        "diedSeconds": num(row, "diedSeconds"),
        "x": num(row, "x"),
        "z": num(row, "z"),
        "nearestEnemyType": row.get("nearestEnemyType") or "",
        "enemiesNear": num(row, "enemiesNear"),
        "friendlyArmyNear": num(row, "friendlyArmyNear"),
        "friendlyTowersNear": num(row, "friendlyTowersNear"),
    }
    # Death-cause columns exist only on newer run roots (design §4 Phase 2); a
    # missing column reads as None and the keys stay out of the evidence.
    for col in ("deathCause", "killerType", "killerPlayer"):
        value = row.get(col)
        if value not in (None, ""):
            death[col] = value
    return death


def rule_d1(events_path, rows: list, t: dict) -> list:
    th = t.get("unitdeath", {})
    base = th.get("d1_extractor_deaths", 1)

    dead = [
        (lineno, row) for lineno, row in rows
        if (row.get("category") or "") == "economy"
        and num(row, "diedTick") is not None
        and (num(row, "enemiesNear", 0.0) or 0.0) == 0
        and not _already_explained(row)
    ]
    if len(dead) < base:
        return []
    severity = "likely-bug" if len(dead) >= 2 * base else "suspicious"
    return [Finding(
        checker="unitdeath",
        severity=severity,
        summary=(
            f"D1: {len(dead)} economy death(s) with no armed enemy or "
            f"attributed killer within 600 units"
        ),
        evidence={
            "file": str(events_path),
            "count": len(dead),
            "deaths": [_death(lineno, row) for lineno, row in dead],
            "threshold": {"extractor_deaths": base},
        },
    )]


def rule_d2(events_path, rows: list, t: dict) -> list:
    th = t.get("unitdeath", {})
    limit = th.get("d2_detached_army", 5)

    dead = [
        (lineno, row) for lineno, row in rows
        if (row.get("category") or "") == "army"
        and num(row, "diedTick") is not None
        and (num(row, "friendlyArmyNear", 0.0) or 0.0) == 0
        and (num(row, "friendlyTowersNear", 0.0) or 0.0) == 0
    ]
    if len(dead) < limit:
        return []
    return [Finding(
        checker="unitdeath",
        severity="suspicious",
        summary=(
            f"D2: {len(dead)} army death(s) with no friendly army or tower within 600 units"
        ),
        evidence={
            "file": str(events_path),
            "count": len(dead),
            "deaths": [_death(lineno, row) for lineno, row in dead[:3]],
            "threshold": {"detached_army": limit},
        },
    )]


def rule_d3(events_path, rows: list, t: dict) -> list:
    th = t.get("unitdeath", {})
    minute = th.get("d3_minute", 180)
    limit = th.get("d3_scout_deaths", 2)

    dead = [
        (lineno, row) for lineno, row in rows
        if (row.get("category") or "") == "scout"
        and num(row, "diedTick") is not None
        and num(row, "diedSeconds") is not None
        and num(row, "diedSeconds") < minute
        and (num(row, "enemiesNear", 0.0) or 0.0) == 0
    ]
    if len(dead) < limit:
        return []
    return [Finding(
        checker="unitdeath",
        severity="suspicious",
        summary=(
            f"D3: {len(dead)} scout death(s) before {int(minute)}s with no armed enemy "
            f"within 600 units"
        ),
        evidence={
            "file": str(events_path),
            "count": len(dead),
            "deaths": [_death(lineno, row) for lineno, row in dead],
            "threshold": {"minute": minute, "scout_deaths": limit},
        },
    )]


def check(run_dir, context=None) -> list:
    run_dir = Path(run_dir)
    t = _thresholds(context)
    events_path = run_dir / EVENTS_NAME
    rows = read_events(events_path)
    if not rows:
        return []
    findings: list = []
    findings += rule_d1(events_path, rows, t)
    findings += rule_d2(events_path, rows, t)
    findings += rule_d3(events_path, rows, t)
    return findings
