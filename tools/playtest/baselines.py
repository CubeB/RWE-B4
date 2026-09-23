#!/usr/bin/env python3
"""Baseline store and drift (design §5.3).

The design says a baseline holds the ``--json`` output of
``tools/arena-analyse.py`` for a scenario's runs. That tool's input naming does
not match this runner's run-root layout (it walks ``<outDir>`` assuming the
arena's own naming), so the aggregates are computed here instead. The output
shape is the design's intent, not the tool's bytes.

One JSON file per scenario per git SHA under ``<data_root>/playtest/baselines/``:
``<scenario>.<sha>.json`` with the pinned schema ``{"schema", "scenario",
"sha", "arms"}``. Deterministic: same inputs -> identical bytes, no timestamps.

Stdlib only, and no import of ``run`` or ``checkers`` -- a checker module may
``import baselines`` from the ``tools/playtest`` directory that ``run.py`` puts
on ``sys.path``, and this module must resolve there as a plain top-level module.
"""

from __future__ import annotations

import csv
import json
import os
import re
import subprocess
from pathlib import Path

SCHEMA = 1
CSV_NAME = "ai-arena.csv"
EVENTS_NAME = "ai-arena-events.csv"
PROFILE_NAME = "ai-profile.log"

# Aggregates are shares over samples at or after this many seconds (design
# §5.3). The design fixes the stall/capped fractions too, so these are the
# rule's definition rather than tunable thresholds.
SHARE_AFTER_SECONDS = 480
STALL_STORE_FRACTION = 0.02
CAPPED_STORE_FRACTION = 0.95

SHARE_NAMES = ("stalled", "capped", "wasted_energy", "idle_builders", "idle_factories")
MILESTONE_NAMES = ("first_economy_seconds", "first_factory_seconds")

RUN_DIR_RE = re.compile(r"^seed(\d+)-(.+)$")
REPEAT_SUFFIX_RE = re.compile(r"-r\d+$")

SUMMARY_RE = re.compile(
    r"AI profile summary: player (\d+) at tick \d+, total [\d.]+ ms: (.*)")
ENTRY_RE = re.compile(
    r"([A-Za-z_][\w.\-]*)=([\d.]+)/(\d+) \(worst [\d.]+, (\d+) spikes\)")


# --------------------------------------------------------------------------
# git (read-only)
# --------------------------------------------------------------------------

def _default_repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def current_sha(repo_root=None) -> str:
    """``git rev-parse --short HEAD`` in repo_root, or "" on any failure."""
    root = Path(repo_root) if repo_root else _default_repo_root()
    try:
        proc = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=str(root), capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return ""
    if proc.returncode != 0:
        return ""
    return proc.stdout.strip()


def _is_ancestor(candidate: str, sha: str, repo_root) -> bool:
    """True when ``candidate`` is an ancestor of ``sha``; False on any failure."""
    try:
        proc = subprocess.run(
            ["git", "merge-base", "--is-ancestor", candidate, sha],
            cwd=str(repo_root), capture_output=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return False
    return proc.returncode == 0


def _commit_time(sha: str, repo_root) -> int:
    try:
        proc = subprocess.run(
            ["git", "show", "-s", "--format=%ct", sha],
            cwd=str(repo_root), capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return 0
    if proc.returncode != 0:
        return 0
    try:
        return int(proc.stdout.strip())
    except ValueError:
        return 0


def _data_root(data_root=None) -> Path:
    if data_root is not None:
        return Path(data_root)
    env = os.environ.get("RWE_LOCAL_DATA")
    if env:
        return Path(env)
    return Path.home() / ".rwe"


def baselines_dir(data_root=None) -> Path:
    return _data_root(data_root) / "playtest" / "baselines"


def closest_baseline(scenario: str, sha: str, data_root=None, repo_root=None) -> dict | None:
    """Newest baseline for ``scenario`` whose SHA is an ancestor of ``sha``.

    None when the baselines directory is absent, ``sha`` is empty, no candidate
    qualifies, or git is unavailable.
    """
    if not sha:
        return None
    root = Path(repo_root) if repo_root else _default_repo_root()
    directory = baselines_dir(data_root)
    if not directory.is_dir():
        return None

    candidates = []
    prefix = f"{scenario}."
    for path in sorted(directory.glob("*.json")):
        if not path.name.startswith(prefix):
            continue
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        candidate_sha = doc.get("sha")
        if not candidate_sha:
            continue
        if not _is_ancestor(candidate_sha, sha, root):
            continue
        candidates.append((_commit_time(candidate_sha, root), candidate_sha, doc))

    if not candidates:
        return None
    candidates.sort(key=lambda c: (c[0], c[1]))
    return candidates[-1][2]


# --------------------------------------------------------------------------
# parsing helpers
# --------------------------------------------------------------------------

def _num(row: dict, key: str, default=0.0):
    value = row.get(key)
    if value is None or value == "":
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _read_rows(path) -> list:
    rows: list = []
    try:
        with open(path, newline="") as fh:
            reader = csv.DictReader(fh)
            if reader.fieldnames is None:
                return rows
            try:
                rows = list(reader)
            except csv.Error:
                return rows
    except OSError:
        return rows
    return rows


def _run_shares(rows: list) -> dict:
    total = 0
    stalled = 0
    capped = 0
    produced = 0.0
    excess = 0.0
    idle_builders_sum = 0.0
    idle_builders_n = 0
    idle_factories_sum = 0.0
    idle_factories_n = 0

    for row in rows:
        seconds = _num(row, "seconds", None)
        if seconds is None or seconds < SHARE_AFTER_SECONDS:
            continue
        total += 1

        metal = _num(row, "metal")
        max_metal = _num(row, "maxMetal")
        energy = _num(row, "energy")
        max_energy = _num(row, "maxEnergy")
        demand = _num(row, "metalDemand")
        income = _num(row, "metalIncome")

        if metal < STALL_STORE_FRACTION * max_metal and demand > income:
            stalled += 1
        if ((max_metal > 0 and metal >= CAPPED_STORE_FRACTION * max_metal)
                or (max_energy > 0 and energy >= CAPPED_STORE_FRACTION * max_energy)):
            capped += 1

        produced += _num(row, "energyProduced")
        excess += _num(row, "energyExcess")

        builders = _num(row, "builders")
        if builders > 0:
            idle_builders_sum += _num(row, "idleBuilders") / builders
            idle_builders_n += 1
        factories = _num(row, "factories")
        if factories > 0:
            idle_factories_sum += _num(row, "idleFactories") / factories
            idle_factories_n += 1

    return {
        "stalled": (stalled / total) if total else 0.0,
        "capped": (capped / total) if total else 0.0,
        "wasted_energy": (excess / produced) if produced > 0 else 0.0,
        "idle_builders": (idle_builders_sum / idle_builders_n) if idle_builders_n else 0.0,
        "idle_factories": (idle_factories_sum / idle_factories_n) if idle_factories_n else 0.0,
    }


def _run_milestones(events_path) -> dict:
    first = {name: None for name in MILESTONE_NAMES}
    for row in _read_rows(events_path):
        category = (row.get("category") or "").strip()
        if category == "economy":
            key = "first_economy_seconds"
        elif category == "factory":
            key = "first_factory_seconds"
        else:
            continue
        value = row.get("completedSeconds")
        if value is None or value == "":
            continue
        try:
            seconds = float(value)
        except (TypeError, ValueError):
            continue
        if first[key] is None or seconds < first[key]:
            first[key] = seconds
    return first


def _run_perf(profile_path) -> dict:
    totals: dict = {}
    if not profile_path.is_file():
        return {}
    try:
        lines = profile_path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return {}
    for line in lines:
        summary = SUMMARY_RE.search(line)
        if not summary:
            continue
        player = summary.group(1)
        for entry in ENTRY_RE.finditer(summary.group(2)):
            key = f"{player}:{entry.group(1)}"
            record = totals.setdefault(key, {"ms": 0.0, "calls": 0, "spikes": 0})
            record["ms"] += float(entry.group(2))
            record["calls"] += int(entry.group(3))
            record["spikes"] += int(entry.group(4))
    return {
        key: {
            "mean_ms": (record["ms"] / record["calls"]) if record["calls"] else 0.0,
            "spikes": record["spikes"],
        }
        for key, record in totals.items()
    }


# --------------------------------------------------------------------------
# aggregation
# --------------------------------------------------------------------------

def _mean(values):
    present = [v for v in values if v is not None]
    return sum(present) / len(present) if present else None


def _discover(run_root) -> dict:
    """scenario -> base_arm -> [run_dir], skipping non run dirs."""
    run_root = Path(run_root)
    scenarios: dict = {}
    if not run_root.is_dir():
        return scenarios
    for scenario_dir in sorted(p for p in run_root.iterdir() if p.is_dir()):
        arms: dict = {}
        for run_dir in sorted(p for p in scenario_dir.iterdir() if p.is_dir()):
            match = RUN_DIR_RE.match(run_dir.name)
            if not match:
                continue
            base_arm = REPEAT_SUFFIX_RE.sub("", match.group(2))
            arms.setdefault(base_arm, []).append(run_dir)
        if arms:
            scenarios[scenario_dir.name] = arms
    return scenarios


def _arm_aggregate(run_dirs: list) -> dict:
    per_run = []
    for run_dir in run_dirs:
        per_run.append({
            "shares": _run_shares(_read_rows(run_dir / CSV_NAME)),
            "milestones": _run_milestones(run_dir / EVENTS_NAME),
            "perf": _run_perf(run_dir / PROFILE_NAME),
        })

    shares = {
        name: round(_mean([run["shares"][name] for run in per_run]) or 0.0, 4)
        for name in SHARE_NAMES
    }
    milestones = {}
    for name in MILESTONE_NAMES:
        mean = _mean([run["milestones"][name] for run in per_run])
        milestones[name] = round(mean, 3) if mean is not None else None

    perf_keys = sorted({key for run in per_run for key in run["perf"]})
    perf = {}
    for key in perf_keys:
        entries = [run["perf"][key] for run in per_run if key in run["perf"]]
        perf[key] = {
            "mean_ms": round(_mean([e["mean_ms"] for e in entries]) or 0.0, 4),
            "spikes": round(_mean([e["spikes"] for e in entries]) or 0.0, 2),
        }

    return {"shares": shares, "milestones": milestones, "perf": perf}


def write_baselines(run_root, repo_root, data_root=None) -> Path | None:
    """Write one ``<scenario>.<sha>.json`` per scenario under the data root.

    Returns the baselines directory when at least one file was written, else
    None. Skips silently (None) when the SHA is empty (no git) or when any run
    directory is missing its ``ai-arena.csv``.
    """
    sha = current_sha(repo_root)
    if not sha:
        return None

    scenarios = _discover(run_root)
    if not scenarios:
        return None

    for arms in scenarios.values():
        for run_dirs in arms.values():
            for run_dir in run_dirs:
                if not (run_dir / CSV_NAME).is_file():
                    return None

    directory = baselines_dir(data_root)
    directory.mkdir(parents=True, exist_ok=True)
    for scenario, arms in sorted(scenarios.items()):
        document = {
            "schema": SCHEMA,
            "scenario": scenario,
            "sha": sha,
            "arms": {
                base_arm: _arm_aggregate(run_dirs)
                for base_arm, run_dirs in sorted(arms.items())
            },
        }
        (directory / f"{scenario}.{sha}.json").write_text(
            json.dumps(document, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return directory


# --------------------------------------------------------------------------
# drift
# --------------------------------------------------------------------------

def _share_table(obj) -> dict:
    if not isinstance(obj, dict):
        return {}
    if isinstance(obj.get("shares"), dict):
        return obj["shares"]
    arms = obj.get("arms")
    if isinstance(arms, dict) and len(arms) == 1:
        only = next(iter(arms.values()))
        if isinstance(only, dict) and isinstance(only.get("shares"), dict):
            return only["shares"]
    return {k: v for k, v in obj.items()
            if isinstance(v, (int, float)) and not isinstance(v, bool)}


def drift(current, baseline, points) -> dict:
    """Shares that moved more than ``points`` percentage points.

    ``current`` and ``baseline`` are an arm aggregate (with a ``shares``
    mapping), a bare share mapping, or a single-arm baseline document. Shares
    missing from either side are skipped.
    """
    cur = _share_table(current)
    base = _share_table(baseline)
    out = {}
    for name in sorted(cur):
        if name not in base:
            continue
        try:
            c = float(cur[name])
            b = float(base[name])
        except (TypeError, ValueError):
            continue
        delta = abs(c - b) * 100.0
        if delta > points:
            out[name] = {"current": c, "baseline": b, "delta_points": round(delta, 2)}
    return out
