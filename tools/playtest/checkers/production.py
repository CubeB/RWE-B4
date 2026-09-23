"""Production checker (design §3.3): P1 idle factories, P2 builder collapse,
P3 build-order milestones absent at their checkpoints.

Thresholds come from ``thresholds.toml`` beside the checkers. TOML, not YAML:
stdlib only, no PyYAML on the machines that run this.
"""

from __future__ import annotations

import csv
import json
import tomllib
from pathlib import Path

from .economy import Finding, load_thresholds, num, read_rows

CSV_NAME = "ai-arena.csv"
EVENTS_NAME = "ai-arena-events.csv"
RUN_JSON_NAME = "run.json"

# A file whose header lacks these is not an arena events table and is skipped
# rather than misread.
REQUIRED_EVENT_COLUMNS = ("category", "completedSeconds", "diedSeconds")


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


def _example(lineno, row) -> dict:
    return {
        "line": lineno,
        "tick": num(row, "tick"),
        "seconds": num(row, "seconds"),
        "player": num(row, "player"),
    }


def rule_p1(csv_path, rows: list, t: dict) -> list:
    th = t.get("production", {})
    after = th.get("p1_after_seconds", 600)
    share_min = th.get("p1_share", 0.5)
    income_floor = th.get("p1_income_floor", 0.5)

    total = 0
    idle = 0
    examples: list = []
    for lineno, row in rows:
        secs = num(row, "seconds")
        if secs is None or secs < after:
            continue
        total += 1
        factories = num(row, "factories", 0.0) or 0.0
        idle_factories = num(row, "idleFactories", 0.0) or 0.0
        income = num(row, "metalIncome", 0.0)
        if factories > 0 and idle_factories == factories and income >= income_floor:
            idle += 1
            if len(examples) < 3:
                examples.append(_example(lineno, row))

    if total == 0:
        return []
    share = idle / total
    if share <= share_min:
        return []
    return [Finding(
        checker="production",
        severity="suspicious",
        summary=(
            f"P1: factories idle in {share * 100:.0f}% of the {total} samples at or after "
            f"{int(after)}s ({idle} idle-factory samples with positive income)"
        ),
        evidence={
            "file": str(csv_path),
            "share": round(share, 4),
            "samples": total,
            "idle_samples": idle,
            "examples": examples,
            "threshold": {"after_seconds": after, "share": share_min, "income_floor": income_floor},
        },
    )]


def _builder_death_seconds(events_path) -> list:
    seconds: list = []
    for _, row in read_events(events_path):
        if (row.get("category") or "") != "builder":
            continue
        if num(row, "diedTick") is None:
            continue
        secs = num(row, "diedSeconds")
        if secs is not None:
            seconds.append(secs)
    return seconds


def rule_p2(csv_path, rows: list, events_path, t: dict) -> list:
    th = t.get("production", {})
    min_samples = th.get("p2_min_samples", 3)

    # When the events table is absent there is nothing to explain a collapse
    # with, so the death filter is skipped rather than assumed to clear it.
    checked = events_path is not None and Path(events_path).exists()
    deaths = _builder_death_seconds(events_path) if checked else []

    timed = sorted(rows, key=lambda x: num(x[1], "seconds", 0.0))
    runs: list = []
    current: list = []
    for lineno, row in timed:
        if (num(row, "builders", 0.0) or 0.0) == 0:
            current.append((lineno, row))
        else:
            if current:
                runs.append(current)
            current = []
    if current:
        runs.append(current)

    best = None
    for run in runs:
        if len(run) < min_samples:
            continue
        start = num(run[0][1], "seconds", 0.0)
        end = num(run[-1][1], "seconds", 0.0)
        if checked and any(start <= d <= end for d in deaths):
            continue
        if best is None or len(run) > len(best):
            best = run
    if best is None:
        return []

    start = num(best[0][1], "seconds", 0.0)
    end = num(best[-1][1], "seconds", 0.0)
    return [Finding(
        checker="production",
        severity="suspicious",
        summary=(
            f"P2: zero builders for {len(best)} consecutive samples from {int(start)}s to "
            f"{int(end)}s with no builder death in that window"
        ),
        evidence={
            "file": str(csv_path),
            "start_seconds": int(start),
            "end_seconds": int(end),
            "samples": len(best),
            "rows": [lineno for lineno, _ in best],
            "examples": [_example(lineno, row) for lineno, row in best[:3]],
            "builder_deaths_checked": checked,
            "threshold": {"min_samples": min_samples},
        },
    )]


def _duration_seconds(rows: list, run_json_path) -> float | None:
    seconds = [num(row, "seconds") for _, row in rows]
    seconds = [s for s in seconds if s is not None]
    if seconds:
        return max(seconds)
    if run_json_path is not None and Path(run_json_path).exists():
        try:
            data = json.loads(Path(run_json_path).read_text())
        except (OSError, ValueError):
            return None
        value = data.get("durationSeconds") if isinstance(data, dict) else None
        try:
            return float(value)
        except (TypeError, ValueError):
            return None
    return None


def rule_p3(events_path, duration, t: dict) -> list:
    if events_path is None or not Path(events_path).exists():
        return []
    th = t.get("production", {})
    min_duration = th.get("p3_min_duration", 900)
    mex_minutes = th.get("p3_mex_minutes", 2)
    factory_minutes = th.get("p3_factory_minutes", 6)
    mex_deadline = mex_minutes * 60
    factory_deadline = factory_minutes * 60

    if duration is None or duration < min_duration:
        return []

    economy_done = None
    factory_done = None
    for _, row in read_events(events_path):
        secs = num(row, "completedSeconds")
        if secs is None:
            continue
        category = row.get("category") or ""
        if category == "economy" and (economy_done is None or secs < economy_done):
            economy_done = secs
        elif category == "factory" and (factory_done is None or secs < factory_done):
            factory_done = secs

    findings: list = []
    if economy_done is None or economy_done > mex_deadline:
        findings.append(Finding(
            checker="production",
            severity="suspicious",
            summary=(
                f"P3: no economy building completed within {int(mex_deadline)}s "
                f"of a {int(duration)}s game"
            ),
            evidence={
                "file": str(events_path),
                "duration_seconds": duration,
                "milestone": "economy",
                "deadline_seconds": int(mex_deadline),
                "earliest_completed_seconds": economy_done,
                "threshold": {"p3_min_duration": min_duration, "p3_mex_minutes": mex_minutes},
            },
        ))
    if factory_done is None or factory_done > factory_deadline:
        findings.append(Finding(
            checker="production",
            severity="suspicious",
            summary=(
                f"P3: no factory completed within {int(factory_deadline)}s "
                f"of a {int(duration)}s game"
            ),
            evidence={
                "file": str(events_path),
                "duration_seconds": duration,
                "milestone": "factory",
                "deadline_seconds": int(factory_deadline),
                "earliest_completed_seconds": factory_done,
                "threshold": {"p3_min_duration": min_duration, "p3_factory_minutes": factory_minutes},
            },
        ))
    return findings


def check(run_dir, context=None) -> list:
    run_dir = Path(run_dir)
    t = _thresholds(context)
    csv_path = run_dir / CSV_NAME
    events_path = run_dir / EVENTS_NAME
    run_json_path = run_dir / RUN_JSON_NAME
    rows = read_rows(csv_path)

    findings: list = []
    findings += rule_p1(csv_path, rows, t)
    findings += rule_p2(csv_path, rows, events_path, t)
    findings += rule_p3(events_path, _duration_seconds(rows, run_json_path), t)
    return findings
