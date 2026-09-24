#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Check RWE's own demo output with the machinery built for the TA corpus.

`ai_arena --record-demo` writes a TA demo of a game RWE has just played. This
script points the tools docs/TA-DEMOS.md already aims at the TA corpus at that
file: `tad_probe` for the container walk, `tad_episodes --unit-state` for the
0x2c decode, and the reference scorers for the build, weapon and stall oracles.
The recorder's job is to write what a TA peer would have sent; this is what
checks it against the same model the corpus is checked against.

    tools/demo-selfcheck.py                        # record 900s and check it
    tools/demo-selfcheck.py --seconds 300 --seed 3
    tools/demo-selfcheck.py --demo game.tad --units /path/to/data-set
    tools/demo-selfcheck.py --keep                 # keep the work directory

THE UNITS DIRECTORY. The scorers name a 0x09's type index from the 1-based
sorted `units/*.FBI` order of the data set the game was played on, and the
recorder writes that same order, so both have to come from the same data.
`--units` takes a directory of the data set's unit files; without it the script
builds one by extracting every archive of `--data-path` (default `~/.rwe/Data`)
in the engine's own priority order -- .hpi, then .ufo, .ccx, .gpf, .gp3, each
tier name-sorted -- with each archive overwriting the ones before it. The
overwrite is CASE-INSENSITIVE into one tree, because the engine's VFS resolves
names case-insensitively while the shipped archives carry both `units/` and
`UNITS/`: a raw extract-all of that data set gives 435 *.FBI files across the
two directories for the 278 types the VFS sees and the demo's 0x1a table
declares.
The merged tree also carries the `weapons/` TDFs, because tools/tad-weapontime.py
reads weapon definitions through the same `--units` path. The resulting *.FBI
count is checked against the demo's own 0x1a count and a mismatch stops the
run: `tad_episodes` only warns and skips the 0x2c pass, and the build cells
drop the demo outright, so a mismatch would quietly score the wrong thing.

WHAT IT REPORTS. `tad_probe` must be clean -- no unknown codes, no truncated
walks, no desyncs, every status checksum verified -- and `--unit-state` must
decode every 0x2c. Each of the four corpus oracles is then one of three things:

  * PASS -- the scorer scored cells and they agree with the model.
  * NOT YET SCOREABLE -- the demo does not carry the records the scorer needs
    (M3 writes only 0x09/0x12/0x2c; M4 adds shots, damage, deaths and 0x28), or
    it carries them and no cell met the scorer's floor. A scorer exits non-zero
    for that too, so the message is read and not just the status. Reported
    plainly; it is neither a pass nor a failure.
  * FAILED -- cells were scored and disagree. A scored cell that moves is the
    check failing, and sets the exit status.

There is no fourth script for the storage half of the corpus checks: its cases
live in `rwe_test`'s [economy][corpus] over src/rwe/sim/tad_economy_episodes.h,
so the summary says that rather than inventing a scorer.

ONE EXPECTED DISAGREEMENT ON M3's OUTPUT. A factory's build cells score two
ticks late against the model. RWE lays the nanoframe down two ticks before the
first lathe credit (the creation request is serviced later in the tick, the
next tick starts the StartBuilding thread and returns without lathing), where
TA's first increment lands on the 0x09's own tick. That is the engine's
pipeline latency, which src/rwe/sim/buildtime.test.cpp explains and the corpus
fixture deliberately does not measure; it is not the recorder, and the
self-check reports it as a scored disagreement rather than hiding it.
"""

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# The engine's archive extensions, lowest priority first, from
# CompositeVirtualFileSystem::addToVfs: a later archive overwrites an earlier
# one, which is what makes a .gp3 beat a .hpi. Among archives of one extension
# the engine has no order at all (it is a directory_iterator), so the name sort
# here is only the deterministic tie-break; the 0x1a count check is what catches
# a wrong set.
ARCHIVE_EXTENSIONS = (".hpi", ".ufo", ".ccx", ".gpf", ".gp3")

UNIT_TABLE = re.compile(r"unit table:\s+(\d+)\s+types")
SUB_PACKET_CODES = re.compile(r"subpacket codes:(.*)")
SUB_PACKET_CODE = re.compile(r"0x([0-9a-fA-F]{2})x(\d+)")

PASS = "pass"
NOT_SCOREABLE = "not yet scoreable"
NO_SCORER = "no demo scorer"
FAILED = "failed"
UNSCORED = (NOT_SCOREABLE, NO_SCORER)


def tool_path(repo: Path, name: str) -> Path:
    for candidate in (repo / "build" / name, repo / "build" / f"{name}.exe"):
        if candidate.is_file():
            return candidate
    return repo / "build" / name


def run(command, quiet=False):
    """Echo the command, run it, echo both streams, and hand back the result."""
    if not quiet:
        print(f"$ {shlex.join(str(part) for part in command)}", flush=True)
    result = subprocess.run(
        [str(part) for part in command],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        errors="replace",
    )
    if not quiet:
        for stream, out in ((sys.stdout, result.stdout), (sys.stderr, result.stderr)):
            if out:
                print(out, end="" if out.endswith("\n") else "\n", file=stream, flush=True)
    return result


def classify(result):
    """A scorer's exit status, with 'nothing to score' read out of its output."""
    if result.returncode == 0:
        return PASS
    if "nothing to score" in (result.stdout + result.stderr):
        return NOT_SCOREABLE
    return FAILED


def archives_in_priority_order(data_path: Path) -> list[Path]:
    by_extension = {}
    for entry in sorted(data_path.iterdir(), key=lambda p: p.name.lower()):
        if entry.is_file() and entry.suffix.lower() in ARCHIVE_EXTENSIONS:
            by_extension.setdefault(entry.suffix.lower(), []).append(entry)
    return [archive for extension in ARCHIVE_EXTENSIONS for archive in by_extension.get(extension, [])]


def overlay_wanted(relative: Path) -> bool:
    """The subtrees the scorers read: units, and any directory named for weapons.

    `tad_episodes` matches a weapon TDF on its parent directory CONTAINING
    "weapon" (Escalation's is `weaponE`), and this merges by any path component
    so a deeper tree still arrives; extra files cost nothing, a missing weapon
    costs tools/tad-weapontime.py its whole run.
    """
    parts = [part.lower() for part in relative.parts]
    if parts and parts[0] == "units":
        return True
    return any("weapon" in part for part in parts[:-1])


def build_units_overlay(data_path: Path, work: Path, hpi_test: Path) -> tuple[Path, int]:
    """Extract every archive in priority order into one case-folded tree."""
    overlay = work / "dataset"
    extraction = work / "extract"
    archives = archives_in_priority_order(data_path)
    if not archives:
        raise SystemExit(f"no {'/'.join(ARCHIVE_EXTENSIONS)} archives under {data_path}")

    print(f"units overlay: {len(archives)} archive(s) from {data_path}, in priority order")
    merged = 0
    for archive in archives:
        destination = extraction / archive.name
        destination.mkdir(parents=True)
        result = run([hpi_test, "extract-all", archive, destination], quiet=True)
        if result.returncode != 0:
            raise SystemExit(f"hpi_test could not extract {archive}:\n{result.stdout}{result.stderr}")

        for root, _dirs, files in os.walk(destination):
            for name in files:
                source = Path(root) / name
                relative = source.relative_to(destination)
                if not overlay_wanted(relative):
                    continue
                target = overlay.joinpath(*[part.lower() for part in relative.parts])
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)
                merged += 1

        shutil.rmtree(destination)
    shutil.rmtree(extraction)

    return overlay, merged


def count_unit_files(units_dir: Path) -> int:
    return sum(1 for path in units_dir.rglob("*") if path.is_file() and path.suffix.lower() == ".fbi")


def resource_records(resources_path: Path) -> int:
    """Samples the stall scorer's phase check would see, which is not all of them.

    tools/tad-stalltime.py drops any sample whose stored metal and energy are
    both zero, and then takes the minimum of the survivors -- an empty list is a
    crash there, not a 'nothing to score', so the emptiness is decided here.
    """
    demos = json.loads(resources_path.read_text())
    return sum(
        1
        for demo in demos
        for record in demo.get("records", [])
        if record["metalStorage"] != 0.0 or record["energyStorage"] != 0.0
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--work-dir", type=Path, help="where the demo and the dumps land (default: $TMPDIR/rwe-demo-selfcheck)")
    parser.add_argument("--keep", action="store_true", help="keep the work directory instead of clearing it first")
    parser.add_argument("--demo", type=Path, help="check this demo instead of recording one")
    parser.add_argument("--units", type=Path, help="a data set's unit files; built from --data-path when absent")
    parser.add_argument("--data-path", type=Path, help="the data set's archives (default: ~/.rwe/Data)")
    parser.add_argument("--ai-arena", type=Path, help="the ai_arena binary (default: <repo>/build/ai_arena)")
    parser.add_argument("--tad-probe", type=Path, help="the tad_probe binary (default: <repo>/build/tad_probe)")
    parser.add_argument("--tad-episodes", type=Path, help="the tad_episodes binary (default: <repo>/build/tad_episodes)")
    parser.add_argument("--hpi-test", type=Path, help="the hpi_test binary (default: <repo>/build/hpi_test)")
    parser.add_argument(
        "--map",
        default="The Cold Place",
        help="arena map (default: The Cold Place, which gives the weapon oracle something to score)",
    )
    parser.add_argument("--seconds", type=int, default=900, help="arena game length cap in seconds (default 900)")
    parser.add_argument("--seed", type=int, default=7, help="arena seed (default 7)")
    parser.add_argument(
        "--player",
        action="append",
        metavar="NAME;Computer;SIDE;COLOUR",
        help="arena player (repeatable; default: two ARM computer players)",
    )
    parser.add_argument("--ai-difficulty", default="standard", help="arena difficulty (default standard)")
    parser.add_argument(
        "--start-location",
        default="random",
        choices=("random", "fixed"),
        help="arena start locations (default random)",
    )
    parser.add_argument("--watchdog", type=int, default=0, help="abort the arena after this many wall-clock seconds")
    args = parser.parse_args()

    ai_arena = args.ai_arena or tool_path(REPO, "ai_arena")
    tad_probe = args.tad_probe or tool_path(REPO, "tad_probe")
    tad_episodes = args.tad_episodes or tool_path(REPO, "tad_episodes")
    hpi_test = args.hpi_test or tool_path(REPO, "hpi_test")
    players = args.player or ["A;Computer;ARM;0", "B;Computer;ARM;1"]

    work = (args.work_dir or Path(os.environ.get("TMPDIR", "/tmp")) / "rwe-demo-selfcheck").resolve()
    if REPO in work.parents or work == REPO:
        raise SystemExit(f"refusing to write the self-check's work into the repository at {REPO}")
    if work.exists() and not args.keep:
        shutil.rmtree(work)
    work.mkdir(parents=True, exist_ok=True)

    if args.demo is None:
        if not ai_arena.is_file():
            raise SystemExit(f"no {ai_arena}; build it with `make -C build -j8 ai_arena`")
        demo = work / "game.tad"
        command = [
            ai_arena,
            "--map", args.map,
            "--ai-arena", str(args.seconds),
            "--seed", str(args.seed),
            "--start-location", args.start_location,
            "--ai-difficulty", args.ai_difficulty,
            "--out", work,
            "--log", work / "ai_arena.log",
            "--record-demo", demo,
            "--strict",
        ]
        for player in players:
            command += ["--player", player]
        if args.data_path is not None:
            command += ["--data-path", args.data_path]
        if args.watchdog > 0:
            command += ["--watchdog", str(args.watchdog)]

        print(f"recording {args.seconds}s of {args.map} (seed {args.seed})")
        result = run(command)
        if result.returncode != 0 or not demo.is_file() or demo.stat().st_size == 0:
            raise SystemExit(f"the arena did not produce a demo at {demo}; see {work / 'ai_arena.log'}")
        print(f"recorded {demo} ({demo.stat().st_size} bytes)")
    else:
        demo = args.demo.resolve()
        if not demo.is_file():
            raise SystemExit(f"no such demo: {demo}")
        print(f"checking {demo}")

    for binary, name in ((tad_probe, "tad_probe"), (tad_episodes, "tad_episodes")):
        if not binary.is_file():
            raise SystemExit(f"no {binary}; build it with `make -C build -j8 {name}`")

    failures: list[str] = []
    summary: list[tuple[str, str, str]] = []
    records: dict[int, int] = {}

    # 1. The container walk. Everything after this reads the file, so a parse
    # failure stops here rather than being scored as an oracle miss.
    probe = run([tad_probe, "--file", demo])
    probe_text = probe.stdout + probe.stderr
    unit_table = UNIT_TABLE.search(probe_text)
    codes = SUB_PACKET_CODES.search(probe_text)
    if codes:
        records = {int(code, 16): int(count) for code, count in SUB_PACKET_CODE.findall(codes.group(1))}

    probe_problems = []
    if probe.returncode != 0:
        probe_problems.append("tad_probe exited non-zero")
    if "CHECKSUM MISMATCH" in probe_text:
        probe_problems.append("a player status checksum did not verify")
    if "DESYNC" in probe_text:
        probe_problems.append("the walk desynchronised")
    if unit_table is None:
        probe_problems.append("the 0x1a unit table could not be read")
    if probe_problems:
        failures.append("tad_probe: " + "; ".join(probe_problems))
        sys.stdout.flush()
        print("\nself-check FAILED before any oracle ran:", file=sys.stderr)
        for problem in probe_problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    demo_types = int(unit_table.group(1))
    summary.append(("tad_probe", PASS, f"clean; 0x1a declares {demo_types} types"))

    # 2. The units directory, and the count both sides of the type-index mapping
    # depend on. A mismatch is fatal: tad_episodes would skip the 0x2c pass and
    # the build cells would drop the demo.
    if args.units is not None:
        units_dir = args.units.resolve()
        if not units_dir.is_dir():
            raise SystemExit(f"no such units directory: {units_dir}")
        origin = "--units"
    else:
        data_path = (args.data_path or Path.home() / ".rwe" / "Data").resolve()
        if not data_path.is_dir():
            raise SystemExit(f"no such data path: {data_path} (pass --units or --data-path)")
        if not hpi_test.is_file():
            raise SystemExit(f"no {hpi_test}; build it with `make -C build -j8 hpi_test`")
        units_dir, merged = build_units_overlay(data_path, work, hpi_test)
        origin = f"overlay of {data_path} ({merged} files merged case-insensitively)"

    unit_count = count_unit_files(units_dir)
    print(f"units: {unit_count} *.FBI under {units_dir} ({origin})")
    if unit_count != demo_types:
        sys.stdout.flush()
        print(
            f"\nself-check FAILED: the units directory has {unit_count} unit types but {demo.name}\n"
            f"declares {demo_types} in its 0x1a table.\n"
            f"  units: {units_dir}\n"
            f"  The two must agree: tad_episodes names every 0x09 type index from the directory's\n"
            f"  1-based load order, warns and skips the 0x2c pass when the counts differ, and drops\n"
            f"  the demo from the build cells. Check --units, or the data set the demo was recorded on.",
            file=sys.stderr,
        )
        return 1

    # 3. The 0x2c decode, held to the rest of the stream by tad_episodes itself.
    state = run([tad_episodes, "--file", demo, "--units", units_dir, "--unit-state"])
    state_text = state.stdout + state.stderr
    if state.returncode != 0 or "unit state skipped" in state_text:
        failures.append("tad_episodes --unit-state: the 0x2c decode failed")
        summary.append(("unit state", FAILED, "the 0x2c decode failed; see above"))
    else:
        summary.append(("unit state", PASS, "every 0x2c decoded"))

    episodes = work / "episodes.json"
    resources = work / "resources.json"
    dumps = run(
        [
            tad_episodes,
            "--file", demo,
            "--units", units_dir,
            "--all",
            "--emit-json", episodes,
            "--emit-resources", resources,
        ]
    )
    if dumps.returncode != 0:
        failures.append("tad_episodes --all --emit-json --emit-resources exited non-zero")
        summary.append(("episodes", FAILED, "the episode dump failed"))
        return report(summary, failures, records, work)

    buildtime = run([sys.executable, REPO / "tools" / "tad-buildtime.py", "--episodes", episodes, "--units", units_dir])
    buildtime_state = classify(buildtime)
    if buildtime_state == PASS:
        summary.append(("build timing", PASS, "scored cells agree with the model"))
    elif buildtime_state == NOT_SCOREABLE:
        summary.append(("build timing", NOT_SCOREABLE, "no immobile-builder pair met --min-builds"))
    else:
        failures.append("tad-buildtime.py: a scored cell disagrees with the model")
        summary.append(("build timing", FAILED, "a scored cell disagrees; see its table above"))

    if 0x0D in records or 0x0B in records or 0x0C in records:
        shots = work / "shots.jsonl"
        emit_shots = run([tad_episodes, "--file", demo, "--units", units_dir, "--emit-shots", shots])
        if emit_shots.returncode != 0:
            failures.append("tad_episodes --emit-shots exited non-zero")
            summary.append(("weapon flight", FAILED, "the shot dump failed"))
        else:
            weapontime = run(
                [sys.executable, REPO / "tools" / "tad-weapontime.py", "--shots", shots, "--units", units_dir]
            )
            weapon_state = classify(weapontime)
            if weapon_state == PASS:
                summary.append(("weapon flight", PASS, "scored cells agree with the model"))
            elif weapon_state == NOT_SCOREABLE:
                summary.append(("weapon flight", NOT_SCOREABLE, "no cell met --min-n pairings"))
            else:
                failures.append("tad-weapontime.py: a scored cell disagrees with the model")
                summary.append(("weapon flight", FAILED, "a scored cell disagrees; see its table above"))
    else:
        summary.append(("weapon flight", NOT_SCOREABLE, "the demo carries no 0x0d/0x0b/0x0c records (M4)"))

    samples = resource_records(resources)
    if samples > 0:
        stalltime = run(
            [
                sys.executable,
                REPO / "tools" / "tad-stalltime.py",
                "--episodes", episodes,
                "--resources", resources,
                "--units", units_dir,
            ]
        )
        stall_state = classify(stalltime)
        if stall_state == PASS:
            summary.append(("stalls", PASS, "every scored build lands on its residue"))
        elif stall_state == NOT_SCOREABLE:
            summary.append(("stalls", NOT_SCOREABLE, "no stall episode was scored"))
        else:
            failures.append("tad-stalltime.py: a scored cell disagrees with the model")
            summary.append(("stalls", FAILED, "a scored cell disagrees; see its table above"))
    else:
        summary.append(("stalls", NOT_SCOREABLE, "the demo carries no 0x28 resource samples (M4)"))

    summary.append(
        (
            "storage/economy",
            NO_SCORER,
            "rwe_test's [economy][corpus] over src/rwe/sim/tad_economy_episodes.h, not a demo scorer",
        )
    )

    return report(summary, failures, records, work)


def report(summary, failures, records, work: Path) -> int:
    print("\nself-check summary")
    width = max(len(name) for name, _, _ in summary)
    for name, state, note in summary:
        print(f"  {name:<{width}}  {state.upper():<17} {note}")

    if records:
        print("\n  subpackets in the demo: " + ", ".join(f"0x{code:02x} x{count}" for code, count in sorted(records.items())))

    print(f"\n  work directory: {work}")
    if failures:
        print("\nself-check FAILED:")
        for failure in failures:
            print(f"  {failure}")
        return 1

    if all(state in UNSCORED for name, state, _ in summary if name != "tad_probe"):
        print("\nno oracle scored cells; tad_probe is clean and nothing failed.")
    else:
        print("\nall checks passed or reported honestly as not yet scoreable.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
