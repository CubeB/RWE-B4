"""Determinism/harness checker (design §3.3): D1 same-seed divergence between
the repeated ``-r1``/``-r2`` twins of one run, D2 a run that produced no
``AI-ARENA-RESULT`` line, D3 a harness fault recorded in ``index.jsonl``.

The pair rule byte-compares ``ai-arena.csv`` and, when both arms have one,
``event-log.jsonl`` (design §9) -- a stronger determinism check, because the
event log carries sim-tick facts with no wall-clock in them. It is only
reported by the ``-r1`` run, so a divergent pair yields exactly one finding per
divergent file. A missing event log is not an error -- older run roots have
none -- and the CSV comparison is then the whole rule.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

from .economy import Finding

CSV_NAME = "ai-arena.csv"
EVENT_LOG_NAME = "event-log.jsonl"
GAME_LOG_NAME = "game.log"
INDEX_NAME = "index.jsonl"
RESULT_MARKER = "AI-ARENA-RESULT"

_REPEAT_RE = re.compile(r"^(?P<base>.+)-r\d+$")


def _context(context, name, default=None):
    return getattr(context, name, default)


def _first_difference(a: bytes, b: bytes) -> int:
    limit = min(len(a), len(b))
    for offset in range(limit):
        if a[offset] != b[offset]:
            return offset
    return limit


def _line_at(data: bytes, offset: int) -> bytes:
    """The line of ``data`` containing byte ``offset`` (newline-delimited)."""
    start = data.rfind(b"\n", 0, offset) + 1
    end = data.find(b"\n", offset)
    if end == -1:
        end = len(data)
    return data[start:end]


def _ev_at(data: bytes, offset: int):
    """The ``ev`` of the JSON object on the line containing ``offset``, or None."""
    line = _line_at(data, offset)
    try:
        obj = json.loads(line.decode("utf-8", "replace"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        return None
    return obj.get("ev") if isinstance(obj, dict) else None


def _pair_paths(first, second, name: Path):
    return Path(first) / name, Path(second) / name


def _csv_pair(first, second, base) -> list:
    a, b = _pair_paths(first, second, CSV_NAME)
    missing = [str(path) for path in (a, b) if not path.exists()]
    if missing:
        return [Finding(
            checker="desync",
            severity="suspicious",
            summary=(
                f"desync: repeated pair {base}-r1/-r2 is missing {CSV_NAME} "
                f"in {len(missing)} of 2 arms"
            ),
            evidence={"pair": [str(a), str(b)], "missing": missing},
        )]

    try:
        data_a = a.read_bytes()
        data_b = b.read_bytes()
    except OSError as exc:
        return [Finding(
            checker="desync",
            severity="suspicious",
            summary=f"desync: could not read the repeated pair's {CSV_NAME}: {exc}",
            evidence={"pair": [str(a), str(b)]},
        )]

    if data_a == data_b:
        return []
    offset = _first_difference(data_a, data_b)
    return [Finding(
        checker="desync",
        severity="likely-bug",
        summary=(
            f"desync: same-seed divergence between {a.parent.name} and "
            f"{b.parent.name} (first differing byte {offset})"
        ),
        evidence={
            "pair": [str(a), str(b)],
            "first_diff_offset": offset,
            "sizes": [len(data_a), len(data_b)],
        },
    )]


def _event_log_pair(first, second) -> list:
    """Byte-compare the twins' event logs, when both have one."""
    a, b = _pair_paths(first, second, EVENT_LOG_NAME)
    if not (a.exists() and b.exists()):
        return []
    try:
        data_a = a.read_bytes()
        data_b = b.read_bytes()
    except OSError:
        return []
    if data_a == data_b:
        return []
    offset = _first_difference(data_a, data_b)
    return [Finding(
        checker="desync",
        severity="likely-bug",
        summary=(
            f"desync: same-seed event-log divergence between {a.parent.name} and "
            f"{b.parent.name} (first differing byte {offset})"
        ),
        evidence={
            "pair": [str(a), str(b)],
            "first_diff_offset": offset,
            "sizes": [len(data_a), len(data_b)],
            "ev": _ev_at(data_a, offset),
            "ev_b": _ev_at(data_b, offset),
        },
    )]


def rule_pair(run_dir, context) -> list:
    arm = _context(context, "arm")
    if not arm or not arm.endswith("-r1"):
        return []
    m = _REPEAT_RE.match(arm)
    if m is None:
        return []
    base = m.group("base")
    siblings = _context(context, "siblings") or {}
    first = siblings.get(f"{base}-r1")
    second = siblings.get(f"{base}-r2")
    if not first or not second:
        return []

    findings: list = []
    findings += _csv_pair(first, second, base)
    findings += _event_log_pair(first, second)
    return findings


def rule_result(run_dir) -> list:
    game = Path(run_dir) / GAME_LOG_NAME
    if not game.exists():
        return []
    try:
        with open(game, errors="replace") as fh:
            for line in fh:
                if RESULT_MARKER in line:
                    return []
    except OSError:
        return []
    return [Finding(
        checker="desync",
        severity="suspicious",
        summary="desync: game.log has no AI-ARENA-RESULT line",
        evidence={"file": str(game)},
    )]


def rule_index(context) -> list:
    run_root = _context(context, "run_root")
    scenario = _context(context, "scenario")
    seed = _context(context, "seed")
    arm = _context(context, "arm")
    if not run_root or scenario is None or seed is None or not arm:
        return []
    index = Path(run_root) / INDEX_NAME
    if not index.exists():
        return []

    run_id = f"{scenario}/seed{seed}-{arm}"
    try:
        with open(index, errors="replace") as fh:
            for lineno, line in enumerate(fh, start=1):
                line = line.strip()
                if not line:
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if not isinstance(record, dict) or record.get("run_id") != run_id:
                    continue
                status = str(record.get("status", ""))
                if status in ("timeout", "no-result", "missing-run-json"):
                    return [Finding(
                        checker="desync",
                        severity="suspicious",
                        summary=f"desync: harness status {status} for this run",
                        evidence={
                            "file": str(index),
                            "line": lineno,
                            "run_id": run_id,
                            "status": status,
                        },
                    )]
                if status.startswith("exit-"):
                    return [Finding(
                        checker="desync",
                        severity="likely-bug",
                        summary=f"desync: harness status {status} for this run (crash)",
                        evidence={
                            "file": str(index),
                            "line": lineno,
                            "run_id": run_id,
                            "status": status,
                        },
                    )]
                return []
    except OSError:
        return []
    return []


def check(run_dir, context=None) -> list:
    findings: list = []
    findings += rule_pair(run_dir, context)
    findings += rule_result(run_dir)
    findings += rule_index(context)
    return findings
