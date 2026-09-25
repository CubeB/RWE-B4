"""Invariant checker (design §3.3): resources are never negative; the result
line's per-player unit count agrees with what the events table says is alive;
a commander that died while the game was not decided; a run.json that recorded
no game at all.

I3 applies only to two-player games: in a >2-player game a player can be
eliminated and the game still legitimately run to a timeout.

No thresholds: every rule here is an invariant, not a tuned share.
"""

from __future__ import annotations

import csv
import json
import re
from pathlib import Path

from .economy import Finding, num, read_rows

CSV_NAME = "ai-arena.csv"
EVENTS_NAME = "ai-arena-events.csv"
LOG_NAME = "game.log"
RUN_JSON_NAME = "run.json"

RESULT_RE = re.compile(r"AI-ARENA-RESULT")
PLAYER_UNITS_RE = re.compile(r"p(\d+)\s*=\s*\S+\s+\S+\s+units=(\d+)")
ENDED_RE = re.compile(r"ended=(\S+)")

REQUIRED_EVENT_COLUMNS = ("category", "completedTick", "diedTick")


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


def parse_result(log_path):
    """``(ended, {player: units})`` from the AI-ARENA-RESULT line, or None."""
    try:
        with open(log_path, errors="replace") as fh:
            for line in fh:
                if RESULT_RE.search(line):
                    ended_match = ENDED_RE.search(line)
                    ended = ended_match.group(1) if ended_match else None
                    units = {int(p): int(u) for p, u in PLAYER_UNITS_RE.findall(line)}
                    return ended, units
    except OSError:
        return None
    return None


def rule_negative(csv_path, rows: list) -> list:
    offenders: list = []
    for lineno, row in rows:
        metal = num(row, "metal")
        energy = num(row, "energy")
        if (metal is not None and metal < 0) or (energy is not None and energy < 0):
            offenders.append({
                "line": lineno,
                "tick": num(row, "tick"),
                "seconds": num(row, "seconds"),
                "player": num(row, "player"),
                "metal": metal,
                "energy": energy,
            })
    if not offenders:
        return []
    return [Finding(
        checker="invariant",
        severity="likely-bug",
        summary=f"I1: {len(offenders)} sample(s) with negative metal or energy",
        evidence={
            "file": str(csv_path),
            "count": len(offenders),
            "samples": offenders[:20],
        },
    )]


def rule_result_units(events_path, result, events_rows: list) -> list:
    if result is None:
        return []
    _, units = result

    alive: dict = {}
    alive_lines: dict = {}
    for lineno, row in events_rows:
        # The result line counts units under construction, and the events CSV
        # records every started unit, so alive here means merely not died.
        if num(row, "diedTick") is not None:
            continue
        player = num(row, "player")
        if player is None:
            continue
        alive[int(player)] = alive.get(int(player), 0) + 1
        alive_lines.setdefault(int(player), []).append(lineno)

    mismatches: list = []
    for player in sorted(units):
        if units[player] != alive.get(player, 0):
            mismatches.append({
                "player": player,
                "result_line_units": units[player],
                "events_csv_units": alive.get(player, 0),
                "events_csv_lines": alive_lines.get(player, []),
            })
    if not mismatches:
        return []
    return [Finding(
        checker="invariant",
        severity="likely-bug",
        summary="I2: result-line unit counts disagree with the events CSV",
        evidence={
            "file": str(events_path),
            "mismatches": mismatches,
        },
    )]


def _deciding_game(run_json_path, events_rows: list) -> bool:
    """True when the game is two-player, so a commander death decides it.

    In a game with more than two players, one player can be eliminated and the
    game still legitimately run to a timeout, so I3 does not apply.
    """
    path = Path(run_json_path) if run_json_path is not None else None
    if path is not None and path.exists():
        try:
            data = json.loads(path.read_text())
        except (OSError, ValueError):
            data = None
        if isinstance(data, dict) and isinstance(data.get("players"), list):
            return len(data["players"]) <= 2
    players = {num(row, "player") for _lineno, row in events_rows}
    players.discard(None)
    return len(players) <= 2


def rule_commander_timeout(events_path, result, events_rows: list, run_json_path=None) -> list:
    ended = result[0] if result is not None else None
    if ended is not None and ended != "timeout":
        return []
    if not _deciding_game(run_json_path, events_rows):
        return []

    dead = [
        (lineno, row) for lineno, row in events_rows
        if (row.get("category") or "") == "commander" and num(row, "diedTick") is not None
    ]
    if not dead:
        return []
    return [Finding(
        checker="invariant",
        severity="suspicious",
        summary=(
            f"I3: commander died but the run ended={ended or 'unknown'}"
        ),
        evidence={
            "file": str(events_path),
            "ended": ended,
            "deaths": [
                {
                    "line": lineno,
                    "player": num(row, "player"),
                    "unitType": row.get("unitType") or "",
                    "diedSeconds": num(row, "diedSeconds"),
                }
                for lineno, row in dead
            ],
        },
    )]


def _is_zero(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and value == 0


def rule_run_zero(run_json_path) -> list:
    path = Path(run_json_path)
    if not path.exists():
        return []
    try:
        data = json.loads(path.read_text())
    except (OSError, ValueError):
        return []
    if not isinstance(data, dict):
        return []
    duration = data.get("durationSeconds")
    ticks = data.get("simTicks")
    if _is_zero(duration) or _is_zero(ticks):
        return [Finding(
            checker="invariant",
            severity="suspicious",
            summary="I4: run.json records a zero-length run",
            evidence={
                "file": str(path),
                "durationSeconds": duration,
                "simTicks": ticks,
            },
        )]
    return []


def check(run_dir, context=None) -> list:
    run_dir = Path(run_dir)
    csv_path = run_dir / CSV_NAME
    events_path = run_dir / EVENTS_NAME
    log_path = run_dir / LOG_NAME
    run_json_path = run_dir / RUN_JSON_NAME

    sample_rows = read_rows(csv_path)
    result = parse_result(log_path)

    findings: list = []
    findings += rule_negative(csv_path, sample_rows)
    # Without the events table there is no alive count to compare against and
    # no record that a commander ever existed, so both rules sit out.
    if events_path.exists():
        events_rows = read_events(events_path)
        findings += rule_result_units(events_path, result, events_rows)
        findings += rule_commander_timeout(events_path, result, events_rows, run_json_path)
    findings += rule_run_zero(run_json_path)
    return findings
