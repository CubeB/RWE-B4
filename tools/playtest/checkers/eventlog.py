"""Shared reader for the structured event log (design §9).

``event-log.jsonl`` sits beside ``ai-arena.csv`` in each run dir, one JSON
object per line, and is written by the engine. The checkers that used to parse
prose log lines read it instead when it is present; this module owns the file
name and the skip-malformed-never-raise rule so that they agree on both.

Not a checker: nothing here is registered in ``CHECKERS``.
"""

from __future__ import annotations

import json
from pathlib import Path

EVENT_LOG_NAME = "event-log.jsonl"


def event_log_path(run_dir) -> Path:
    return Path(run_dir) / EVENT_LOG_NAME


def has_event_log(run_dir) -> bool:
    try:
        return event_log_path(run_dir).is_file()
    except OSError:
        return False


def read_events(run_dir) -> list:
    """Return ``[(line_number, event_dict), ...]``, dropping malformed lines.

    A blank line, an unparseable line or a JSON value that is not an object is
    skipped, never raised: the log may be truncated or mid-write when a checker
    reads it.
    """
    path = event_log_path(run_dir)
    events: list = []
    try:
        with open(path, errors="replace") as fh:
            for lineno, line in enumerate(fh, start=1):
                line = line.strip()
                if not line:
                    continue
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(event, dict):
                    events.append((lineno, event))
    except OSError:
        return []
    return events
