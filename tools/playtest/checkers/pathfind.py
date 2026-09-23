"""Pathfinder-profile checker (design §3.3): H1 sustained exhausted searches
(the issue #155 signature), H2 growing stuck counters, H3 sustained queue
pressure.

When the run dir has an ``event-log.jsonl`` (design §9) the windows come from
its ``path_stats`` events and the prose log is not read at all; otherwise the
checker falls back to ``path-profile.log`` (or the ``path profile `` lines in
``game.log``) for older run roots. Every value is a window delta, one window per
300 sim ticks, so "consecutive" means the tick fields also step by 300 -- a
malformed line that drops a window must not stitch the runs either side of it
together.
"""

from __future__ import annotations

import re
from pathlib import Path

from .economy import Finding, load_thresholds
from .eventlog import event_log_path, has_event_log, read_events

PROFILE_NAME = "path-profile.log"
GAME_LOG_NAME = "game.log"
PROFILE_MARKER = "path profile "
WINDOW_TICKS = 300

# One value per field the rules read; a line missing any of them is not a
# profile window and is skipped rather than half-read.
_FIELD_RES = {
    "tick": re.compile(r"\bt=(\d+)"),
    "exhausted": re.compile(r"\bexhausted=(\d+)"),
    "bugwalk": re.compile(r"\bbugwalk=(\d+)"),
    "walkstuck": re.compile(r"\bwalkstuck=(\d+)"),
    "queued_per_tick": re.compile(r"\bqueued/tick=([\d.]+)"),
}


def _thresholds(context) -> dict:
    t = getattr(context, "thresholds", None)
    if t:
        return t
    try:
        return load_thresholds()
    except Exception:
        return {}


def _read_entries(path: Path):
    try:
        with open(path, errors="replace") as fh:
            return [(lineno, line) for lineno, line in enumerate(fh, start=1)]
    except OSError:
        return []


def read_source(run_dir):
    """Return ``(path_or_None, entries)``, preferring the extracted profile."""
    run_dir = Path(run_dir)
    extracted = run_dir / PROFILE_NAME
    if extracted.exists():
        entries = _read_entries(extracted)
        if entries:
            return extracted, entries
    game = run_dir / GAME_LOG_NAME
    if game.exists():
        return game, _read_entries(game)
    return None, []


def parse_windows(entries) -> list:
    """``[{"lineno", "tick", "exhausted", "bugwalk", "walkstuck",
    "queued_per_tick"}, ...]``; malformed lines are dropped."""
    windows = []
    for lineno, text in entries:
        if PROFILE_MARKER not in text:
            continue
        values = {"lineno": lineno}
        good = True
        for key, rx in _FIELD_RES.items():
            m = rx.search(text)
            if not m:
                good = False
                break
            try:
                values[key] = float(m.group(1)) if key == "queued_per_tick" else int(m.group(1))
            except ValueError:
                good = False
                break
        if good:
            windows.append(values)
    return windows


# ``path_stats`` event fields the rules read. The event's envelope ``tick`` is
# the window tick, on the same 300-tick cadence as the prose line's ``t=``.
_EVENT_FIELDS = {
    "exhausted": int,
    "bugwalk": int,
    "walkstuck": int,
    "queued_per_tick": float,
}


def parse_event_windows(entries) -> list:
    """Same shape as :func:`parse_windows`, built from ``path_stats`` events.

    Windows are returned in tick order; an event missing any field the rules
    read is dropped rather than half-read.
    """
    windows = []
    for lineno, event in entries:
        if event.get("ev") != "path_stats":
            continue
        try:
            values = {"lineno": lineno, "tick": int(event["tick"])}
        except (KeyError, TypeError, ValueError):
            continue
        good = True
        for key, caster in _EVENT_FIELDS.items():
            try:
                values[key] = caster(event[key])
            except (KeyError, TypeError, ValueError):
                good = False
                break
        if good:
            windows.append(values)
    windows.sort(key=lambda w: w["tick"])
    return windows


def _longest_run(windows, holds, advances) -> list:
    """Longest run where ``holds`` is true and each step ``advances``."""
    best: list = []
    current: list = []
    for window in windows:
        if not holds(window):
            current = []
            continue
        if current and not advances(current[-1], window):
            current = []
        current.append(window)
        if len(current) > len(best):
            best = list(current)
    return best


def _adjacent(prev: dict, cur: dict) -> bool:
    return cur["tick"] - prev["tick"] == WINDOW_TICKS


def rule_h1(profile_path, windows: list, t: dict) -> list:
    th = t.get("pathfind", {})
    n_windows = th.get("h1_windows", 6)

    run = _longest_run(
        windows,
        lambda w: w["exhausted"] > 0,
        _adjacent,
    )
    if len(run) < n_windows:
        return []
    return [Finding(
        checker="pathfind",
        severity="suspicious",
        summary=(
            f"H1: {len(run)} consecutive windows with exhausted searches "
            f"(t={run[0]['tick']}..{run[-1]['tick']})"
        ),
        evidence={
            "file": str(profile_path),
            "rule": "H1",
            "start_tick": run[0]["tick"],
            "end_tick": run[-1]["tick"],
            "windows": len(run),
            "exhausted": [w["exhausted"] for w in run],
            "rows": [w["lineno"] for w in run],
            "threshold": {"h1_windows": n_windows},
        },
    )]


def rule_h2(profile_path, windows: list, t: dict) -> list:
    th = t.get("pathfind", {})
    n_windows = th.get("h2_windows", 10)

    findings = []
    for counter in ("bugwalk", "walkstuck"):
        run = _longest_run(
            windows,
            lambda w, k=counter: w[k] > 0,
            lambda prev, cur, k=counter: cur[k] > prev[k] and _adjacent(prev, cur),
        )
        if len(run) < n_windows:
            continue
        findings.append(Finding(
            checker="pathfind",
            severity="suspicious",
            summary=(
                f"H2: {counter} rising across {len(run)} consecutive windows "
                f"(t={run[0]['tick']}..{run[-1]['tick']})"
            ),
            evidence={
                "file": str(profile_path),
                "rule": "H2",
                "counter": counter,
                "start_tick": run[0]["tick"],
                "end_tick": run[-1]["tick"],
                "windows": len(run),
                "values": [w[counter] for w in run],
                "rows": [w["lineno"] for w in run],
                "threshold": {"h2_windows": n_windows},
            },
        ))
    return findings


def rule_h3(profile_path, windows: list, t: dict) -> list:
    th = t.get("pathfind", {})
    rate = th.get("h3_queued_per_tick", 0.5)
    n_windows = th.get("h3_windows", 6)

    run = _longest_run(
        windows,
        lambda w: w["queued_per_tick"] > rate,
        _adjacent,
    )
    if len(run) < n_windows:
        return []
    peak = max(w["queued_per_tick"] for w in run)
    return [Finding(
        checker="pathfind",
        severity="suspicious",
        summary=(
            f"H3: queued/tick above {rate} across {len(run)} consecutive windows "
            f"(peak {peak:g} at t={run[0]['tick']}..{run[-1]['tick']})"
        ),
        evidence={
            "file": str(profile_path),
            "rule": "H3",
            "start_tick": run[0]["tick"],
            "end_tick": run[-1]["tick"],
            "windows": len(run),
            "max_queued_per_tick": peak,
            "queued_per_tick": [w["queued_per_tick"] for w in run],
            "rows": [w["lineno"] for w in run],
            "threshold": {"h3_queued_per_tick": rate, "h3_windows": n_windows},
        },
    )]


def check(run_dir, context=None) -> list:
    t = _thresholds(context)
    if has_event_log(run_dir):
        profile_path = event_log_path(run_dir)
        windows = parse_event_windows(read_events(run_dir))
    else:
        profile_path, entries = read_source(run_dir)
        if profile_path is None:
            return []
        windows = parse_windows(entries)
    if not windows:
        return []
    findings: list = []
    findings += rule_h1(profile_path, windows, t)
    findings += rule_h2(profile_path, windows, t)
    findings += rule_h3(profile_path, windows, t)
    return findings
