#!/usr/bin/env python3
"""Automatic arena tuning of the skirmish AI's knobs (issue #390, milestone 1).

Searches a small, hand-picked subset of AiTuningProfile.cpp's ~270 knobs for
values that beat the untuned AI on the paired-seed arena harness
(tools/ai-arena.ps1/.sh, tools/arena-paired.py), screening many candidates on
few seeds/short games and promoting the best to more seeds/longer games
(successive halving), with a mandatory 1v1v1v1 confirmation of the final
winner before it is proposed as a new default.

    tools/ai-autotune.py --exe <ai_arena.exe> --data-path <dir> --out <dir>

Design notes, and why each exists:

  Knob spec (--spec, default ai-autotune-knobs.toml) is validated against
  AiTuningProfile.cpp's own knob tables at startup (arena_common.
  parse_knob_tables), so a typo'd knob name fails immediately instead of
  --ai-tune silently no-opping fifty games into a search.

  Every candidate is scored by PAIRED games, reusing arena-paired.py's own
  parsing (arena_common.PLAYER_LINE_RE, which arena-paired.py now imports
  rather than re-deriving) and its margin/verdict convention (deadband 10):
  the same seed is played once with the candidate applied to one seat
  ("tuned") and once with no tune anywhere ("control"), and the change is
  judged tuned-margin-minus-control-margin, not tuned-vs-control directly --
  exactly the trap ai-arena.ps1's header describes ("a knob that looked like
  a rout and turned out to reproduce identically with no knob applied").
  Control games don't depend on the candidate, so one control game per
  (scenario, seed, seconds) is cached and reused across every candidate
  tested at that seed -- roughly halving the games a search needs to run.

  Fitness is the mean paired margin delta, PLUS a bonus for the tuned arm
  reaching a decided (commander-kill) ending more often than its control, and
  a small penalty for wasting more metal/energy than the control did
  (tools/arena-analyse.py's efficiency figures, imported and reused, not
  reimplemented) -- rwe-ai-objective-doctrine: a game should end in a
  commander kill, not just a bigger unit count, and a candidate that "wins"
  by starving its own economy is not actually better.

  Scenarios: ARM-mirror and CORE-mirror (self-controlled, tune alternates
  seats by seed parity, same as ai-arena.*) plus an ARM-vs-CORE cross game
  (tune applied to both seats, since a single alternating seat would
  otherwise confound the knob with the faction difference -- ai-arena.ps1's
  own warning about -sideA/-sideB differing). A candidate's fitness is the
  unweighted mean of its three scenario fitnesses.

  Search is random sampling + successive halving: --candidates candidates are
  drawn deterministically from --seed, screened at --screen-seeds/
  --screen-seconds, the top --promote go on to --confirm-seeds/
  --confirm-seconds, and the single best of those is checked once more in
  1v1v1v1 on --fourplayer-map (rwe-ai-multi-enemy-focus: a knob tuned only
  against one opponent has silently regressed in a four-player game before,
  with no 1v1 metric moving).

  Resumable: every completed game is appended as one JSON line to
  <out>/results.jsonl; restarting the same --out re-reads it and skips any
  (scenario, candidate, seed, arm, seconds) already there, so a killed run
  costs only the games in flight.

  --jobs (default 3) games run at once via a thread pool -- each is a
  subprocess with its own --out directory, so concurrent games cannot collide
  on ai-arena.csv/events the way sequential games sharing one --out would;
  ai-arena.sh's own header says six-at-once ran the machine out of memory, so
  the default here stays conservative rather than scaling to core count.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import random
import subprocess
import sys
import time
import uuid
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arena_common  # noqa: E402

# Windows redirects stdout to the console codepage (cp1252) rather than UTF-8
# the moment it isn't a TTY, and this script otherwise runs for long enough
# (screening, then confirmation) that discovering a stray non-ASCII character
# only at the very end -- after every game has already been played -- is a
# needless way to lose a run's output. Belt and suspenders: prints stay ASCII
# by convention, and this reconfigures the streams anyway.
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        try:
            _stream.reconfigure(encoding="utf-8", errors="replace")
        except (ValueError, OSError):
            pass

try:
    import tomllib  # Python 3.11+, stdlib
except ImportError:  # pragma: no cover - this machine's python is 3.14
    tomllib = None


# ---------------------------------------------------------------------------
# Tunable constants for the fitness formula. Milestone-1 values, chosen to be
# defensible rather than fitted: a decided game is worth about two deadbands
# of margin, and a percentage point of extra waste costs a small fraction of
# a margin point so it only breaks near-ties.
# ---------------------------------------------------------------------------
MARGIN_DEADBAND = 10          # arena-paired.py's own better/worse/same cutoff
DECIDED_BONUS = 20.0          # per (tuned decided rate - control decided rate)
EFFICIENCY_WEIGHT = 0.3       # per percentage point of (tuned waste - control waste)

DEFAULT_SCENARIOS = ("mirror_arm", "mirror_core", "cross")
SCENARIO_SIDES = {
    "mirror_arm": ("ARM", "ARM"),
    "mirror_core": ("CORE", "CORE"),
    "cross": ("ARM", "CORE"),
}


# ---------------------------------------------------------------------------
# Knob spec: parsing and validation
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class KnobSpec:
    name: str
    type: str  # "int", "bool", "float", "scalar"
    min: Optional[float] = None
    max: Optional[float] = None
    log_scale: bool = False
    choices: Optional[Tuple[Any, ...]] = None


def load_knob_spec(path: str) -> List[KnobSpec]:
    text = Path(path).read_text(encoding="utf-8")
    if path.endswith(".toml"):
        if tomllib is None:
            raise RuntimeError("tomllib not available (need Python 3.11+) and spec is TOML")
        data = tomllib.loads(text)
    else:
        data = json.loads(text)

    entries = data.get("knob", data.get("knobs", []))
    specs: List[KnobSpec] = []
    for e in entries:
        choices = e.get("choices")
        specs.append(
            KnobSpec(
                name=e["name"],
                type=e.get("type", "int"),
                min=e.get("min"),
                max=e.get("max"),
                log_scale=bool(e.get("log_scale", False)),
                choices=tuple(choices) if choices is not None else None,
            )
        )
    return specs


def validate_knob_spec(specs: Sequence[KnobSpec], known: Dict[str, str]) -> None:
    """Raises ValueError naming every knob in `specs` that AiTuningProfile.cpp
    does not actually define, so a typo fails at startup rather than after an
    hour of --ai-tune silently being ignored by applyAiTuning."""
    bad = [s.name for s in specs if s.name not in known]
    if bad:
        raise ValueError(
            "unknown AI tuning knob(s), not found in AiTuningProfile.cpp's tables: "
            + ", ".join(sorted(bad))
        )
    mismatched = []
    for s in specs:
        engine_type = known[s.name]
        # scalar and float both use plain decimal text on the command line, so
        # those two may stand for each other; any other difference is an error.
        continuous = ("float", "scalar")
        if s.type != engine_type and not (s.type in continuous and engine_type in continuous):
            mismatched.append(f"{s.name} (spec says {s.type}, engine says {engine_type})")
    if mismatched:
        # A continuous value sent to an int/bool knob is rejected by the engine
        # (fatal for that game), and an int sent to a float knob wastes the range.
        raise ValueError("knob type mismatch: " + "; ".join(mismatched))


# ---------------------------------------------------------------------------
# Candidate sampling
# ---------------------------------------------------------------------------
Knobs = Dict[str, Any]


def sample_knob_value(spec: KnobSpec, rng: random.Random) -> Any:
    if spec.choices:
        return rng.choice(spec.choices)
    if spec.type == "bool":
        return rng.random() < 0.5
    if spec.min is None or spec.max is None:
        raise ValueError(f"knob {spec.name}: needs min/max or choices")
    if spec.type == "int":
        if spec.log_scale:
            lo, hi = math.log(max(1, spec.min)), math.log(max(1, spec.max))
            return max(spec.min, min(spec.max, round(math.exp(rng.uniform(lo, hi)))))
        return rng.randint(int(spec.min), int(spec.max))
    # float / scalar
    if spec.log_scale:
        lo, hi = math.log(max(1e-9, spec.min)), math.log(max(1e-9, spec.max))
        return math.exp(rng.uniform(lo, hi))
    return rng.uniform(spec.min, spec.max)


def sample_candidate(specs: Sequence[KnobSpec], rng: random.Random) -> Knobs:
    return {s.name: sample_knob_value(s, rng) for s in specs}


def generate_candidates(specs: Sequence[KnobSpec], n: int, seed: int) -> List[Knobs]:
    """Deterministic given `seed`: candidates are drawn in spec order from a
    single random.Random(seed), so two runs with the same --seed produce
    byte-identical candidate lists."""
    rng = random.Random(seed)
    return [sample_candidate(specs, rng) for _ in range(n)]


def format_knob_value(spec_type: str, value: Any) -> str:
    if spec_type == "bool":
        return "true" if value else "false"
    if spec_type == "int":
        return str(int(round(value)))
    return f"{float(value):.4f}"


def candidate_id(knobs: Knobs) -> str:
    canonical = json.dumps(knobs, sort_keys=True)
    return hashlib.sha1(canonical.encode("utf-8")).hexdigest()[:10]


def ai_tune_args(knobs: Knobs, specs_by_name: Dict[str, KnobSpec], player: int) -> List[str]:
    args = []
    for name, value in knobs.items():
        spec = specs_by_name[name]
        args += ["--ai-tune", f"{player}:{name}={format_knob_value(spec.type, value)}"]
    return args


# ---------------------------------------------------------------------------
# Wilson interval
# ---------------------------------------------------------------------------
def wilson_interval(successes: int, n: int, z: float = 1.96) -> Tuple[float, float]:
    if n <= 0:
        return (0.0, 1.0)
    phat = successes / n
    denom = 1.0 + z * z / n
    center = (phat + z * z / (2 * n)) / denom
    half = (z * math.sqrt((phat * (1 - phat) + z * z / (4 * n)) / n)) / denom
    return (max(0.0, center - half), min(1.0, center + half))


# ---------------------------------------------------------------------------
# One game: task description, execution, result parsing
# ---------------------------------------------------------------------------
@dataclass
class GameTask:
    scenario: str
    seed: int
    seconds: int
    sides: Tuple[str, ...]   # one entry per seat, in seat order
    arm: str            # "tuned" or "control"
    tuned_seat: int      # which seat gets the candidate; -1 for control
    map_name: str
    difficulty: str
    start_location: str
    candidate: Optional[Knobs]  # None for control
    out_dir: str
    tune_both: bool = False     # cross scenario: apply the candidate to every seat


def result_key(scenario: str, candidate: Optional[str], seed: int, arm: str, seconds: int) -> str:
    return "|".join([scenario, candidate or "control", str(seed), arm, str(seconds)])


def build_command(exe: str, data_path: str, task: GameTask, specs_by_name: Dict[str, KnobSpec],
                   log_path: str, watchdog: int) -> List[str]:
    cmd = [
        exe,
        "--map", task.map_name,
        "--ai-arena", str(task.seconds),
        "--seed", str(task.seed),
        "--ai-difficulty", task.difficulty,
        "--start-location", task.start_location,
        "--data-path", data_path,
        "--out", task.out_dir,
        "--log", log_path,
        "--watchdog", str(watchdog),
    ]
    for i, side in enumerate(task.sides):
        cmd += ["--player", f"P{i};Computer;{side};{i}"]
    if task.arm == "tuned" and task.candidate:
        if task.tune_both:
            for seat in range(len(task.sides)):
                cmd += ai_tune_args(task.candidate, specs_by_name, seat)
        else:
            cmd += ai_tune_args(task.candidate, specs_by_name, task.tuned_seat)
    return cmd


def run_one_game(exe: str, data_path: str, task: GameTask, specs_by_name: Dict[str, KnobSpec],
                  watchdog: int) -> Dict[str, Any]:
    os.makedirs(task.out_dir, exist_ok=True)
    log_path = os.path.join(task.out_dir, "game.log")
    cmd = build_command(exe, data_path, task, specs_by_name, log_path, watchdog)
    started = time.time()
    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=watchdog + 60)
        exit_code = proc.returncode
    except subprocess.TimeoutExpired:
        exit_code = -1
    wall = time.time() - started

    arena_result = arena_common.read_arena_result(log_path)
    ok = arena_result is not None and arena_result.ended is not None
    return {
        "exit_code": exit_code,
        "wall_time": wall,
        "ok": ok,
        "result": _serialize_result(arena_result),
    }


def _serialize_result(r: Optional[arena_common.ArenaResult]) -> Optional[Dict[str, Any]]:
    if r is None:
        return None
    return {
        "ticks": r.ticks,
        "seconds": r.seconds,
        "ended": r.ended,
        "players": {
            str(i): {
                "side": p.side, "status": p.status, "units": p.units,
                "buildings": p.buildings, "army": p.army, "lost": p.lost,
                "metal_income": p.metal_income, "types": p.types,
            }
            for i, p in r.players.items()
        },
    }


# ---------------------------------------------------------------------------
# Fitness
# ---------------------------------------------------------------------------
def player_margin(players: Dict[str, Any], seat: int, opponents: Sequence[int]) -> Optional[int]:
    """seat's unit count (0 if dead) minus the mean of its opponents' (0 if
    dead), generalising arena-paired.py's two-player margin to N players."""
    me = players.get(str(seat))
    if me is None:
        return None
    me_units = me["units"] if me["status"] == "alive" else 0
    others = []
    for o in opponents:
        op = players.get(str(o))
        if op is None:
            continue
        others.append(op["units"] if op["status"] == "alive" else 0)
    if not others:
        return None
    return me_units - (sum(others) / len(others))


def seat_decided_win(players: Dict[str, Any], seat: int, ended: Optional[str]) -> bool:
    if ended != "decided":
        return False
    me = players.get(str(seat))
    if me is None or me["status"] != "alive":
        return False
    others_alive = any(
        p["status"] == "alive" for k, p in players.items() if int(k) != seat
    )
    return not others_alive


@dataclass
class FitnessSample:
    scenario: str
    seed: int
    margin_tuned: float
    margin_control: float
    margin_delta: float
    verdict: str
    tuned_decided: bool
    control_decided: bool
    tuned_won: bool
    efficiency_delta: Optional[float] = None


def make_fitness_sample(scenario: str, seed: int, tuned_seat: int,
                         tuned_record: Dict[str, Any], control_record: Dict[str, Any],
                         efficiency_delta: Optional[float] = None) -> Optional[FitnessSample]:
    tr, cr = tuned_record.get("result"), control_record.get("result")
    if not tr or not cr:
        return None
    opponents = [i for i in range(len(tr["players"])) if i != tuned_seat]
    mt = player_margin(tr["players"], tuned_seat, opponents)
    mc = player_margin(cr["players"], tuned_seat, opponents)
    if mt is None or mc is None:
        return None
    delta = mt - mc
    verdict = "better" if delta > MARGIN_DEADBAND else "worse" if delta < -MARGIN_DEADBAND else "same"
    return FitnessSample(
        scenario=scenario, seed=seed,
        margin_tuned=mt, margin_control=mc, margin_delta=delta, verdict=verdict,
        tuned_decided=seat_decided_win(tr["players"], tuned_seat, tr["ended"]),
        control_decided=seat_decided_win(cr["players"], tuned_seat, cr["ended"]),
        tuned_won=seat_decided_win(tr["players"], tuned_seat, tr["ended"]),
        efficiency_delta=efficiency_delta,
    )


@dataclass
class CandidateScore:
    candidate_id: str
    knobs: Knobs
    n: int = 0
    wins: int = 0
    losses: int = 0
    ties: int = 0
    mean_margin_delta: float = 0.0
    decided_rate_tuned: float = 0.0
    decided_rate_control: float = 0.0
    mean_efficiency_delta: Optional[float] = None
    wilson_lo: float = 0.0
    wilson_hi: float = 1.0
    fitness: float = 0.0
    by_scenario: Dict[str, float] = field(default_factory=dict)


def aggregate_fitness(candidate_id_: str, knobs: Knobs, samples: Sequence[FitnessSample]) -> CandidateScore:
    score = CandidateScore(candidate_id=candidate_id_, knobs=knobs)
    if not samples:
        return score
    score.n = len(samples)
    score.wins = sum(1 for s in samples if s.verdict == "better")
    score.losses = sum(1 for s in samples if s.verdict == "worse")
    score.ties = score.n - score.wins - score.losses
    score.mean_margin_delta = sum(s.margin_delta for s in samples) / score.n
    score.decided_rate_tuned = sum(1 for s in samples if s.tuned_decided) / score.n
    score.decided_rate_control = sum(1 for s in samples if s.control_decided) / score.n
    eff = [s.efficiency_delta for s in samples if s.efficiency_delta is not None]
    score.mean_efficiency_delta = (sum(eff) / len(eff)) if eff else None
    score.wilson_lo, score.wilson_hi = wilson_interval(score.wins, score.n)

    fitness = score.mean_margin_delta
    fitness += DECIDED_BONUS * (score.decided_rate_tuned - score.decided_rate_control)
    if score.mean_efficiency_delta is not None:
        fitness -= EFFICIENCY_WEIGHT * max(0.0, score.mean_efficiency_delta)
    score.fitness = fitness

    by_scenario: Dict[str, List[float]] = {}
    for s in samples:
        by_scenario.setdefault(s.scenario, []).append(s.margin_delta)
    score.by_scenario = {k: sum(v) / len(v) for k, v in by_scenario.items()}
    return score


def promote(scores: Sequence[CandidateScore], keep_n: int) -> List[CandidateScore]:
    ranked = sorted(
        scores,
        key=lambda s: (-s.fitness, -s.wilson_lo, s.candidate_id),
    )
    return ranked[:keep_n]


# ---------------------------------------------------------------------------
# Results store (resume)
# ---------------------------------------------------------------------------
class ResultStore:
    def __init__(self, path: str):
        self.path = path
        self.records: Dict[str, Dict[str, Any]] = {}
        if os.path.exists(path):
            with open(path, encoding="utf-8") as fh:
                for line in fh:
                    line = line.strip()
                    if not line:
                        continue
                    rec = json.loads(line)
                    self.records[rec["key"]] = rec

    def has(self, key: str) -> bool:
        return key in self.records

    def get(self, key: str) -> Optional[Dict[str, Any]]:
        return self.records.get(key)

    def append(self, key: str, record: Dict[str, Any]) -> None:
        record = dict(record)
        record["key"] = key
        self.records[key] = record
        with open(self.path, "a", encoding="utf-8") as fh:
            fh.write(json.dumps(record) + "\n")


# ---------------------------------------------------------------------------
# Efficiency figures (optional tie-break), reusing arena-analyse.py
# ---------------------------------------------------------------------------
def _load_arena_analyse():
    import importlib.util
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location("arena_analyse_mod", os.path.join(here, "arena-analyse.py"))
    mod = importlib.util.module_from_spec(spec)
    sys.modules["arena_analyse_mod"] = mod
    spec.loader.exec_module(mod)
    return mod


_ARENA_ANALYSE = None


def efficiency_waste_pct(out_dir: str, seat: int) -> Optional[float]:
    """Mean of metalWastedPct/energyWastedPct for one seat's game, via
    arena-analyse.py's own analyse_player -- not reimplemented here. Returns
    None (rather than 0) whenever the figures aren't available, so a
    candidate is never penalised for a column an older run simply lacks."""
    global _ARENA_ANALYSE
    try:
        if _ARENA_ANALYSE is None:
            _ARENA_ANALYSE = _load_arena_analyse()
        import csv
        eco_path = os.path.join(out_dir, "ai-arena.csv")
        ev_path = os.path.join(out_dir, "ai-arena-events.csv")
        if not os.path.exists(eco_path):
            return None
        samples = []
        with open(eco_path, newline="") as fh:
            for row in csv.DictReader(fh):
                if int(row["player"]) == seat:
                    samples.append(row)
        events = []
        if os.path.exists(ev_path):
            with open(ev_path, newline="") as fh:
                for row in csv.DictReader(fh):
                    if int(row["player"]) == seat:
                        events.append(row)
        if not samples:
            return None
        a = _ARENA_ANALYSE.analyse_player(samples, events)
        eff = a.get("efficiency", {})
        parts = [eff[k] for k in ("metalWastedPct", "energyWastedPct") if k in eff]
        return sum(parts) / len(parts) if parts else None
    except Exception:
        return None


# ---------------------------------------------------------------------------
# Orchestration
# ---------------------------------------------------------------------------
PlayFn = Callable[[GameTask], Dict[str, Any]]


def evaluate_round(
    candidates: Sequence[Tuple[str, Knobs]],
    seeds: Sequence[int],
    seconds: int,
    scenarios: Sequence[str],
    map_name: str,
    difficulty: str,
    start_location: str,
    store: ResultStore,
    play_fn: PlayFn,
    make_task_out_dir: Callable[[str, str, int, str], str],
    jobs: int = 3,
    compute_efficiency: bool = False,
) -> List[CandidateScore]:
    """Runs (or resumes) every game a round of candidates needs, then
    aggregates fitness per candidate. Control games are cached in `store` and
    shared across every candidate at the same (scenario, seed, seconds)."""

    to_run: List[Tuple[str, GameTask]] = []

    def seat_for(seed: int) -> int:
        return 0 if seed % 2 == 1 else 1

    # Controls: one per (scenario, seed), independent of every candidate.
    for scenario in scenarios:
        sides = SCENARIO_SIDES[scenario]
        for seed in seeds:
            key = result_key(scenario, None, seed, "control", seconds)
            if store.has(key):
                continue
            task = GameTask(
                scenario=scenario, seed=seed, seconds=seconds, sides=sides,
                arm="control", tuned_seat=-1, map_name=map_name, difficulty=difficulty,
                start_location=start_location, candidate=None,
                out_dir=make_task_out_dir(scenario, "control", seed, "control"),
            )
            to_run.append((key, task))

    # Tuned games, one per (scenario, candidate, seed).
    for cid, knobs in candidates:
        for scenario in scenarios:
            sides = SCENARIO_SIDES[scenario]
            tune_both = scenario == "cross"
            for seed in seeds:
                key = result_key(scenario, cid, seed, "tuned", seconds)
                if store.has(key):
                    continue
                task = GameTask(
                    scenario=scenario, seed=seed, seconds=seconds, sides=sides,
                    arm="tuned", tuned_seat=seat_for(seed), map_name=map_name, difficulty=difficulty,
                    start_location=start_location, candidate=knobs,
                    out_dir=make_task_out_dir(scenario, cid, seed, "tuned"),
                    tune_both=tune_both,
                )
                to_run.append((key, task))

    if to_run:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = {pool.submit(play_fn, task): key for key, task in to_run}
            for fut in as_completed(futures):
                key = futures[fut]
                record = fut.result()
                store.append(key, record)

    # Aggregate.
    scores = []
    for cid, knobs in candidates:
        samples: List[FitnessSample] = []
        for scenario in scenarios:
            for seed in seeds:
                seat = seat_for(seed)
                tuned_key = result_key(scenario, cid, seed, "tuned", seconds)
                control_key = result_key(scenario, None, seed, "control", seconds)
                tuned_rec = store.get(tuned_key)
                control_rec = store.get(control_key)
                if not tuned_rec or not control_rec:
                    continue
                eff_delta = None
                if compute_efficiency and tuned_rec.get("ok") and control_rec.get("ok"):
                    t_out = tuned_rec.get("out_dir")
                    c_out = control_rec.get("out_dir")
                    if t_out and c_out:
                        tw = efficiency_waste_pct(t_out, seat)
                        cw = efficiency_waste_pct(c_out, seat)
                        if tw is not None and cw is not None:
                            eff_delta = tw - cw
                sample = make_fitness_sample(scenario, seed, seat, tuned_rec, control_rec, eff_delta)
                if sample:
                    samples.append(sample)
        scores.append(aggregate_fitness(cid, knobs, samples))
    return scores


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def default_source_path() -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "src", "rwe", "ai", "AiTuningProfile.cpp"))


def build_real_play_fn(exe: str, data_path: str, specs_by_name: Dict[str, KnobSpec], watchdog: int) -> PlayFn:
    def play(task: GameTask) -> Dict[str, Any]:
        record = run_one_game(exe, data_path, task, specs_by_name, watchdog)
        record.update({
            "scenario": task.scenario, "seed": task.seed, "seconds": task.seconds,
            "arm": task.arm, "tuned_seat": task.tuned_seat, "map": task.map_name,
            "sides": list(task.sides), "out_dir": task.out_dir,
            "knobs": task.candidate,
        })
        return record
    return play


def print_leaderboard(scores: Sequence[CandidateScore], title: str) -> None:
    print(f"\n=== {title} ===")
    ranked = sorted(scores, key=lambda s: (-s.fitness, -s.wilson_lo))
    print(f"{'candidate':10} {'n':>3} {'win':>3} {'same':>4} {'loss':>4} {'meanDelta':>9} "
          f"{'wilson95':>14} {'decidedDelta':>13} {'fitness':>9}")
    for s in ranked:
        decided_delta = s.decided_rate_tuned - s.decided_rate_control
        wilson = f"[{s.wilson_lo:.2f},{s.wilson_hi:.2f}]"
        print(f"{s.candidate_id:10} {s.n:>3} {s.wins:>3} {s.ties:>4} {s.losses:>4} "
              f"{s.mean_margin_delta:>9.1f} {wilson:>14} {decided_delta:>+13.2f} {s.fitness:>9.2f}")


def print_candidate_detail(score: CandidateScore, specs: Sequence[KnobSpec]) -> None:
    print(f"\ncandidate {score.candidate_id}: fitness {score.fitness:.2f}, "
          f"{score.wins}/{score.n} better (95% CI [{score.wilson_lo:.2f},{score.wilson_hi:.2f}]), "
          f"mean margin delta {score.mean_margin_delta:+.1f}, "
          f"decided rate tuned {score.decided_rate_tuned:.2f} vs control {score.decided_rate_control:.2f}")
    if score.mean_efficiency_delta is not None:
        print(f"  mean waste delta (tuned - control): {score.mean_efficiency_delta:+.1f} pct points")
    for scenario, delta in sorted(score.by_scenario.items()):
        print(f"  {scenario}: mean margin delta {delta:+.1f}")

    print("\n  --ai-tune string (apply to one player's slot):")
    parts = [f"{name}={format_knob_value(next(s.type for s in specs if s.name == name), value)}"
              for name, value in score.knobs.items()]
    print("    " + ",".join(parts) + "   (ai-arena.ps1/.sh -tune form)")
    print("    e.g.: --ai-tune " + " --ai-tune ".join(f"0:{p}" for p in parts))

    print("\n  proposed AiTuningProfile diff (not applied -- paste into applyFactionDefaults or a profile factory):")
    for name, value in score.knobs.items():
        spec_type = next(s.type for s in specs if s.name == name)
        if spec_type == "int":
            print(f"    p.{name} = {int(round(value))};")
        elif spec_type == "bool":
            print(f"    p.{name} = {'true' if value else 'false'};")
        else:
            print(f"    p.{name} = {float(value):.4f}f;")


def phase_seeds(first: int, screen: int, confirm: int, fourplayer: int) -> Tuple[List[int], List[int], List[int]]:
    """The arena seeds each phase plays, as three disjoint runs of numbers.

    Confirmation must use seeds the screening never saw: a candidate is promoted
    because it did well on the screening seeds, so counting those seeds again in
    confirmation is exactly the selection bias successive halving exists to
    remove. The 1v1v1v1 check is held out from both for the same reason."""
    screen_seeds = list(range(first, first + screen))
    confirm_seeds = list(range(first + screen, first + screen + confirm))
    fp_seeds = list(range(first + screen + confirm, first + screen + confirm + fourplayer))
    return screen_seeds, confirm_seeds, fp_seeds


RUN_CONFIG_NAME = "run_config.json"


def run_fingerprint(args: argparse.Namespace) -> Dict[str, object]:
    """Everything outside the result key that changes what a game means. A
    resumed run with a different map, difficulty or engine binary would
    otherwise blend two experiments into one candidate's fitness."""
    try:
        st = os.stat(args.exe)
        exe_id = {"path": os.path.abspath(args.exe), "size": st.st_size, "mtime": int(st.st_mtime)}
    except OSError:
        exe_id = {"path": os.path.abspath(args.exe), "size": None, "mtime": None}
    return {
        "exe": exe_id,
        "data_path": args.data_path,
        "map": args.map,
        "fourplayer_map": args.fourplayer_map,
        "difficulty": args.difficulty,
        "start_location": args.start_location,
        "first_eval_seed": args.first_eval_seed,
    }


def check_run_config(out_dir: str, fingerprint: Dict[str, object], allow_change: bool) -> None:
    """Records the fingerprint on first use; on resume, refuses a mismatch
    unless --allow-config-change says the caller means it."""
    path = os.path.join(out_dir, RUN_CONFIG_NAME)
    if not os.path.exists(path):
        with open(path, "w", encoding="utf-8") as f:
            json.dump(fingerprint, f, indent=2, sort_keys=True)
        return
    with open(path, encoding="utf-8") as f:
        recorded = json.load(f)
    if recorded != fingerprint:
        diff = sorted(k for k in set(recorded) | set(fingerprint) if recorded.get(k) != fingerprint.get(k))
        msg = f"{path} records a different run configuration ({', '.join(diff)}); results would mix two experiments"
        if not allow_change:
            raise SystemExit("error: " + msg + ". Use a new --out, or --allow-config-change if this is deliberate.")
        print("warning: " + msg, file=sys.stderr)


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", default="D:/RWE-bin/revival-31369ec6/ai_arena.exe")
    parser.add_argument("--data-path", default="D:/RWE-Data")
    parser.add_argument("--out", required=True, help="results/output directory (also enables --resume)")
    parser.add_argument("--spec", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "ai-autotune-knobs.toml"))
    parser.add_argument("--source", default=default_source_path(), help="AiTuningProfile.cpp, for knob validation")
    parser.add_argument("--map", default="Great Divide")
    parser.add_argument("--fourplayer-map", default="Acid Foursome")
    parser.add_argument("--difficulty", default="standard")
    parser.add_argument("--start-location", default="random")
    parser.add_argument("--scenarios", default=",".join(DEFAULT_SCENARIOS))
    parser.add_argument("--candidates", type=int, default=8)
    parser.add_argument("--promote", type=int, default=2)
    parser.add_argument("--screen-seeds", type=int, default=4)
    parser.add_argument("--screen-seconds", type=int, default=900)
    parser.add_argument("--confirm-seeds", type=int, default=12)
    parser.add_argument("--confirm-seconds", type=int, default=900)
    parser.add_argument("--fourplayer-seeds", type=int, default=4)
    parser.add_argument("--fourplayer-seconds", type=int, default=900)
    parser.add_argument("--seed", type=int, default=1, help="RNG seed for candidate generation (determinism)")
    parser.add_argument("--first-eval-seed", type=int, default=1, help="first arena seed used for evaluation games")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--watchdog", type=int, default=120, help="wall-clock seconds before a game is killed")
    parser.add_argument("--efficiency", action="store_true", help="fold arena-analyse waste figures into fitness")
    parser.add_argument("--allow-config-change", action="store_true",
                        help="resume into --out even though its recorded run configuration differs")
    parser.add_argument("--skip-fourplayer", action="store_true", help="skip the mandatory 1v1v1v1 confirmation (debugging only)")
    args = parser.parse_args(argv)

    known_knobs = arena_common.load_knob_tables(args.source)
    specs = load_knob_spec(args.spec)
    validate_knob_spec(specs, known_knobs)
    specs_by_name = {s.name: s for s in specs}
    scenarios = [s.strip() for s in args.scenarios.split(",") if s.strip()]
    for s in scenarios:
        if s not in SCENARIO_SIDES:
            raise ValueError(f"unknown scenario {s!r}, expected one of {list(SCENARIO_SIDES)}")

    os.makedirs(args.out, exist_ok=True)
    check_run_config(args.out, run_fingerprint(args), args.allow_config_change)
    screen_seeds, confirm_seeds, fp_seeds = phase_seeds(
        args.first_eval_seed, args.screen_seeds, args.confirm_seeds, args.fourplayer_seeds)
    store = ResultStore(os.path.join(args.out, "results.jsonl"))
    games_dir = os.path.join(args.out, "games")

    def make_out_dir(scenario: str, cid: str, seed: int, arm: str) -> str:
        d = os.path.join(games_dir, scenario, cid, f"seed{seed}-{arm}-{uuid.uuid4().hex[:6]}")
        return d

    play_fn = build_real_play_fn(args.exe, args.data_path, specs_by_name, args.watchdog)

    candidates_raw = generate_candidates(specs, args.candidates, args.seed)
    candidates = [(candidate_id(k), k) for k in candidates_raw]
    print(f"generated {len(candidates)} candidates from --seed {args.seed}:")
    for cid, k in candidates:
        print(f"  {cid}: {k}")

    t0 = time.time()
    screen_scores = evaluate_round(
        candidates, screen_seeds, args.screen_seconds, scenarios,
        args.map, args.difficulty, args.start_location, store, play_fn, make_out_dir,
        jobs=args.jobs, compute_efficiency=args.efficiency,
    )
    print(f"\nscreening: {len(candidates)} candidates x {len(scenarios)} scenarios x {len(screen_seeds)} seeds "
          f"@ {args.screen_seconds}s, wall {time.time()-t0:.0f}s")
    print_leaderboard(screen_scores, "screening leaderboard")

    promoted = promote(screen_scores, args.promote)
    promoted_pairs = [(s.candidate_id, s.knobs) for s in promoted]
    print(f"\npromoted to confirmation: {[c for c, _ in promoted_pairs]}")

    t1 = time.time()
    confirm_scores = evaluate_round(
        promoted_pairs, confirm_seeds, args.confirm_seconds, scenarios,
        args.map, args.difficulty, args.start_location, store, play_fn, make_out_dir,
        jobs=args.jobs, compute_efficiency=args.efficiency,
    )
    print(f"\nconfirmation: {len(promoted_pairs)} candidates x {len(scenarios)} scenarios x {len(confirm_seeds)} seeds "
          f"@ {args.confirm_seconds}s, wall {time.time()-t1:.0f}s")
    print_leaderboard(confirm_scores, "confirmation leaderboard")

    if not confirm_scores:
        print("\nno candidate produced usable results; nothing to confirm")
        return 1

    winner = max(confirm_scores, key=lambda s: (s.fitness, s.wilson_lo))
    print_candidate_detail(winner, specs)

    if args.skip_fourplayer:
        print("\n--skip-fourplayer set: the mandatory 1v1v1v1 confirmation was NOT run. Do not ship this candidate.")
        return 0

    fp_scenario = "fourplayer"
    fp_sides = ("ARM", "CORE", "ARM", "CORE")
    t2 = time.time()
    fp_scores = evaluate_fourplayer(
        winner.candidate_id, winner.knobs, fp_seeds, args.fourplayer_seconds, fp_sides,
        args.fourplayer_map, args.difficulty, args.start_location, store, play_fn, make_out_dir,
        jobs=args.jobs,
    )
    print(f"\n1v1v1v1 confirmation ({args.fourplayer_map}): {len(fp_seeds)} seeds @ {args.fourplayer_seconds}s, "
          f"wall {time.time()-t2:.0f}s")
    print_leaderboard([fp_scores], "1v1v1v1 confirmation")

    return 0


def evaluate_fourplayer(
    cid: str, knobs: Knobs, seeds: Sequence[int], seconds: int, sides: Sequence[str],
    map_name: str, difficulty: str, start_location: str, store: ResultStore, play_fn: PlayFn,
    make_task_out_dir: Callable[[str, str, int, str], str], jobs: int = 3,
) -> CandidateScore:
    """The mandatory 1v1v1v1 confirmation: the tune goes on one rotating seat
    of a 4-player free-for-all, control gets none, and the margin is that
    seat's units against the mean of the other three -- rwe-ai-multi-enemy-
    focus's standing rule that a 1v1-only measurement can silently miss a
    regression that only shows up with more than one opponent."""
    scenario = "fourplayer"

    def seat_for(seed: int) -> int:
        return seed % len(sides)

    to_run: List[Tuple[str, GameTask]] = []
    for arm, candidate in (("control", None), ("tuned", knobs)):
        for seed in seeds:
            cid_for_key = cid if arm == "tuned" else None
            key = result_key(scenario, cid_for_key, seed, arm, seconds)
            if store.has(key):
                continue
            task = GameTask(
                scenario=scenario, seed=seed, seconds=seconds, sides=tuple(sides),
                arm=arm, tuned_seat=seat_for(seed) if arm == "tuned" else -1,
                map_name=map_name, difficulty=difficulty, start_location=start_location,
                candidate=candidate, out_dir=make_task_out_dir(scenario, cid if arm == "tuned" else "control", seed, arm),
            )
            to_run.append((key, task))

    if to_run:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = {pool.submit(play_fn, task): key for key, task in to_run}
            for fut in as_completed(futures):
                store.append(futures[fut], fut.result())

    samples: List[FitnessSample] = []
    for seed in seeds:
        seat = seat_for(seed)
        tuned_rec = store.get(result_key(scenario, cid, seed, "tuned", seconds))
        control_rec = store.get(result_key(scenario, None, seed, "control", seconds))
        if not tuned_rec or not control_rec:
            continue
        sample = make_fitness_sample(scenario, seed, seat, tuned_rec, control_rec)
        if sample:
            samples.append(sample)
    return aggregate_fitness(cid, knobs, samples)


if __name__ == "__main__":
    sys.exit(main())
