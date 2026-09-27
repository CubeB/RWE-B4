"""Shared parsing for the AI-ARENA-RESULT line and the AiTuningProfile knob
tables, so a script that needs either does not re-derive it.

Two things live here because two tools already needed them independently
before this file existed: `arena-paired.py`'s per-player regex (moved here
verbatim, with the group order it always had, so scripts matching on
`m.group(4)` etc. keep working) and a knob-table parser for `ai-autotune.py`'s
startup validation ("does this knob name actually exist in
AiTuningProfile.cpp, so a typo fails fast instead of silently no-opping").

Plain stdlib, Python 3.8+.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional


# ---------------------------------------------------------------------------
# The AI-ARENA-RESULT line (written by AiArenaReport::write and, with
# "| ended=decided|timeout" appended, by GameScene_replay.cpp / ai_arena.cpp).
#
# Format (one player segment per player, N players, not just two):
#   AI-ARENA-RESULT ticks=<n> seconds=<n>
#     | p<i>=<SIDE> <alive|dead> units=<n> buildings=<n> army=<n> lost=<n>
#       metalIncome=<n> types=<a x n;b x n;...|->
#     ... (repeated per player)
#     | ended=<decided|timeout>
#
# PLAYER_LINE_RE is exactly arena-paired.py's original `pat`, extended with
# extra trailing groups (buildings/lost/metalIncome/types) appended AFTER the
# four it always had (player, side, status, units, ..., army as group 6) so
# existing group(1)/group(4)/group(6) callers are unaffected.
# ---------------------------------------------------------------------------
PLAYER_LINE_RE = re.compile(
    r"p(\d+)=(\S+) (\w+) units=(\d+) buildings=(\d+) army=(\d+)"
    r"(?: lost=(\d+))?(?: metalIncome=(-?\d+))?(?: types=(\S+))?"
)

HEADER_RE = re.compile(r"ticks=(\d+) seconds=(\d+)")
ENDED_RE = re.compile(r"\bended=(\w+)\b")


@dataclass
class PlayerResult:
    index: int
    side: str
    status: str  # "alive" or "dead"
    units: int
    buildings: int
    army: int
    lost: Optional[int] = None
    metal_income: Optional[int] = None
    types: Dict[str, int] = field(default_factory=dict)

    @property
    def alive(self) -> bool:
        return self.status == "alive"


@dataclass
class ArenaResult:
    ticks: Optional[int]
    seconds: Optional[int]
    ended: Optional[str]  # "decided", "timeout", or None if not present
    players: Dict[int, PlayerResult]
    raw: str


def _parse_types(text: Optional[str]) -> Dict[str, int]:
    counts: Dict[str, int] = {}
    if not text or text == "-":
        return counts
    for piece in text.split(";"):
        if not piece or "x" not in piece:
            continue
        name, _, count = piece.rpartition("x")
        try:
            counts[name] = int(count)
        except ValueError:
            continue
    return counts


def parse_arena_result_line(line: str) -> Optional[ArenaResult]:
    """Parses one AI-ARENA-RESULT log line into structured players.

    Returns None if the line does not contain AI-ARENA-RESULT at all.
    """
    if "AI-ARENA-RESULT" not in line:
        return None

    header = HEADER_RE.search(line)
    ticks = int(header.group(1)) if header else None
    seconds = int(header.group(2)) if header else None

    ended_m = ENDED_RE.search(line)
    ended = ended_m.group(1) if ended_m else None

    players: Dict[int, PlayerResult] = {}
    for m in PLAYER_LINE_RE.finditer(line):
        idx = int(m.group(1))
        players[idx] = PlayerResult(
            index=idx,
            side=m.group(2),
            status=m.group(3),
            units=int(m.group(4)),
            buildings=int(m.group(5)),
            army=int(m.group(6)),
            lost=int(m.group(7)) if m.group(7) is not None else None,
            metal_income=int(m.group(8)) if m.group(8) is not None else None,
            types=_parse_types(m.group(9)),
        )

    return ArenaResult(ticks=ticks, seconds=seconds, ended=ended, players=players, raw=line)


def last_result_line(path: str) -> Optional[str]:
    """The last AI-ARENA-RESULT line in a log file, or None if there isn't one."""
    last = None
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                if "AI-ARENA-RESULT" in line:
                    last = line
    except OSError:
        return None
    return last


def read_arena_result(path: str) -> Optional[ArenaResult]:
    line = last_result_line(path)
    if line is None:
        return None
    return parse_arena_result_line(line)


# ---------------------------------------------------------------------------
# AiTuningProfile.cpp's four knob tables. Parsed as text rather than compiled,
# so this works on a source checkout with no build step -- ai-autotune.py's
# whole point is to run only the frozen binaries, never to build.
#
# Each table looks like:
#   constexpr IntKnob intKnobs[] = {
#       {"name", &AiTuningProfile::field},
#       ...
#   };
# and applyAiTuning/listAiKnobs both read from exactly these four tables, so a
# name found here is a name the engine actually accepts.
# ---------------------------------------------------------------------------
_TABLE_RE = re.compile(
    r"constexpr\s+(Int|Bool|Float|Scalar)Knob\s+\w+\s*\[\]\s*=\s*\{(.*?)\}\s*;",
    re.DOTALL,
)
_ENTRY_NAME_RE = re.compile(r'\{\s*"([A-Za-z0-9_]+)"\s*,')

_TYPE_NAMES = {
    "Int": "int",
    "Bool": "bool",
    "Float": "float",
    "Scalar": "scalar",
}


def parse_knob_tables(source_text: str) -> Dict[str, str]:
    """Returns {knob_name: "int"|"bool"|"float"|"scalar"} from
    AiTuningProfile.cpp's source text (the four tables applyAiTuning and
    listAiKnobs both read from -- see AiTuningProfile.cpp's own comment that
    those two must never drift apart, which is exactly what makes this a
    faithful source of truth for "is this a real knob").
    """
    knobs: Dict[str, str] = {}
    for table_kind, body in _TABLE_RE.findall(source_text):
        kind = _TYPE_NAMES[table_kind]
        for name in _ENTRY_NAME_RE.findall(body):
            knobs[name] = kind
    return knobs


def load_knob_tables(source_path: str) -> Dict[str, str]:
    with open(source_path, encoding="utf-8") as fh:
        return parse_knob_tables(fh.read())
