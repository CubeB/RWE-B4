#!/usr/bin/env python3
"""Says whether an AI tuning change helped, by how much, how sure we are, and
how many more seeds would settle it.

``tools/arena-paired.py`` ends the loop "change a knob -> run tuned vs control
arena games -> decide" with a 31-line win tally and no statistics. This tool
replaces that last step: it pairs each tuned run with its control twin (same
scenario, same seed), reads the outcome and the per-run metrics both already
write, and reports a verdict with a confidence interval and a p-value instead
of a raw count.

    python tools/ai-experiment.py compare <root>...
    python tools/ai-experiment.py compare <root> --json out.json --metric-filter army_value_end,units_lost
    python tools/ai-experiment.py sweep <root> --knob navalAttackFleetSize
    python tools/ai-experiment.py plan --knob navalAttackFleetSize --values 1,3,5 \
        --seeds 1-8 --out /tmp/sweep.toml

Two run-root layouts are understood, because both are in active use:

  * The current one, from ``tools/playtest/run.py``:
    ``<root>/<scenario>/seed<N>-<arm>/`` holding ``ai-arena.csv``,
    ``ai-arena-events.csv``, ``run.json`` and ``game.log``, arms named
    ``tuned``/``control``. The tuned player is read from ``run.json``'s
    per-player ``tune`` object (whichever player index has a non-empty one).
  * The older one, from ``tools/ai-arena.ps1`` / ``tools/arena-paired.py``: a
    flat directory of ``game-<seed>.log`` (tuned) and
    ``game-<seed>-control.log`` (control), with ``game-<seed>[-control]-ai-
    arena[-events].csv`` beside them and no ``run.json``. There the tuned
    player alternates by the script's own convention: odd seeds tune player 0,
    even seeds tune player 1.

A run's OUTCOME (win/draw/loss for the tuned player) is read from its last
``AI-ARENA-RESULT`` line (``src/rwe/game/AiArenaReport.cpp``'s summary,
documented there and mirrored in ``build_command``'s callers). If the tuned
player's ``status`` is ``alive`` and every opponent's is ``dead``, that is a
win; the reverse is a loss. Otherwise neither player was eliminated (the game
hit its time limit, or both were wiped at once) and the result line's ``army``
unit count breaks the tie: a margin bigger than ``--draw-margin`` (default 2)
in the tuned player's favour is a win, the same margin against is a loss,
anything closer is a draw. This mirrors ``arena-paired.py``'s own tie-break,
which reads a unit-count margin off the same line.

Statistics (no numpy/scipy; implemented here):

  * Win rate treats a draw as half a win. Its 95% confidence interval is the
    Wilson score interval, which behaves at small n and at 0%/100% where the
    normal approximation does not.
  * The sign test is exact (binomial, p=0.5) over decisive (non-draw) pairs
    only, two-sided as ``2 * min(P(X<=k), P(X>=k))``, capped at 1.
  * Each metric's paired delta gets a 95% CI from a paired bootstrap over the
    per-pair (tuned - control) differences, fixed RNG seed (``--bootstrap-
    seed``, default 1234567) so a rerun on the same data reproduces the same
    interval.
  * The "how many more seeds" estimates are a normal approximation to the
    sample size for 80% power at the observed effect size (for the win rate,
    a one-sample-proportion-vs-0.5 formula; for the main metric, a paired-
    mean-difference formula using the observed standard deviation of the
    per-pair deltas). Both are said to be approximations in the output.

Metrics come from whatever ``ai-arena.csv`` / ``ai-arena-events.csv`` /
``run.json`` a run actually has. A run missing a column, or an old-format CSV
missing a required column entirely, reports that metric absent rather than
crashing -- old run roots must still read. Reuses
``tools/playtest/checkers/economy.py`` and ``.../production.py`` for
tolerant CSV/events reading, and ``tools/arena-analyse.py`` (loaded by path,
its name is not an importable identifier) for the milestone/checkpoint/
efficiency arithmetic, rather than duplicating either.

Plain-text output by default, aligned columns, no colour codes. Every
subcommand accepts ``--json PATH`` to additionally write the same figures as
JSON, alongside (not instead of) the plain-text report.
"""

from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import math
import random
import re
import subprocess
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
PLAYTEST_DIR = HERE / "playtest"

if str(PLAYTEST_DIR) not in sys.path:
    sys.path.insert(0, str(PLAYTEST_DIR))

import run as playtest_run  # noqa: E402  tools/playtest/run.py
from checkers import economy as eco_checker  # noqa: E402
from checkers import production as prod_checker  # noqa: E402


def _load_arena_analyse():
    """``tools/arena-analyse.py``, loaded by path since its name (the hyphen)
    is not an importable module identifier. Reused for MILESTONES,
    CHECKPOINTS_MIN, strip_side, mmss and analyse_player rather than
    reimplementing the milestone/checkpoint/efficiency arithmetic."""
    spec = importlib.util.spec_from_file_location("rwe_arena_analyse", HERE / "arena-analyse.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


arena_analyse = _load_arena_analyse()


# --------------------------------------------------------------------------
# statistics
# --------------------------------------------------------------------------

Z_ALPHA_2_95 = 1.959963984540054  # two-sided 95% (alpha=0.05)
Z_BETA_80 = 0.8416212335729143  # one-sided, 80% power


def wilson_interval(successes: float, n: int, z: float = Z_ALPHA_2_95) -> tuple:
    """The Wilson score interval for a proportion, at confidence given by z."""
    if n <= 0:
        return (0.0, 1.0)
    p = successes / n
    z2 = z * z
    denom = 1.0 + z2 / n
    center = (p + z2 / (2 * n)) / denom
    half = (z * math.sqrt((p * (1 - p) / n) + (z2 / (4 * n * n)))) / denom
    return (max(0.0, center - half), min(1.0, center + half))


def sign_test_two_sided(k: int, n: int) -> float:
    """Exact two-sided sign test p-value for k successes out of n trials at p=0.5."""
    if n <= 0:
        return 1.0
    total = 2 ** n
    tail_ge = sum(math.comb(n, i) for i in range(k, n + 1)) / total
    tail_le = sum(math.comb(n, i) for i in range(0, k + 1)) / total
    return min(1.0, 2.0 * min(tail_ge, tail_le))


def paired_bootstrap_ci(diffs: list, n_resamples: int = 10000, seed: int = 1234567, alpha: float = 0.05) -> tuple:
    """A percentile bootstrap CI for the mean of ``diffs``, reproducible under ``seed``."""
    n = len(diffs)
    if n == 0:
        return (None, None)
    if n == 1:
        return (diffs[0], diffs[0])
    rng = random.Random(seed)
    means = []
    for _ in range(n_resamples):
        total = 0.0
        for _ in range(n):
            total += diffs[rng.randrange(n)]
        means.append(total / n)
    means.sort()
    lo_idx = int((alpha / 2.0) * n_resamples)
    hi_idx = min(n_resamples - 1, int((1.0 - alpha / 2.0) * n_resamples))
    return (means[lo_idx], means[hi_idx])


def sample_stdev(values: list) -> float:
    n = len(values)
    if n < 2:
        return 0.0
    mean = sum(values) / n
    return math.sqrt(sum((v - mean) ** 2 for v in values) / (n - 1))


def power_n_for_proportion(p_hat: float, p0: float = 0.5, z_alpha2: float = Z_ALPHA_2_95, z_beta: float = Z_BETA_80):
    """Normal-approximation sample size for 80% power detecting p_hat against p0=0.5."""
    delta = abs(p_hat - p0)
    if delta <= 1e-9:
        return None
    numerator = (z_alpha2 * math.sqrt(p0 * (1 - p0)) + z_beta * math.sqrt(max(p_hat * (1 - p_hat), 1e-9))) ** 2
    return numerator / (delta * delta)


def power_n_for_paired_mean(mean_diff: float, sd_diff: float, z_alpha2: float = Z_ALPHA_2_95, z_beta: float = Z_BETA_80):
    """Normal-approximation sample size for 80% power on a paired mean difference."""
    if sd_diff <= 0 or mean_diff == 0:
        return None
    return ((z_alpha2 + z_beta) * sd_diff / mean_diff) ** 2


# --------------------------------------------------------------------------
# run discovery: two layouts, one Pair abstraction
# --------------------------------------------------------------------------

@dataclass
class RunLoc:
    run_id: str
    scenario: str
    seed: int
    arm: str  # "tuned" | "control"
    layout: str  # "new" | "old"
    log_path: Path
    csv_path: Path
    events_path: Path
    run_json_path: Path | None


@dataclass
class Pair:
    scenario: str
    seed: int
    tuned: RunLoc
    control: RunLoc
    tuned_player: int


def _runloc_new(scenario: str, seed: int, arm: str, run_dir: Path) -> RunLoc:
    return RunLoc(
        run_id=f"{scenario}/seed{seed}-{arm}", scenario=scenario, seed=seed, arm=arm, layout="new",
        log_path=run_dir / "game.log", csv_path=run_dir / "ai-arena.csv",
        events_path=run_dir / "ai-arena-events.csv", run_json_path=run_dir / "run.json",
    )


def discover_new_layout(root: Path) -> list:
    root = Path(root)
    if not root.is_dir():
        return []
    locs = []
    for scenario_dir in sorted(p for p in root.iterdir() if p.is_dir()):
        for run_dir in sorted(p for p in scenario_dir.iterdir() if p.is_dir()):
            m = playtest_run.RUN_DIR_RE.match(run_dir.name)
            if not m or m.group(2) not in ("tuned", "control"):
                continue
            locs.append(_runloc_new(scenario_dir.name, int(m.group(1)), m.group(2), run_dir))
    if locs:
        return locs
    # root may itself BE one scenario directory (its children are seed<N>-<arm> dirs directly).
    for run_dir in sorted(p for p in root.iterdir() if p.is_dir()):
        m = playtest_run.RUN_DIR_RE.match(run_dir.name)
        if not m or m.group(2) not in ("tuned", "control"):
            continue
        locs.append(_runloc_new(root.name, int(m.group(1)), m.group(2), run_dir))
    return locs


OLD_LOG_RE = re.compile(r"^game-(\d+)(-control)?\.log$")


def discover_old_layout(root: Path) -> list:
    root = Path(root)
    if not root.is_dir():
        return []
    scenario = root.name
    locs = []
    for path in sorted(root.glob("game-*.log")):
        m = OLD_LOG_RE.match(path.name)
        if not m:
            continue
        seed = int(m.group(1))
        arm = "control" if m.group(2) else "tuned"
        tag = f"game-{seed}" + ("-control" if m.group(2) else "")
        locs.append(RunLoc(
            run_id=f"{scenario}/seed{seed}-{arm}", scenario=scenario, seed=seed, arm=arm, layout="old",
            log_path=path, csv_path=root / f"{tag}-ai-arena.csv",
            events_path=root / f"{tag}-ai-arena-events.csv", run_json_path=None,
        ))
    return locs


def discover(root: Path) -> list:
    locs = discover_new_layout(root)
    if locs:
        return locs
    return discover_old_layout(root)


def _tuned_player_from_run_json(run_json_path: Path):
    try:
        data = json.loads(Path(run_json_path).read_text())
    except (OSError, ValueError):
        return None
    for p in data.get("players", []):
        if p.get("tune"):
            try:
                return int(p["index"])
            except (KeyError, TypeError, ValueError):
                continue
    return None


def build_pairs(locs: list) -> list:
    groups: dict = defaultdict(dict)
    for loc in locs:
        groups[(loc.scenario, loc.seed)][loc.arm] = loc

    pairs = []
    for (scenario, seed), arms in sorted(groups.items()):
        tuned = arms.get("tuned")
        control = arms.get("control")
        if tuned is None or control is None:
            continue
        tuned_player = None
        if tuned.layout == "new" and tuned.run_json_path is not None:
            tuned_player = _tuned_player_from_run_json(tuned.run_json_path)
        if tuned_player is None:
            # Old layout, or a new-layout run whose run.json didn't say: the
            # scripts' own convention, odd seeds tune p0, even seeds tune p1.
            tuned_player = 0 if seed % 2 == 1 else 1
        pairs.append(Pair(scenario, seed, tuned, control, tuned_player))
    return pairs


# --------------------------------------------------------------------------
# AI-ARENA-RESULT parsing and outcome classification
# --------------------------------------------------------------------------

RESULT_LINE_RE = re.compile(r"AI-ARENA-RESULT\s+ticks=(\d+)\s+seconds=(\d+)")
PLAYER_BLOCK_RE = re.compile(
    r"p(\d+)=(\S+)\s+(\w+)\s+units=(\d+)\s+buildings=(\d+)\s+army=(\d+)\s+lost=(\d+)\s+metalIncome=(-?\d+)"
)


def parse_result_line(line: str):
    m = RESULT_LINE_RE.search(line)
    if not m:
        return None
    result = {"ticks": int(m.group(1)), "seconds": int(m.group(2)), "players": {}}
    for pm in PLAYER_BLOCK_RE.finditer(line):
        idx = int(pm.group(1))
        result["players"][idx] = {
            "side": pm.group(2), "status": pm.group(3),
            "units": int(pm.group(4)), "buildings": int(pm.group(5)),
            "army": int(pm.group(6)), "lost": int(pm.group(7)), "metalIncome": int(pm.group(8)),
        }
    return result if result["players"] else None


def find_last_result(log_path: Path):
    last = None
    try:
        with open(log_path, errors="replace") as fh:
            for line in fh:
                if "AI-ARENA-RESULT" in line:
                    parsed = parse_result_line(line)
                    if parsed:
                        last = parsed
    except OSError:
        return None
    return last


DEFAULT_DRAW_MARGIN = 2


def classify_outcome(result, tuned_player: int, draw_margin: int = DEFAULT_DRAW_MARGIN):
    """win/draw/loss for tuned_player, or None if the result can't say. See the
    module docstring's "A run's OUTCOME" paragraph for the rule."""
    if result is None:
        return None
    players = result.get("players", {})
    if tuned_player not in players:
        return None
    others = [idx for idx in players if idx != tuned_player]
    if not others:
        return None
    tuned = players[tuned_player]
    if tuned["status"] == "alive" and all(players[o]["status"] == "dead" for o in others):
        return "win"
    if tuned["status"] == "dead" and any(players[o]["status"] == "alive" for o in others):
        return "loss"
    margin = tuned["army"] - max(players[o]["army"] for o in others)
    if margin > draw_margin:
        return "win"
    if margin < -draw_margin:
        return "loss"
    return "draw"


# --------------------------------------------------------------------------
# per-run metric bundle (reuses the checkers' tolerant CSV reading and
# arena-analyse's milestone/checkpoint/efficiency arithmetic)
# --------------------------------------------------------------------------

@dataclass
class RunBundle:
    samples: list  # this player's ai-arena.csv rows, plain dicts, chronological
    events: list  # this player's ai-arena-events.csv rows
    all_events: list  # every player's events, for kill counting
    player_index: int
    analysis: dict | None
    duration_s: float | None


def build_bundle(loc: RunLoc, player_index: int) -> RunBundle:
    samples_all = eco_checker.read_rows(loc.csv_path)
    events_all = prod_checker.read_events(loc.events_path)

    samples = [row for _, row in samples_all if eco_checker.num(row, "player") == player_index]
    samples.sort(key=lambda r: eco_checker.num(r, "tick", 0.0) or 0.0)
    my_events = [row for _, row in events_all if eco_checker.num(row, "player") == player_index]
    all_events_plain = [row for _, row in events_all]

    duration_s = None
    if loc.run_json_path is not None and Path(loc.run_json_path).exists():
        try:
            data = json.loads(Path(loc.run_json_path).read_text())
            duration_s = data.get("durationSeconds")
        except (OSError, ValueError):
            duration_s = None
    if duration_s is None and samples:
        duration_s = eco_checker.num(samples[-1], "seconds")

    analysis = None
    if samples:
        try:
            analysis = arena_analyse.analyse_player(samples, my_events)
        except (KeyError, ValueError, ZeroDivisionError, IndexError, TypeError):
            analysis = None

    return RunBundle(samples=samples, events=my_events, all_events=all_events_plain,
                      player_index=player_index, analysis=analysis, duration_s=duration_s)


def _checkpoint(bundle: RunBundle, minute: int, key: str):
    if not bundle.analysis:
        return None
    for c in bundle.analysis.get("checkpoints", []):
        if c["minute"] == minute:
            return c.get(key)
    return None


def _end(bundle: RunBundle, csv_key: str):
    if not bundle.samples:
        return None
    return arena_analyse.f(bundle.samples[-1], csv_key)


def _milestone_min(bundle: RunBundle, labels: list):
    if not bundle.analysis:
        return None
    times = []
    for label in labels:
        m = bundle.analysis["milestones"].get(label)
        if m and m["first"] is not None:
            times.append(m["first"])
    return min(times) if times else None


def _nth_extractor(bundle: RunBundle, n: int = 3):
    if not bundle.events:
        return None
    done = sorted(
        float(e["completedSeconds"]) for e in bundle.events
        if e.get("completedSeconds") and arena_analyse.strip_side(e.get("unitType", "")) == "MEX"
    )
    return done[n - 1] if len(done) >= n else None


def _commander_survived(bundle: RunBundle):
    if not bundle.events:
        return None
    for e in bundle.events:
        if (e.get("category") or "") == "commander" and e.get("diedSeconds"):
            return 0.0
    return 1.0


def _units_killed(bundle: RunBundle):
    if not bundle.all_events:
        return None
    seen_column = False
    count = 0
    for e in bundle.all_events:
        if "killerPlayer" not in e:
            continue
        seen_column = True
        kp = e.get("killerPlayer")
        if kp in (None, ""):
            continue
        try:
            if int(float(kp)) == bundle.player_index:
                count += 1
        except ValueError:
            continue
    return float(count) if seen_column else None


def _efficiency(bundle: RunBundle, key: str):
    if not bundle.analysis:
        return None
    return bundle.analysis.get("efficiency", {}).get(key)


METRICS = [
    ("army_value_5min", "army value @5m", lambda b: _checkpoint(b, 5, "armyMetal")),
    ("army_value_10min", "army value @10m", lambda b: _checkpoint(b, 10, "armyMetal")),
    ("army_value_15min", "army value @15m", lambda b: _checkpoint(b, 15, "armyMetal")),
    ("army_value_end", "army value @end", lambda b: _end(b, "armyMetal")),
    ("army_units_5min", "army units @5m", lambda b: _checkpoint(b, 5, "army")),
    ("army_units_10min", "army units @10m", lambda b: _checkpoint(b, 10, "army")),
    ("army_units_15min", "army units @15m", lambda b: _checkpoint(b, 15, "army")),
    ("army_units_end", "army units @end", lambda b: _end(b, "army")),
    ("metal_income_5min", "metal income @5m", lambda b: _checkpoint(b, 5, "metalIncome")),
    ("metal_income_10min", "metal income @10m", lambda b: _checkpoint(b, 10, "metalIncome")),
    ("metal_income_15min", "metal income @15m", lambda b: _checkpoint(b, 15, "metalIncome")),
    ("energy_income_5min", "energy income @5m", lambda b: _checkpoint(b, 5, "energyIncome")),
    ("energy_income_10min", "energy income @10m", lambda b: _checkpoint(b, 10, "energyIncome")),
    ("energy_income_15min", "energy income @15m", lambda b: _checkpoint(b, 15, "energyIncome")),
    ("metal_stalled_pct", "% metal-stalled", lambda b: _efficiency(b, "metalStalled")),
    ("energy_stalled_pct", "% energy-stalled", lambda b: _efficiency(b, "energyStalled")),
    ("energy_wasted_pct", "energy wasted %", lambda b: _efficiency(b, "energyWastedPct")),
    ("energy_capped_pct", "% samples energy at cap", lambda b: _efficiency(b, "energyAtCap")),
    ("metal_wasted_pct", "metal wasted %", lambda b: _efficiency(b, "metalWastedPct")),
    ("first_factory_s", "time to 1st factory (s)", lambda b: _milestone_min(b, ["lab", "veh plant", "air plant", "shipyard"])),
    ("first_t2_factory_s", "time to 1st T2 factory (s)", lambda b: _milestone_min(b, ["adv lab", "adv shipyard"])),
    ("extractor_3_s", "time to 3rd extractor (s)", lambda b: _nth_extractor(b, 3)),
    ("units_lost", "units lost", lambda b: _end(b, "unitsLost")),
    ("buildings_lost", "buildings lost", lambda b: _end(b, "buildingsLost")),
    ("units_killed", "units killed", _units_killed),
    ("commander_survived", "commander survived", _commander_survived),
]
METRIC_LOOKUP = {key: (label, fn) for key, label, fn in METRICS}


# --------------------------------------------------------------------------
# compare
# --------------------------------------------------------------------------

def _collect(pairs: list, draw_margin: int, metric_filter):
    per_pair = []
    wins = draws = losses = 0
    decisive = []
    metric_pairs: dict = defaultdict(list)

    for pair in pairs:
        result = find_last_result(pair.tuned.log_path)
        outcome = classify_outcome(result, pair.tuned_player, draw_margin)
        if outcome == "win":
            wins += 1
            decisive.append("win")
        elif outcome == "loss":
            losses += 1
            decisive.append("loss")
        elif outcome == "draw":
            draws += 1

        tuned_side = None
        if result and pair.tuned_player in result["players"]:
            tuned_side = result["players"][pair.tuned_player]["side"]

        tuned_bundle = build_bundle(pair.tuned, pair.tuned_player)
        control_bundle = build_bundle(pair.control, pair.tuned_player)

        for key, _label, fn in METRICS:
            if metric_filter and key not in metric_filter:
                continue
            tv = fn(tuned_bundle)
            cv = fn(control_bundle)
            if tv is None or cv is None:
                continue
            metric_pairs[key].append((tv, cv))

        per_pair.append({
            "scenario": pair.scenario, "seed": pair.seed, "tuned_player": pair.tuned_player,
            "tuned_side": tuned_side, "outcome": outcome, "run_id": pair.tuned.run_id,
        })

    return per_pair, wins, draws, losses, decisive, metric_pairs


def _metric_table(metric_pairs: dict, bootstrap_seed: int) -> list:
    rows = []
    for key, _label, _fn in METRICS:
        pairs = metric_pairs.get(key)
        if not pairs:
            continue
        tvals = [tv for tv, _cv in pairs]
        cvals = [cv for _tv, cv in pairs]
        diffs = [tv - cv for tv, cv in pairs]
        n = len(pairs)
        mean_t = sum(tvals) / n
        mean_c = sum(cvals) / n
        mean_delta = sum(diffs) / n
        ci_lo, ci_hi = paired_bootstrap_ci(diffs, seed=bootstrap_seed)
        excludes_zero = (ci_lo is not None) and not (ci_lo <= 0.0 <= ci_hi)
        rows.append({
            "key": key, "label": METRIC_LOOKUP[key][0], "n": n,
            "mean_tuned": mean_t, "mean_control": mean_c, "mean_delta": mean_delta,
            "ci_lo": ci_lo, "ci_hi": ci_hi, "excludes_zero": excludes_zero,
            "sd_delta": sample_stdev(diffs),
        })
    return rows


def _pick_main_metric(metric_rows: list, requested: str | None):
    if requested:
        for row in metric_rows:
            if row["key"] == requested:
                return row
        return None
    for preferred in ("army_value_end", "army_units_end"):
        for row in metric_rows:
            if row["key"] == preferred:
                return row
    excl = [row for row in metric_rows if row["excludes_zero"]]
    if excl:
        return max(excl, key=lambda r: r["n"])
    return metric_rows[0] if metric_rows else None


def cmd_compare(args) -> int:
    metric_filter = None
    if args.metric_filter:
        metric_filter = {m.strip() for m in args.metric_filter.split(",") if m.strip()}

    all_pairs = []
    for root in args.roots:
        root_path = Path(root)
        locs = discover(root_path)
        if not locs:
            print(f"warning: no runs found under {root_path}", file=sys.stderr)
            continue
        pairs = build_pairs(locs)
        if not pairs:
            print(f"warning: no tuned/control pairs under {root_path}", file=sys.stderr)
        all_pairs += pairs

    if not all_pairs:
        print("no tuned/control pairs found in any root", file=sys.stderr)
        return 1

    per_pair, wins, draws, losses, decisive, metric_pairs = _collect(all_pairs, args.draw_margin, metric_filter)
    n_total = wins + draws + losses
    win_rate = (wins + 0.5 * draws) / n_total if n_total else 0.0
    wilson_lo, wilson_hi = wilson_interval(wins + 0.5 * draws, n_total) if n_total else (0.0, 1.0)
    n_decisive = len(decisive)
    sign_p = sign_test_two_sided(decisive.count("win"), n_decisive) if n_decisive else 1.0

    metric_rows = _metric_table(metric_pairs, args.bootstrap_seed)
    main_metric = _pick_main_metric(metric_rows, args.main_metric)

    if n_total == 0:
        direction = "undetermined (no pair produced a parseable result)"
    elif wilson_lo > 0.5:
        direction = "better"
    elif wilson_hi < 0.5:
        direction = "worse"
    else:
        direction = "no detectable difference"
    verdict = (
        f"tuned {direction} at n={n_total} pairs (win rate {win_rate:.0%}, "
        f"95% CI [{wilson_lo:.0%}, {wilson_hi:.0%}], sign test p={sign_p:.4f} over {n_decisive} decisive pairs)"
    )

    power = {}
    needed = power_n_for_proportion(win_rate)
    power["win_rate"] = {
        "needed_pairs": None if needed is None else math.ceil(needed),
        "additional_pairs": None if needed is None else max(0, math.ceil(needed) - n_total),
    }
    if main_metric is not None:
        needed_m = power_n_for_paired_mean(main_metric["mean_delta"], main_metric["sd_delta"])
        power["main_metric"] = {
            "key": main_metric["key"],
            "needed_pairs": None if needed_m is None else math.ceil(needed_m),
            "additional_pairs": None if needed_m is None else max(0, math.ceil(needed_m) - main_metric["n"]),
        }
    else:
        power["main_metric"] = None

    _print_compare(per_pair, wins, draws, losses, win_rate, wilson_lo, wilson_hi, sign_p, n_decisive,
                    metric_rows, verdict, power, main_metric)

    if args.json:
        payload = {
            "pairs": per_pair,
            "tally": {"wins": wins, "draws": draws, "losses": losses, "n": n_total,
                      "win_rate": win_rate, "wilson_ci": [wilson_lo, wilson_hi],
                      "sign_test_p": sign_p, "n_decisive": n_decisive},
            "metrics": metric_rows,
            "verdict": verdict,
            "power": power,
        }
        Path(args.json).write_text(json.dumps(payload, indent=2, default=str))
        print(f"json written to {args.json}")

    return 0


def _print_compare(per_pair, wins, draws, losses, win_rate, wilson_lo, wilson_hi, sign_p, n_decisive,
                    metric_rows, verdict, power, main_metric) -> None:
    print("per-pair outcomes:")
    cols = ["scenario", "seed", "tuned_player", "tuned_side", "outcome"]
    widths = {c: max(len(c), max((len(str(row.get(c, ""))) for row in per_pair), default=0)) for c in cols}
    print("  " + "  ".join(c.ljust(widths[c]) for c in cols))
    for row in per_pair:
        print("  " + "  ".join(str(row.get(c, "")).ljust(widths[c]) for c in cols))
    print()
    print(f"W/D/L: {wins}/{draws}/{losses}  win rate {win_rate:.1%}  95% Wilson CI [{wilson_lo:.1%}, {wilson_hi:.1%}]"
          f"  sign test p={sign_p:.4f} (n={n_decisive} decisive)")
    print()

    if metric_rows:
        print("metrics (tuned vs control, paired):")
        headers = ["metric", "n", "mean tuned", "mean control", "mean delta", "95% CI", ""]
        rows_fmt = []
        for r in metric_rows:
            mark = "*" if r["excludes_zero"] else ""
            rows_fmt.append([
                r["label"], str(r["n"]), f"{r['mean_tuned']:.2f}", f"{r['mean_control']:.2f}",
                f"{r['mean_delta']:+.2f}", f"[{r['ci_lo']:+.2f}, {r['ci_hi']:+.2f}]", mark,
            ])
        widths2 = [max(len(headers[i]), max((len(row[i]) for row in rows_fmt), default=0)) for i in range(len(headers))]
        print("  " + "  ".join(headers[i].ljust(widths2[i]) for i in range(len(headers))))
        for row in rows_fmt:
            print("  " + "  ".join(row[i].ljust(widths2[i]) for i in range(len(headers))))
        print("  (* marks a row whose 95% CI excludes 0)")
        print()
    else:
        print("metrics: none of the requested metrics could be read from these runs")
        print()

    print("verdict: " + verdict)
    wr = power.get("win_rate", {})
    if wr.get("needed_pairs") is None:
        print("power: win rate effect is ~0; no finite number of pairs would reach 80% power (normal approximation)")
    else:
        print(f"power: win rate needs ~{wr['needed_pairs']} pairs for 80% power at the observed effect "
              f"(normal approximation) -- {wr['additional_pairs']} more than the {wins + draws + losses} run")
    mm = power.get("main_metric")
    if main_metric is not None and mm and mm.get("needed_pairs") is not None:
        print(f"power: {main_metric['label']} needs ~{mm['needed_pairs']} pairs for 80% power at the observed "
              f"effect (normal approximation) -- {mm['additional_pairs']} more than the {main_metric['n']} run")
    elif main_metric is not None:
        print(f"power: {main_metric['label']}'s observed effect is ~0; no finite number of pairs would reach "
              f"80% power (normal approximation)")


# --------------------------------------------------------------------------
# sweep
# --------------------------------------------------------------------------

def _bar(fraction: float, width: int = 20) -> str:
    fraction = max(0.0, min(1.0, fraction))
    filled = int(round(fraction * width))
    return "#" * filled + "-" * (width - filled)


def cmd_sweep(args) -> int:
    root = Path(args.root)
    locs = discover_new_layout(root)
    if not locs:
        print(f"error: sweep needs the run.py layout (with run.json) to know each run's knob value; "
              f"found none under {root}", file=sys.stderr)
        return 1
    pairs = build_pairs(locs)
    if not pairs:
        print(f"no tuned/control pairs under {root}", file=sys.stderr)
        return 1

    groups: dict = defaultdict(list)
    skipped = 0
    for pair in pairs:
        if pair.tuned.run_json_path is None or not pair.tuned.run_json_path.exists():
            skipped += 1
            continue
        try:
            data = json.loads(pair.tuned.run_json_path.read_text())
        except (OSError, ValueError):
            skipped += 1
            continue
        tune = None
        for p in data.get("players", []):
            if int(p.get("index", -1)) == pair.tuned_player:
                tune = p.get("tune") or {}
                break
        if not tune:
            skipped += 1
            continue
        if args.knob:
            if args.knob not in tune:
                skipped += 1
                continue
            value = tune[args.knob]
        else:
            if len(tune) != 1:
                skipped += 1
                continue
            value = next(iter(tune.values()))
        groups[str(value)].append(pair)

    if not groups:
        print("no runs carried a recognisable tuned value (pass --knob?)", file=sys.stderr)
        return 1

    key_metric = args.main_metric or "army_value_end"
    summary = []
    for value, value_pairs in groups.items():
        _per_pair, wins, draws, losses, decisive, metric_pairs = _collect(value_pairs, args.draw_margin, {key_metric})
        n = wins + draws + losses
        win_rate = (wins + 0.5 * draws) / n if n else 0.0
        lo, hi = wilson_interval(wins + 0.5 * draws, n) if n else (0.0, 1.0)
        metric_rows = _metric_table(metric_pairs, args.bootstrap_seed)
        mean_delta = metric_rows[0]["mean_delta"] if metric_rows else None
        summary.append({
            "value": value, "n": n, "wins": wins, "draws": draws, "losses": losses,
            "win_rate": win_rate, "wilson_ci": [lo, hi], "mean_delta": mean_delta,
            "metric": key_metric,
        })

    def sort_key(row):
        try:
            return (0, float(row["value"]))
        except ValueError:
            return (1, row["value"])
    summary.sort(key=sort_key)

    best = max(summary, key=lambda r: r["win_rate"]) if summary else None

    print(f"sweep over knob values under {root}  (key metric: {key_metric}; {skipped} runs skipped, no recognisable tune)")
    print()
    headers = ["value", "n", "W/D/L", "win rate", "95% CI", f"mean delta ({key_metric})", "bar", "best"]
    rows_fmt = []
    for row in summary:
        wdl = f"{row['wins']}/{row['draws']}/{row['losses']}"
        delta = "-" if row["mean_delta"] is None else f"{row['mean_delta']:+.2f}"
        mark = "<-- best" if best is not None and row["value"] == best["value"] else ""
        rows_fmt.append([
            row["value"], str(row["n"]), wdl, f"{row['win_rate']:.1%}",
            f"[{row['wilson_ci'][0]:.1%}, {row['wilson_ci'][1]:.1%}]", delta, _bar(row["win_rate"]), mark,
        ])
    widths = [max(len(headers[i]), max((len(r[i]) for r in rows_fmt), default=0)) for i in range(len(headers))]
    print("  " + "  ".join(headers[i].ljust(widths[i]) for i in range(len(headers))))
    for row in rows_fmt:
        print("  " + "  ".join(row[i].ljust(widths[i]) for i in range(len(headers))))

    if args.json:
        Path(args.json).write_text(json.dumps({"knob": args.knob, "key_metric": key_metric, "values": summary,
                                                 "best_value": best["value"] if best else None,
                                                 "skipped": skipped}, indent=2, default=str))
        print(f"json written to {args.json}")
    return 0


# --------------------------------------------------------------------------
# plan
# --------------------------------------------------------------------------

def parse_seed_range(spec: str) -> list:
    seeds = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            seeds.extend(range(int(a), int(b) + 1))
        else:
            seeds.append(int(part))
    return seeds


def _list_knobs_via_binary(binary):
    if not binary:
        return None
    try:
        proc = subprocess.run([str(binary), "--list-knobs"], capture_output=True, text=True, timeout=15)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if proc.returncode != 0 or not proc.stdout.strip():
        return None
    names = set()
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        names.add(line.split("\t", 1)[0])
    return names or None


def _knob_in_source(name: str) -> bool:
    path = REPO_ROOT / "src" / "rwe" / "ai" / "AiTuningProfile.cpp"
    try:
        text = path.read_text()
    except OSError:
        return False
    return f'"{name}"' in text


def validate_knob(name: str, binary) -> bool:
    names = _list_knobs_via_binary(binary)
    if names is not None:
        return name in names
    return _knob_in_source(name)


def _toml_value(raw: str) -> str:
    if raw.lower() in ("true", "false"):
        return raw.lower()
    try:
        int(raw)
        return raw
    except ValueError:
        pass
    try:
        float(raw)
        return raw
    except ValueError:
        pass
    return json.dumps(raw)


def cmd_plan(args) -> int:
    if not validate_knob(args.knob, args.binary):
        print(f"error: unknown AI tuning knob {args.knob!r} -- an invented knob aborts every tuned run at "
              f"startup (issue #252)", file=sys.stderr)
        return 1

    values = [v.strip() for v in args.values.split(",") if v.strip()]
    if not values:
        print("error: --values must name at least one value", file=sys.stderr)
        return 1
    seeds = parse_seed_range(args.seeds) if args.seeds else [1, 2, 3, 4, 5, 6]
    sides = [s.strip() for s in args.sides.split(",")] if args.sides else ["ARM", "CORE"]
    if len(sides) != 2:
        print("error: --sides needs exactly two comma-separated sides", file=sys.stderr)
        return 1

    lines = [f"# Generated by tools/ai-experiment.py plan --knob {args.knob} --values {args.values}", ""]
    scenario_ids = []
    for value in values:
        sid = f"sweep-{args.knob}-{value}"
        scenario_ids.append(sid)
        # tune, and everything else belonging to [[scenarios]] itself, must come
        # BEFORE any [[scenarios.players]] sub-table: once a [[scenarios.players]]
        # header opens, TOML attaches a later bare "key = value" to that player
        # entry rather than back to the scenario, which silently drops the tune
        # table off a naive append-after-players ordering.
        lines += [
            "[[scenarios]]",
            f'id = "{sid}"',
            f'map = "{args.map}"',
            f"duration_s = {args.duration}",
            f"seeds = {seeds!r}",
            f'tune = {{ "{args.player}" = {{ {args.knob} = {_toml_value(value)} }} }}',
            "",
            "[[scenarios.players]]",
            'name = "A"',
            f'side = "{sides[0]}"',
            "colour = 0",
            'difficulty = "standard"',
            "",
            "[[scenarios.players]]",
            'name = "B"',
            f'side = "{sides[1]}"',
            "colour = 1",
            'difficulty = "standard"',
            "",
        ]
    text = "\n".join(lines) + "\n"

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(text)
    print(f"wrote {out_path} ({len(values)} scenarios, {len(seeds)} seeds each)")
    for sid in scenario_ids:
        print(f"  {sid}")

    if args.json:
        Path(args.json).write_text(json.dumps({
            "knob": args.knob, "values": values, "seeds": seeds, "map": args.map,
            "sides": sides, "duration_s": args.duration, "player": args.player,
            "out": str(out_path), "scenario_ids": scenario_ids,
        }, indent=2))
        print(f"json written to {args.json}")
    return 0


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Evaluate a tuned-vs-control AI experiment: verdict, confidence, and how many more seeds "
                     "would settle it.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_compare = sub.add_parser("compare", help="pair tuned/control runs under one or more roots and report a verdict")
    p_compare.add_argument("roots", nargs="+", help="run root(s) (new run.py layout or old flat ai-arena.ps1 layout)")
    p_compare.add_argument("--json", default=None, help="also write the figures as JSON to this path")
    p_compare.add_argument("--metric-filter", default=None, help="comma-separated metric keys to keep (default: all)")
    p_compare.add_argument("--draw-margin", type=int, default=DEFAULT_DRAW_MARGIN,
                            help="army-unit-count margin below which an undecided result is a draw (default 2)")
    p_compare.add_argument("--bootstrap-seed", type=int, default=1234567, help="RNG seed for the paired bootstrap CI")
    p_compare.add_argument("--main-metric", default=None,
                            help="metric key to use for the power estimate (default: army_value_end, or the "
                                 "largest metric whose CI excludes 0)")
    p_compare.set_defaults(func=cmd_compare)

    p_sweep = sub.add_parser("sweep", help="group runs under one root by a tuned knob's value")
    p_sweep.add_argument("root", help="run root (new run.py layout; needs run.json)")
    p_sweep.add_argument("--knob", default=None,
                          help="which knob to group by (default: the run's sole tuned knob, if there's only one)")
    p_sweep.add_argument("--main-metric", default=None, help="metric key to show as the mean delta (default: army_value_end)")
    p_sweep.add_argument("--draw-margin", type=int, default=DEFAULT_DRAW_MARGIN)
    p_sweep.add_argument("--bootstrap-seed", type=int, default=1234567)
    p_sweep.add_argument("--json", default=None, help="also write the figures as JSON to this path")
    p_sweep.set_defaults(func=cmd_sweep)

    p_plan = sub.add_parser("plan", help="write a matrix.toml sweep for a knob over several values")
    p_plan.add_argument("--knob", required=True, help="the AI tuning knob to sweep")
    p_plan.add_argument("--values", required=True, help="comma-separated values to sweep")
    p_plan.add_argument("--seeds", default="1-6", help="seed range/list, e.g. 1-8 or 1,2,5 (default 1-6)")
    p_plan.add_argument("--map", default="Coast To Coast")
    p_plan.add_argument("--sides", default="ARM,CORE", help="two comma-separated sides (default ARM,CORE)")
    p_plan.add_argument("--duration", type=int, default=900, help="duration_s per scenario (default 900)")
    p_plan.add_argument("--player", default="0", help="player index (as a string key) the knob applies to (default 0)")
    p_plan.add_argument("--out", default="matrix.toml", help="where to write the matrix TOML (default matrix.toml)")
    p_plan.add_argument("--binary", default=None, help="ai_arena binary to try --list-knobs against")
    p_plan.add_argument("--json", default=None, help="also write a JSON summary of the plan to this path")
    p_plan.set_defaults(func=cmd_plan)

    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
