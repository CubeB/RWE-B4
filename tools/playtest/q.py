#!/usr/bin/env python3
"""Query the structured event log (design §9) instead of grepping prose.

Usage::

    python3 tools/playtest/q.py <run_dir_or_run_root> [filters]

``<run_dir_or_run_root>`` may be a single run directory (the one holding
``event-log.jsonl``) or a run root whose scenario/``seed<N>-<arm>/`` directories
hold them; a run root scans every run under it. Each matching line is printed
prefixed with its run-relative id (``<scenario>/seed<N>-<arm>``). A malformed
line is skipped, never fatal.

Filters (all combinable):

    --ev <name[,name]>   keep events whose ``ev`` is any of these names
    --player <n>         keep events whose ``player`` is n
    --from <secs>        keep events with ``secs`` >= this
    --to <secs>          keep events with ``secs`` <= this
    --why <tag>          keep events whose ``why`` tag is this
    --grep <substr>      keep events whose ``detail`` contains this substring
    --level <lvl>        all, info (default) or trace -- see "Levels" below
    --count [FIELD]      print counts instead of lines: grouped by FIELD, or by
                         ``ev`` when FIELD is omitted, sorted by count descending
    --limit <n>          cap printed lines (or count groups); default 50

An explicit ``--ev``, ``--grep`` or ``--why`` shows whatever it matches even
if the level would otherwise hide it, so ``q.py --ev build_unaffordable``
still returns those events.

Stdlib only: json, argparse, pathlib.

Levels
======

``--level`` partitions the vocabulary; it is a selection, not a threshold:

    all     every event (what the writer kept, and what q.py printed before
            ``--level`` existed)
    info    every event except the trace-level ones (the default)
    trace   only the trace-level ones

The high-frequency decision events are trace: ``build_unaffordable``,
``build_refusal``, ``factory_hold``, ``factory_hold_t2``,
``transport_refusal``. The periodic ``ai_status`` and ``path_stats`` telemetry
stay info -- they are the backbone of a scan and are not a decision flood.

Event vocabulary
================

Every line is one JSON object. Envelope on every event:

    schema  int    schema version, currently 1
    secs    float  sim seconds at emission
    tick    int    sim tick at emission
    ev      str    event name (below)
    detail  str    verbatim prose reason; for judgement, not parsing

Event types and their own fields:

    path_stats     emitted every 300 sim ticks
        searches, expansions, suspended, abandoned, exhausted, relaxed,
        bugwalk, wasted_stand_in, wasted_other, worst, goalblocked,
        goalblocked_total, walkstuck, queued_per_tick, ticks_with_queue,
        ticks_total, deepest_ever, region_unreachable  (all numbers)

    ai_status      emitted every 30 sim seconds
        player, phase, metal_stalled, energy_stalled, metal_income,
        energy_income, metal_demand, energy_demand, idle_builders, army,
        known_enemies, focus_enemy, unreachable_ground, wants_transport,
        expansion_site, enemy_across_water, commander (string),
        types (object: unit type -> count)

    ai_transition
        player, kind, on, detail

    ai_perf
        player, pass, ms, calls, tick, kind ("spike"|"summary"), worst, spikes
        (only when the run was made with RWE_AI_PROFILE: ms is wall-clock, so
        profiler-gated logs are exempt from byte-determinism)

    decision events, ev = "<area>_<verb>", fields as applicable:
        player, subject, unit, x, z, target, cost, why (snake_case tag), detail.
        Names: build_order, build_refusal, build_site_contested,
        build_site_search, build_unaffordable, build_saving, build_not_worth, build_tier_assessment, build_builder_retreat,
        build_commander_mend, build_defence_lost, builder_gone,
        build_guard_request, factory_start, factory_refusal,
        factory_queue_cleared, transport_refusal, scout_assigned,
        army_commander_stays / _hands / _danger / _dgun, navy_sail,
        navy_recall, army_reinforce, ...

    factory_hold / factory_hold_t2   emit-on-change, not per tick:
        a hold spell emits once when it begins, again when the held subject
        changes, and once at its end (kind "drained"|"released"); held_for is
        the spell's length in ticks at emission, subject the held unit type.

    unit_death   per died unit (always on)
        unit, player, subject (type), x, z, cause, killer_type, killer_player,
        enemies_near, friendly_army_near, friendly_towers_near,
        nearest_enemy_type, frame, born. cause is the engine-distinguishable
        tag: weapon, self_destruct, reclaimed, carrier_died, unfinished;
        killer_type/killer_player are null when no attacker was known.

    ai_reachability   every 20 sim seconds
        player, kind ("ground"|"commander"|"naval"), on, changed.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

EVENT_LOG_NAME = "event-log.jsonl"

# The high-frequency decision events. They are written like every other event
# -- the log keeps everything -- but q.py's default level hides them so a scan
# sees the milestones, deaths and transitions first.
TRACE_EVENTS = frozenset({
    "build_unaffordable",
    "build_refusal",
    "factory_hold",
    "factory_hold_t2",
    "transport_refusal",
})


def run_id_for(run_dir: Path) -> str:
    """``<scenario>/seed<N>-<arm>`` from the run dir's path."""
    if run_dir.parent.name:
        return f"{run_dir.parent.name}/{run_dir.name}"
    return run_dir.name


def discover(path: Path) -> list:
    """``[(run_id, event_log_path), ...]``, deterministic order."""
    path = Path(path)
    direct = path / EVENT_LOG_NAME
    if direct.is_file():
        return [(run_id_for(path), direct)]
    if not path.is_dir():
        return []
    found = []
    for log in sorted(path.rglob(EVENT_LOG_NAME)):
        found.append((run_id_for(log.parent), log))
    return found


def _as_int(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _as_float(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _explicit_selection(args) -> bool:
    return bool(args.ev) or args.grep is not None or args.why is not None


def matches(event: dict, args) -> bool:
    if args.ev and event.get("ev") not in args.ev:
        return False
    if args.player is not None and _as_int(event.get("player")) != args.player:
        return False
    if args.from_secs is not None:
        secs = _as_float(event.get("secs"))
        if secs is None or secs < args.from_secs:
            return False
    if args.to_secs is not None:
        secs = _as_float(event.get("secs"))
        if secs is None or secs > args.to_secs:
            return False
    if args.why is not None and event.get("why") != args.why:
        return False
    if args.grep is not None and args.grep not in str(event.get("detail", "")):
        return False
    # An explicit selector asks for its matches by name, so the level does not
    # get to hide them.
    if not _explicit_selection(args):
        is_trace = event.get("ev") in TRACE_EVENTS
        if args.level == "info" and is_trace:
            return False
        if args.level == "trace" and not is_trace:
            return False
    return True


def iter_lines(sources):
    """Yield ``(run_id, lineno, raw_line, event)``, skipping malformed lines."""
    for run_id, log_path in sources:
        try:
            with open(log_path, errors="replace") as fh:
                for lineno, line in enumerate(fh, start=1):
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        event = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if isinstance(event, dict):
                        yield run_id, lineno, line, event
        except OSError:
            continue


def _group_key(event: dict, field: str):
    value = event.get(field)
    if isinstance(value, (dict, list)):
        value = json.dumps(value, sort_keys=True)
    return "<missing>" if value is None else value


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        prog="q.py",
        description="Query a run's event-log.jsonl (design §9).",
    )
    parser.add_argument("path", help="run dir or run root to scan")
    parser.add_argument("--ev", default=None,
                        help="comma-separated event names to keep")
    parser.add_argument("--player", type=int, default=None, help="player index")
    parser.add_argument("--from", dest="from_secs", type=float, default=None,
                        metavar="SECS", help="keep events at or after this sim time")
    parser.add_argument("--to", dest="to_secs", type=float, default=None,
                        metavar="SECS", help="keep events at or before this sim time")
    parser.add_argument("--why", default=None, help="keep events with this why tag")
    parser.add_argument("--grep", default=None, help="substring in detail")
    parser.add_argument("--level", choices=("all", "info", "trace"), default="info",
                        help="event level: all, info (default) or trace only")
    parser.add_argument("--count", nargs="?", const="ev", default=None,
                        metavar="FIELD",
                        help="print counts grouped by FIELD (default: ev)")
    parser.add_argument("--limit", type=int, default=50, help="max lines (default 50)")
    args = parser.parse_args(argv)

    if args.ev:
        args.ev = [name.strip() for name in args.ev.split(",") if name.strip()]
    else:
        args.ev = None

    sources = discover(Path(args.path))
    if not sources:
        print(f"no {EVENT_LOG_NAME} found under {args.path}", file=sys.stderr)
        return 1

    if args.count is not None:
        counts: dict = {}
        for _run_id, _lineno, _line, event in iter_lines(sources):
            if not matches(event, args):
                continue
            key = _group_key(event, args.count)
            counts[key] = counts.get(key, 0) + 1
        ordered = sorted(counts.items(), key=lambda kv: (-kv[1], str(kv[0])))
        for key, count in ordered[: max(0, args.limit)]:
            print(f"{count}\t{key}")
        return 0

    printed = 0
    for run_id, _lineno, line, event in iter_lines(sources):
        if not matches(event, args):
            continue
        if printed >= args.limit:
            break
        print(f"{run_id}: {line}")
        printed += 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
