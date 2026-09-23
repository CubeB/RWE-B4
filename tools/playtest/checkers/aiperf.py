"""AI-profile checker (design §3.3): F1 an expensive pass, F2 a pass spiking
more than its budget, F3 drift against the closest earlier baseline.

Reads ``ai-profile.log`` when the runner has extracted it, otherwise the
``AI profile summary:`` lines inside ``game.log``. The engine clears its
per-pass counters every summary window, so the whole-game total and call count
for a pass is the sum across windows, and the mean is total/calls.

F3 is guarded: ``baselines`` is a sibling module that loads git state, and
this checker only uses it when it is importable and answering. Every failure
path degrades to "no baseline" rather than taking the batch down.
"""

from __future__ import annotations

import re
from pathlib import Path

from .economy import Finding, load_thresholds

try:  # a sibling module; absent or mid-write while the tree is shared
    import baselines  # type: ignore
except Exception:
    baselines = None

PROFILE_NAME = "ai-profile.log"
GAME_LOG_NAME = "game.log"

SUMMARY_RE = re.compile(
    r"AI profile summary: player (\d+) at tick (\d+), total ([\d.]+) ms:(.*)$"
)
# "<name>=<total>ms/<calls>" with the "(worst <ms>ms, <n> spikes)" suffix the
# engine emits; the suffix is optional so a hand-written or older line parses.
ITEM_RE = re.compile(
    r"([^\s=]+)=([\d.]+)(?:ms)?/(\d+)"
    r"(?:\s*\(worst\s+([\d.]+)ms,\s*(\d+)\s+spikes\))?"
)
_REPEAT_SUFFIX_RE = re.compile(r"-r\d+$")


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


def _parse_summaries(entries) -> list:
    """``[(lineno, player, {pass: (total_ms, calls, spikes)}), ...]``."""
    records = []
    for lineno, text in entries:
        m = SUMMARY_RE.search(text)
        if not m:
            continue
        try:
            player = int(m.group(1))
        except ValueError:
            continue
        passes = {}
        for name, total, calls, _worst, spikes in ITEM_RE.findall(m.group(4)):
            try:
                passes[name] = (float(total), int(calls), int(spikes) if spikes else 0)
            except (TypeError, ValueError):
                continue
        if passes:
            records.append((lineno, player, passes))
    return records


def aggregate(records) -> dict:
    """``{(player, pass): {"total_ms", "calls", "spikes", "rows", "file"}}``."""
    agg: dict = {}
    for lineno, player, passes in records:
        for name, (total, calls, spikes) in passes.items():
            key = (player, name)
            entry = agg.setdefault(key, {"total_ms": 0.0, "calls": 0, "spikes": 0, "rows": []})
            entry["total_ms"] += total
            entry["calls"] += calls
            entry["spikes"] += spikes
            entry["rows"].append(lineno)
    return agg


def rule_f1(profile_path, stats: dict, t: dict) -> list:
    th = t.get("aiperf", {})
    limit = th.get("f1_mean_ms", 5.0)

    findings = []
    for (player, name), entry in sorted(stats.items()):
        if entry["calls"] <= 0:
            continue
        mean = entry["total_ms"] / entry["calls"]
        if mean <= limit:
            continue
        findings.append(Finding(
            checker="aiperf",
            severity="suspicious",
            summary=(
                f"F1: player {player} pass {name} averages {mean:.3f} ms/call "
                f"over {entry['calls']} calls (limit {limit})"
            ),
            evidence={
                "file": str(profile_path),
                "rule": "F1",
                "player": player,
                "pass": name,
                "mean_ms": round(mean, 6),
                "total_ms": round(entry["total_ms"], 6),
                "calls": entry["calls"],
                "rows": entry["rows"],
                "threshold": {"f1_mean_ms": limit},
            },
        ))
    return findings


def rule_f2(profile_path, stats: dict, t: dict) -> list:
    th = t.get("aiperf", {})
    limit = th.get("f2_spikes", 50)

    findings = []
    for (player, name), entry in sorted(stats.items()):
        if entry["spikes"] <= limit:
            continue
        findings.append(Finding(
            checker="aiperf",
            severity="suspicious",
            summary=(
                f"F2: player {player} pass {name} spiked {entry['spikes']} times "
                f"(limit {limit})"
            ),
            evidence={
                "file": str(profile_path),
                "rule": "F2",
                "player": player,
                "pass": name,
                "spikes": entry["spikes"],
                "rows": entry["rows"],
                "threshold": {"f2_spikes": limit},
            },
        ))
    return findings


# --------------------------------------------------------------------------
# F3 baseline drift (guarded)
# --------------------------------------------------------------------------

def _get(obj, name, default=None):
    if obj is None:
        return default
    if isinstance(obj, dict):
        return obj.get(name, default)
    return getattr(obj, name, default)


def _arm_perf(base, arm):
    """The ``{pass_key: mean}`` table for ``arm`` in a baseline, or None.

    The baseline's exact shape is the sibling module's contract; accept the
    two shapes the design implies rather than assuming one.
    """
    arms = _get(base, "arms")
    if arms is not None:
        entry = arms.get(arm) if isinstance(arms, dict) else getattr(arms, arm, None)
        perf = _get(entry, "perf")
        if perf is not None:
            return perf
    perf = _get(base, "perf")
    if isinstance(perf, dict) and arm in perf:
        return perf[arm]
    return None


def _perf_mean(perf, key):
    value = _get(perf, key)
    if value is None:
        return None
    if isinstance(value, (int, float)):
        return float(value)
    mean = _get(value, "mean")
    if mean is None:
        mean = _get(value, "mean_ms")
    try:
        return float(mean)
    except (TypeError, ValueError):
        return None


def rule_f3(profile_path, stats: dict, t: dict, context) -> list:
    if baselines is None or context is None:
        return []
    scenario = getattr(context, "scenario", None)
    arm = getattr(context, "arm", None)
    if not scenario or not arm:
        return []

    ratio = t.get("aiperf", {}).get("f3_baseline_ratio", 1.5)
    base_arm = _REPEAT_SUFFIX_RE.sub("", arm)

    try:
        sha = baselines.current_sha()
    except Exception:
        return []
    if not sha:
        return []
    try:
        base = baselines.closest_baseline(scenario, sha)
    except Exception:
        return []
    if base is None:
        return []

    perf = _arm_perf(base, base_arm)
    if not perf:
        return []

    findings = []
    for (player, name), entry in sorted(stats.items()):
        if entry["calls"] <= 0:
            continue
        current = entry["total_ms"] / entry["calls"]
        base_mean = _perf_mean(perf, f"{player}:{name}")
        if base_mean is None or base_mean <= 0:
            continue
        if current <= base_mean * ratio:
            continue
        findings.append(Finding(
            checker="aiperf",
            severity="suspicious",
            summary=(
                f"F3: player {player} pass {name} averages {current:.3f} ms/call, "
                f"{current / base_mean:.2f}x the baseline {base_mean:.3f} ms/call"
            ),
            evidence={
                "file": str(profile_path),
                "rule": "F3",
                "player": player,
                "pass": name,
                "mean_ms": round(current, 6),
                "baseline_mean_ms": round(base_mean, 6),
                "baseline_arm": base_arm,
                "baseline_sha": sha,
                "ratio": round(current / base_mean, 4),
                "rows": entry["rows"],
                "threshold": {"f3_baseline_ratio": ratio},
            },
        ))
    return findings


def check(run_dir, context=None) -> list:
    t = _thresholds(context)
    profile_path, entries = read_source(run_dir)
    if profile_path is None:
        return []
    records = _parse_summaries(entries)
    if not records:
        return []
    stats = aggregate(records)
    findings: list = []
    findings += rule_f1(profile_path, stats, t)
    findings += rule_f2(profile_path, stats, t)
    findings += rule_f3(profile_path, stats, t, context)
    return findings
