"""Economy checker (design §3.3): E1 stalled economy, E2 stalled-share delta
against the same seed's control twin, E3 energy-waste phase jump.

Thresholds come from ``thresholds.toml`` beside this file. TOML, not YAML:
stdlib only, no PyYAML on the machines that run this.
"""

from __future__ import annotations

import csv
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_THRESHOLDS = HERE / "thresholds.toml"

CSV_NAME = "ai-arena.csv"

# The design fixes E2's "near-empty store" at 2% of maxMetal; only the twin
# delta is tunable, so this is the rule's definition and not a threshold.
STALL_STORE_FRACTION = 0.02

# The columns this checker needs. A file whose header lacks them is not an
# arena economy table and is skipped rather than misread.
REQUIRED_COLUMNS = ("player", "seconds", "metal", "maxMetal", "metalIncome", "metalDemand", "idleBuilders")


@dataclass
class Finding:
    checker: str
    severity: str
    summary: str
    evidence: dict = field(default_factory=dict)
    suggested_scenario: str | None = None


def load_thresholds(path=DEFAULT_THRESHOLDS) -> dict:
    with open(path, "rb") as fh:
        return tomllib.load(fh)


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


def num(row: dict, key: str, default=None):
    v = row.get(key)
    if v is None or v == "":
        return default
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def read_rows(csv_path) -> list:
    """Return ``[(line_number, row_dict), ...]``; malformed input yields []."""
    rows: list = []
    try:
        with open(csv_path, newline="") as fh:
            reader = csv.DictReader(fh)
            if reader.fieldnames is None:
                return rows
            if any(col not in reader.fieldnames for col in REQUIRED_COLUMNS):
                return rows
            try:
                for lineno, row in enumerate(reader, start=2):
                    rows.append((lineno, row))
            except csv.Error:
                pass
    except OSError:
        return rows
    return rows


def by_player(rows: list) -> dict:
    players: dict = {}
    for lineno, row in rows:
        p = num(row, "player")
        if p is None:
            continue
        players.setdefault(int(p), []).append((lineno, row))
    return players


def _side(prows: list) -> str:
    return prows[0][1].get("side", "?") if prows else "?"


def rule_e1(csv_path, rows: list, t: dict) -> list:
    th = t.get("economy", {})
    after = th.get("e1_after_seconds", 300)
    floor = th.get("e1_income_floor", 0.5)
    frac = th.get("e1_store_fraction", 0.02)
    n_suspicious = th.get("e1_min_samples_suspicious", 3)
    n_likely = th.get("e1_min_samples_likely_bug", 12)

    findings = []
    for player, prows in sorted(by_player(rows).items()):
        longest: list = []
        current: list = []
        for lineno, row in prows:
            secs = num(row, "seconds")
            if secs is None or secs < after:
                current = []
                continue
            income = num(row, "metalIncome", 0.0)
            metal = num(row, "metal", 0.0)
            max_metal = num(row, "maxMetal", 0.0) or 0.0
            idle = num(row, "idleBuilders", 0.0) or 0.0
            stalled = income < floor and metal < frac * max(max_metal, 1.0) and idle > 0
            if stalled:
                current.append((lineno, secs))
                if len(current) > len(longest):
                    longest = list(current)
            else:
                current = []

        if len(longest) < n_suspicious:
            continue
        severity = "likely-bug" if len(longest) >= n_likely else "suspicious"
        findings.append(Finding(
            checker="economy",
            severity=severity,
            summary=(
                f"E1: player {player} ({_side(prows)}) stalled for {len(longest)} consecutive "
                f"samples from {int(longest[0][1])}s to {int(longest[-1][1])}s"
            ),
            evidence={
                "file": str(csv_path),
                "player": player,
                "side": _side(prows),
                "start_seconds": int(longest[0][1]),
                "end_seconds": int(longest[-1][1]),
                "samples": len(longest),
                "rows": [lineno for lineno, _ in longest],
                "threshold": {
                    "after_seconds": after,
                    "income_floor": floor,
                    "store_fraction": frac,
                    "min_samples_suspicious": n_suspicious,
                    "min_samples_likely_bug": n_likely,
                },
            },
        ))
    return findings


def twin_dir(context):
    """The control twin to compare against, or None.

    Only a non-control run has one: a control run is the reference, and E2
    firing symmetrically would just duplicate the same finding from both arms.
    """
    if getattr(context, "arm", None) == "control":
        return None
    control = (getattr(context, "siblings", None) or {}).get("control")
    return Path(control) if control else None


def stalled_share(csv_path, after: float) -> dict:
    """Per player: share of samples at/after ``after`` seconds that are stalled."""
    rows = read_rows(csv_path)
    out: dict = {}
    for player, prows in sorted(by_player(rows).items()):
        total = 0
        stalled = 0
        stalled_rows: list = []
        for lineno, row in prows:
            secs = num(row, "seconds")
            if secs is None or secs < after:
                continue
            total += 1
            metal = num(row, "metal", 0.0)
            max_metal = num(row, "maxMetal", 0.0) or 0.0
            demand = num(row, "metalDemand", 0.0)
            income = num(row, "metalIncome", 0.0)
            if metal < STALL_STORE_FRACTION * max(max_metal, 1.0) and demand > income:
                stalled += 1
                stalled_rows.append(lineno)
        out[player] = {
            "share": (stalled / total) if total else 0.0,
            "stalled": stalled,
            "total": total,
            "rows": stalled_rows,
            "file": str(csv_path),
        }
    return out


def rule_e2(csv_path, t: dict, context) -> list:
    th = t.get("economy", {})
    after = th.get("e2_after_seconds", 480)
    delta_points = th.get("e2_delta_points", 25)

    twin = twin_dir(context)
    if twin is None:
        return []
    twin_csv = twin / CSV_NAME
    if not twin_csv.exists():
        return []

    mine = stalled_share(csv_path, after)
    theirs = stalled_share(twin_csv, after)

    findings = []
    for player in sorted(set(mine) & set(theirs)):
        a = mine[player]
        b = theirs[player]
        delta = abs(a["share"] - b["share"]) * 100.0
        if delta <= delta_points:
            continue
        findings.append(Finding(
            checker="economy",
            severity="suspicious",
            summary=(
                f"E2: player {player} stalled share differs from control by "
                f"{delta:.0f} points ({a['share'] * 100:.0f}% vs {b['share'] * 100:.0f}%)"
            ),
            evidence={
                "player": player,
                "delta_points": round(delta, 2),
                "run": {
                    "file": str(csv_path),
                    "share": round(a["share"], 4),
                    "stalled": a["stalled"],
                    "total": a["total"],
                    "rows": a["rows"],
                },
                "twin": {
                    "dir": str(twin),
                    "file": str(twin_csv),
                    "share": round(b["share"], 4),
                    "stalled": b["stalled"],
                    "total": b["total"],
                    "rows": b["rows"],
                },
                "threshold": {"after_seconds": after, "delta_points": delta_points},
            },
        ))
    return findings


def _window_share(prows: list, lo_frac: float, hi_frac: float) -> dict:
    """Energy excess/produced share over the [lo, hi] fraction of the timeline."""
    timed = [(lineno, row, num(row, "seconds", 0.0)) for lineno, row in prows]
    timed.sort(key=lambda x: x[2])
    if len(timed) < 2:
        return {"share": 0.0, "rows": []}
    lo_s = timed[0][2]
    hi_s = timed[-1][2]
    if hi_s <= lo_s:
        return {"share": 0.0, "rows": []}
    cut_lo = lo_s + lo_frac * (hi_s - lo_s)
    cut_hi = lo_s + hi_frac * (hi_s - lo_s)
    window = [x for x in timed if cut_lo <= x[2] <= cut_hi]
    if len(window) < 2:
        return {"share": 0.0, "rows": [x[0] for x in window]}
    produced = num(window[-1][1], "energyProduced", 0.0) - num(window[0][1], "energyProduced", 0.0)
    excess = num(window[-1][1], "energyExcess", 0.0) - num(window[0][1], "energyExcess", 0.0)
    if produced <= 0:
        return {"share": 0.0, "rows": [x[0] for x in window]}
    return {"share": excess / produced, "rows": [x[0] for x in window]}


def rule_e3(csv_path, rows: list, t: dict) -> list:
    th = t.get("economy", {})
    whole_min = th.get("e3_whole_game_share", 0.25)
    jump_points = th.get("e3_phase_jump_points", 20)

    findings = []
    for player, prows in sorted(by_player(rows).items()):
        timed = sorted(prows, key=lambda x: num(x[1], "seconds", 0.0))
        produced = num(timed[-1][1], "energyProduced", 0.0)
        excess = num(timed[-1][1], "energyExcess", 0.0)
        if produced <= 0:
            continue
        whole = excess / produced
        early = _window_share(prows, 0.0, 2.0 / 3.0)
        late = _window_share(prows, 2.0 / 3.0, 1.0)
        jump = (late["share"] - early["share"]) * 100.0
        if whole > whole_min and jump > jump_points:
            findings.append(Finding(
                checker="economy",
                severity="info",
                summary=(
                    f"E3: player {player} ({_side(prows)}) energy waste share {whole * 100:.0f}% "
                    f"with a {jump:.0f}-point jump into the final third"
                ),
                evidence={
                    "file": str(csv_path),
                    "player": player,
                    "side": _side(prows),
                    "whole_game_share": round(whole, 4),
                    "first_two_thirds_share": round(early["share"], 4),
                    "final_third_share": round(late["share"], 4),
                    "jump_points": round(jump, 2),
                    "early_rows": early["rows"],
                    "late_rows": late["rows"],
                    "threshold": {"whole_game_share": whole_min, "phase_jump_points": jump_points},
                },
            ))
    return findings


def check(run_dir, context=None) -> list:
    run_dir = Path(run_dir)
    t = _thresholds(context)
    csv_path = run_dir / CSV_NAME
    rows = read_rows(csv_path)
    if not rows:
        return []
    findings: list = []
    findings += rule_e1(csv_path, rows, t)
    findings += rule_e2(csv_path, t, context)
    findings += rule_e3(csv_path, rows, t)
    return findings
