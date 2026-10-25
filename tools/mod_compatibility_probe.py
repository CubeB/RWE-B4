#!/usr/bin/env python3
"""Run bounded, repeatable mod games through RWE's real headless loader.

This is an RWE diagnostic, not an original-game oracle or a compatibility
certificate. See docs/TA-MOD-COMPATIBILITY-VALIDATION.md.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


RESOURCES = (
    "ai", "anims", "bitmaps", "camps", "downloads", "fonts", "gamedata",
    "guis", "maps", "objects3d", "palettes", "scripts", "sounds",
    "textures", "unitpics", "units", "weapons",
)


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(block)
    return sha.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True, help="ai_arena executable")
    parser.add_argument("--data-path", action="append", type=Path, required=True,
                        help="repeat in priority order: mod first, base game last")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--map", default="Coast To Coast")
    parser.add_argument("--seconds", type=int, default=600)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--repeat", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--ai-engine", choices=("classic", "rwe"), default="classic")
    parser.add_argument("--require-profile", action="store_true")
    for resource in RESOURCES:
        parser.add_argument("--dir-" + resource)
    args = parser.parse_args()
    if min(args.seconds, args.repeat, args.timeout) < 1:
        parser.error("seconds, repeat and timeout must be positive")
    if not args.engine.is_file() or any(not p.is_dir() for p in args.data_path):
        parser.error("engine and all data directories must exist")

    args.out.mkdir(parents=True, exist_ok=True)
    engine = args.engine.resolve()
    report = {
        "scope": "bounded RWE execution and same-binary determinism; not full mod parity",
        "engine": str(engine), "engine_sha256": digest(engine),
        "map": args.map, "seconds": args.seconds, "seed": args.seed,
        "ai_engine": args.ai_engine, "directories": {}, "data_paths": [], "runs": [],
    }
    command = [str(engine), "--map", args.map, "--ai-arena", str(args.seconds),
               "--seed", str(args.seed), "--ai-engine", args.ai_engine,
               "--ai-difficulty", "hard", "--player", "ARM;Computer;ARM;0",
               "--player", "CORE;Computer;CORE;1", "--strict",
               "--watchdog", str(args.timeout)]
    for directory in args.data_path:
        directory = directory.resolve()
        command += ["--data-path", str(directory)]
        # Identify the archive corpus without copying proprietary game data.
        archives = [{"name": p.name, "bytes": p.stat().st_size, "sha256": digest(p)}
                    for p in sorted(directory.iterdir())
                    if p.is_file() and p.suffix.lower() in (".hpi", ".ufo", ".ccx", ".gpf", ".gp3")]
        report["data_paths"].append({"path": str(directory), "archives": archives})
    for resource in RESOURCES:
        directory = getattr(args, "dir_" + resource)
        if directory is not None:
            command += ["--dir-" + resource, directory]
            report["directories"][resource] = directory

    passed = True
    previous = None
    for index in range(args.repeat):
        output = (args.out / ("run-" + str(index + 1))).resolve()
        output.mkdir(parents=True, exist_ok=True)
        log = output / "arena.log"
        hashes = output / "hashes.log"
        env = dict(os.environ, RWE_HASH_LOG=str(hashes))
        invocation = command + ["--out", str(output), "--log", str(log)]
        try:
            result = subprocess.run(invocation, capture_output=True, text=True,
                                    timeout=args.timeout + 15, env=env)
            (output / "stdout.txt").write_text(result.stdout, encoding="utf-8")
            (output / "stderr.txt").write_text(result.stderr, encoding="utf-8")
            text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
            profiles = re.findall(r"classic AI profile read from ([^\r\n]+)", text)
            root = report["directories"].get("ai", "ai").rstrip("/\\") + "/"
            checks = {
                "successful_exit": result.returncode == 0,
                "arena_result": "AI-ARENA-RESULT" in text,
                "profile_root": all(p.startswith(root) for p in profiles),
                "required_profile": not args.require_profile or len(profiles) == 2,
                "hash_log": hashes.is_file() and hashes.stat().st_size > 0,
            }
            fingerprint = {name: digest(output / name) for name in
                           ("hashes.log", "ai-arena.csv", "ai-arena-events.csv")
                           if (output / name).is_file()}
            if previous is not None:
                checks["same_seed_repeat"] = fingerprint == previous
            previous = fingerprint
            run = {"command": invocation, "exit": result.returncode,
                   "profiles": profiles, "checks": checks, "fingerprint": fingerprint,
                   "warnings": sorted(set(re.findall(r"\[warn\] ([^\r\n]+)", text))),
                   "result": re.findall(r"AI-ARENA-RESULT[^\r\n]+", text)}
            passed = passed and all(checks.values())
        except subprocess.TimeoutExpired:
            run = {"command": invocation, "error": "process timeout"}
            passed = False
        report["runs"].append(run)
        print("Run", index + 1, "passed" if "checks" in run and all(run["checks"].values()) else "FAILED", flush=True)
    report["checks_passed"] = passed
    (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
