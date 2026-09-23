#!/usr/bin/env python3
"""Playtest runner (design §3.2, §5.4): expand matrix.toml into arena runs,
execute them with a per-run wall-clock watchdog, and write index.jsonl and
findings.jsonl into a run root under the engine's local data root.

Modes: --plan/--dry-run, --execute, --check-only <runroot>, --scan-only.

TOML, not YAML: stdlib only, no PyYAML on the machines that run this.

Never builds the engine -- the design's build gate would run make, and another
agent owns build/ while it is being changed; staleness is recorded per run
(binary_mtime_stale) instead of acted on.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import signal
import subprocess
import sys
import time
import tomllib
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import asdict, dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
DEFAULT_MATRIX = HERE / "matrix.toml"
DEFAULT_THRESHOLDS = HERE / "checkers" / "thresholds.toml"

if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from checkers import CHECKERS, Finding  # noqa: E402

RUN_DIR_RE = re.compile(r"^seed(\d+)-(.+)$")


@dataclass
class PlannedRun:
    run_id: str
    scenario: str
    seed: int
    arm: str
    map: str
    duration_s: int
    players: list
    tune: dict = field(default_factory=dict)
    start_location: str | None = None

    def run_dir(self, run_root) -> Path:
        return Path(run_root) / self.scenario / f"seed{self.seed}-{self.arm}"


@dataclass
class RunResult:
    run_id: str
    scenario: str
    seed: int
    arm: str
    map: str
    players: list
    tune: dict
    status: str
    exit_code: int | None
    wall_seconds: float
    binary_mtime_stale: bool


@dataclass
class RunInfo:
    run_id: str
    scenario: str
    seed: int
    arm: str
    run_dir: Path


@dataclass
class RunContext:
    scenario: str
    seed: int
    arm: str
    siblings: dict = field(default_factory=dict)
    run_root: str = ""
    thresholds: dict = field(default_factory=dict)


# --------------------------------------------------------------------------
# matrix
# --------------------------------------------------------------------------

def load_matrix(path) -> dict:
    with open(path, "rb") as fh:
        return tomllib.load(fh)


def normalize_players(scenario: dict) -> list:
    players = []
    for p in scenario.get("players", []):
        players.append({
            "name": str(p.get("name", "?")),
            "side": str(p.get("side", "ARM")),
            "colour": int(p.get("colour", len(players))),
            "difficulty": str(p.get("difficulty", "standard")),
            "team": p.get("team"),
        })
    return players


def expand(matrix: dict, scenario_filter=None, seeds_override=None) -> list:
    runs = []
    for scenario in matrix.get("scenarios", []):
        sid = scenario["id"]
        if scenario_filter and sid not in scenario_filter:
            continue
        seeds = seeds_override if seeds_override else scenario.get("seeds", [])
        players = normalize_players(scenario)
        tune = scenario.get("tune", {}) or {}
        # A tuned scenario gets an automatic control twin; a scenario without a
        # tune is itself a control.
        arms = ["tuned", "control"] if tune else ["control"]
        for seed in seeds:
            for arm in arms:
                runs.append(PlannedRun(
                    run_id=f"{sid}/seed{seed}-{arm}",
                    scenario=sid,
                    seed=int(seed),
                    arm=arm,
                    map=scenario["map"],
                    duration_s=int(scenario["duration_s"]),
                    players=players,
                    tune=tune if arm == "tuned" else {},
                    start_location=scenario.get("start_location"),
                ))
    return runs


# --------------------------------------------------------------------------
# paths
# --------------------------------------------------------------------------

def data_root() -> Path:
    env = os.environ.get("RWE_LOCAL_DATA")
    if env:
        return Path(env)
    return Path.home() / ".rwe"


def matrix_hash(path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()[:8]


def run_root_for(matrix_path) -> Path:
    date = time.strftime("%Y-%m-%d")
    return data_root() / "playtest" / "runs" / f"{date}-{matrix_hash(matrix_path)}"


def newest_run_root():
    base = data_root() / "playtest" / "runs"
    if not base.is_dir():
        return None
    roots = [d for d in base.iterdir() if d.is_dir()]
    if not roots:
        return None
    return max(roots, key=lambda d: (d.stat().st_mtime, d.name))


# --------------------------------------------------------------------------
# command
# --------------------------------------------------------------------------

def _fmt_value(v) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    return str(v)


def build_command(binary, run: PlannedRun, run_dir) -> list:
    run_dir = Path(run_dir)
    cmd = [
        str(binary),
        "--map", run.map,
        "--ai-arena", str(run.duration_s),
        "--seed", str(run.seed),
    ]
    for p in run.players:
        spec = f"{p['name']};Computer;{p['side']};{p['colour']}"
        if p.get("team") is not None:
            spec += f";{p['team']}"
        cmd += ["--player", spec]

    difficulties = {p["difficulty"] for p in run.players}
    if len(difficulties) == 1:
        cmd += ["--ai-difficulty", difficulties.pop()]

    if run.start_location:
        cmd += ["--start-location", run.start_location]

    if run.arm == "tuned":
        for player_index, knobs in run.tune.items():
            for knob, value in knobs.items():
                cmd += ["--ai-tune", f"{player_index}:{knob}={_fmt_value(value)}"]

    cmd += ["--out", str(run_dir), "--log", str(run_dir / "game.log")]
    return cmd


def newest_src_mtime() -> float:
    newest = 0.0
    src = REPO_ROOT / "src"
    if not src.is_dir():
        return newest
    for path in src.rglob("*"):
        if path.is_file():
            try:
                mtime = path.stat().st_mtime
            except OSError:
                continue
            if mtime > newest:
                newest = mtime
    return newest


def binary_is_stale(binary) -> bool:
    binary = Path(binary)
    try:
        binary_mtime = binary.stat().st_mtime
    except OSError:
        return True
    return binary_mtime < newest_src_mtime()


# --------------------------------------------------------------------------
# execution
# --------------------------------------------------------------------------

def _kill(proc) -> None:
    try:
        if hasattr(os, "killpg"):
            os.killpg(os.getpgid(proc.pid), getattr(signal, "SIGKILL", signal.SIGTERM))
        else:
            proc.kill()
    except (ProcessLookupError, PermissionError, OSError):
        try:
            proc.kill()
        except OSError:
            pass
    try:
        proc.wait(timeout=10)
    except Exception:
        pass


def _log_has_result(path: Path) -> bool:
    try:
        with open(path, errors="replace") as fh:
            for line in fh:
                if "AI-ARENA-RESULT" in line:
                    return True
    except OSError:
        return False
    return False


def _status(run_dir: Path, timed_out: bool, exit_code) -> str:
    if timed_out:
        return "timeout"
    if exit_code is None:
        return "exit-unknown"
    if exit_code != 0:
        return f"exit-{exit_code}"
    if not _log_has_result(run_dir / "game.log"):
        return "no-result"
    if not (run_dir / "run.json").exists():
        return "missing-run-json"
    return "ok"


def execute_run(binary, run: PlannedRun, run_dir, watchdog_seconds: float, stale: bool) -> RunResult:
    run_dir = Path(run_dir)
    run_dir.mkdir(parents=True, exist_ok=True)
    cmd = build_command(binary, run, run_dir)

    started = time.monotonic()
    timed_out = False
    exit_code = None
    try:
        with open(run_dir / "runner.log", "wb") as logfh:
            proc = subprocess.Popen(
                cmd,
                stdout=logfh,
                stderr=subprocess.STDOUT,
                cwd=str(REPO_ROOT),
                start_new_session=True,
            )
            try:
                proc.wait(timeout=watchdog_seconds)
                exit_code = proc.returncode
            except subprocess.TimeoutExpired:
                timed_out = True
                _kill(proc)
                exit_code = proc.returncode
    except OSError as exc:
        exit_code = 127
        try:
            (run_dir / "runner.log").write_text(f"failed to launch {cmd[0]}: {exc}\n")
        except OSError:
            pass

    wall = time.monotonic() - started
    return RunResult(
        run_id=run.run_id,
        scenario=run.scenario,
        seed=run.seed,
        arm=run.arm,
        map=run.map,
        players=run.players,
        tune=run.tune,
        status=_status(run_dir, timed_out, exit_code),
        exit_code=exit_code,
        wall_seconds=round(wall, 3),
        binary_mtime_stale=stale,
    )


def write_jsonl(path, records) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w") as fh:
        for record in records:
            fh.write(json.dumps(record, sort_keys=True, default=str) + "\n")


# --------------------------------------------------------------------------
# checking
# --------------------------------------------------------------------------

def discover_runs(run_root) -> list:
    run_root = Path(run_root)
    infos = []
    if not run_root.is_dir():
        return infos
    for scenario_dir in sorted(p for p in run_root.iterdir() if p.is_dir()):
        for run_dir in sorted(p for p in scenario_dir.iterdir() if p.is_dir()):
            m = RUN_DIR_RE.match(run_dir.name)
            if not m:
                continue
            infos.append(RunInfo(
                run_id=f"{scenario_dir.name}/{run_dir.name}",
                scenario=scenario_dir.name,
                seed=int(m.group(1)),
                arm=m.group(2),
                run_dir=run_dir,
            ))
    return infos


def build_contexts(infos: list, run_root, thresholds: dict) -> dict:
    groups: dict = {}
    for info in infos:
        groups.setdefault((info.scenario, info.seed), {})[info.arm] = info.run_dir
    contexts = {}
    for info in infos:
        siblings = groups[(info.scenario, info.seed)]
        contexts[info.run_dir] = RunContext(
            scenario=info.scenario,
            seed=info.seed,
            arm=info.arm,
            siblings={arm: str(path) for arm, path in siblings.items()},
            run_root=str(run_root),
            thresholds=thresholds,
        )
    return contexts


def load_thresholds(path) -> dict:
    with open(path, "rb") as fh:
        return tomllib.load(fh)


def check_root(run_root, thresholds_path) -> list:
    run_root = Path(run_root)
    try:
        thresholds = load_thresholds(thresholds_path)
    except (OSError, tomllib.TOMLDecodeError):
        thresholds = {}
    infos = discover_runs(run_root)
    contexts = build_contexts(infos, run_root, thresholds)

    findings = []
    for info in infos:
        context = contexts[info.run_dir]
        for name, checker in CHECKERS.items():
            try:
                produced = checker(info.run_dir, context)
            except Exception as exc:  # a checker must not take the batch down
                produced = [Finding(
                    checker=name,
                    severity="info",
                    summary=f"{name} checker raised: {exc}",
                    evidence={"run_dir": str(info.run_dir)},
                )]
            for finding in produced:
                findings.append({"run_id": info.run_id, **asdict(finding)})

    findings.sort(key=lambda f: (
        f.get("run_id", ""), f.get("checker", ""), f.get("severity", ""), f.get("summary", "")))
    write_jsonl(run_root / "findings.jsonl", findings)
    return findings


# --------------------------------------------------------------------------
# modes
# --------------------------------------------------------------------------

def parse_id_list(value):
    if not value:
        return None
    return [item.strip() for item in value.split(",") if item.strip()]


def parse_seed_list(value):
    if not value:
        return None
    return [int(item) for item in parse_id_list(value)]


def do_plan(args) -> int:
    matrix = load_matrix(args.matrix)
    runs = expand(matrix, parse_id_list(args.scenarios), parse_seed_list(args.seeds))
    root = run_root_for(args.matrix)
    print(f"run root: {root}")
    for run in runs:
        print(f"  {run.run_id}  map={run.map} duration_s={run.duration_s}")
    print(f"total: {len(runs)} runs")
    return 0


def do_execute(args) -> int:
    matrix = load_matrix(args.matrix)
    runs = expand(matrix, parse_id_list(args.scenarios), parse_seed_list(args.seeds))
    if not runs:
        print("no runs to execute", file=sys.stderr)
        return 1

    root = run_root_for(args.matrix)
    root.mkdir(parents=True, exist_ok=True)
    binary = Path(args.binary) if args.binary else REPO_ROOT / "build" / "ai_arena"
    stale = binary_is_stale(binary)

    jobs = args.jobs if args.jobs and args.jobs > 0 else max(1, (os.cpu_count() or 2) // 2)
    print(f"run root: {root}")
    print(f"binary: {binary} (stale={stale}), jobs={jobs}")

    results: dict = {}
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {}
        for run in runs:
            watchdog = args.watchdog_seconds if args.watchdog_seconds else max(120.0, 3.0 * run.duration_s)
            futures[pool.submit(execute_run, binary, run, run.run_dir(root), watchdog, stale)] = run
        for future in as_completed(futures):
            result = future.result()
            results[result.run_id] = result

    ordered = [results[run.run_id] for run in runs if run.run_id in results]
    write_jsonl(root / "index.jsonl", [asdict(r) for r in ordered])

    findings = check_root(root, args.thresholds)

    for result in ordered:
        print(f"  {result.run_id}: {result.status} ({result.wall_seconds:.1f}s)")
    print(f"findings: {len(findings)}")

    failed = [r for r in ordered
              if r.status == "timeout" or r.status.startswith("exit-") or r.status == "no-result"]
    if failed:
        return 1
    if findings:
        return 2
    return 0


def do_check(run_root, thresholds_path) -> int:
    findings = check_root(run_root, thresholds_path)
    print(f"{len(findings)} findings written to {Path(run_root) / 'findings.jsonl'}")
    return 2 if findings else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Playtest runner: expand, execute and check arena scenarios.")
    parser.add_argument("--matrix", default=str(DEFAULT_MATRIX), help="scenario matrix TOML")
    parser.add_argument("--thresholds", default=str(DEFAULT_THRESHOLDS), help="checker thresholds TOML")
    parser.add_argument("--plan", action="store_true", help="print the expanded plan and exit")
    parser.add_argument("--dry-run", action="store_true", help="alias of --plan")
    parser.add_argument("--execute", action="store_true", help="execute the planned runs")
    parser.add_argument("--scenarios", default=None, help="comma-separated scenario ids to keep")
    parser.add_argument("--seeds", default=None, help="comma-separated seed override")
    parser.add_argument("--binary", default=None, help="path to the ai_arena binary")
    parser.add_argument("--jobs", type=int, default=None, help="parallel workers (default nproc/2)")
    parser.add_argument("--watchdog-seconds", type=float, default=None, help="per-run wall-clock budget override")
    parser.add_argument("--check-only", default=None, metavar="RUNROOT", help="check an existing run root")
    parser.add_argument("--scan-only", action="store_true", help="check the newest run root")
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)

    if args.check_only:
        return do_check(Path(args.check_only).resolve(), args.thresholds)
    if args.scan_only:
        root = newest_run_root()
        if root is None:
            print("no run roots found", file=sys.stderr)
            return 1
        return do_check(root, args.thresholds)
    if args.execute:
        return do_execute(args)
    return do_plan(args)


if __name__ == "__main__":
    sys.exit(main())
