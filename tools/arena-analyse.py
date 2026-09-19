# Reads a folder of arena games and says HOW each side played, not just who
# won: when its milestones landed, what it built in what order, how much of its
# income it threw away, and how much of the capacity it had paid for stood
# idle. ai-arena.ps1's table answers "did the change help"; this answers "why",
# which is the question the next change is made from.
#
#   python tools/arena-analyse.py <outDir>                 # every game, every side
#   python tools/arena-analyse.py <outDir> --game 2        # one game
#   python tools/arena-analyse.py <outDir> --brief         # the aggregate only
#   python tools/arena-analyse.py <outDir> --order 30      # longer build orders
#   python tools/arena-analyse.py <outDir> --json out.json # the same figures, for a script
#
# It reads three files per game, all written by the game itself in arena mode:
# game-N-ai-arena.csv (a sample every ten seconds per player),
# game-N-ai-arena-events.csv (one row per unit: started, completed, died) and
# game-N.log (the AI's own account of what it decided and what it could not
# do). Columns added later than a given run are simply reported as absent, so
# an old folder still reads.
#
# Plain Python 3, no dependencies. Use the MinGW one on this machine:
#   /d/msys64/mingw64/bin/python.exe tools/arena-analyse.py ...
import argparse
import csv
import glob
import json
import os
import re
import sys
from collections import Counter, defaultdict

CHECKPOINTS_MIN = [5, 10, 15, 20, 30, 45, 60, 90]

# A milestone is the first COMPLETED unit whose type ends with one of these,
# with the two-or-three letter side prefix taken off. Order is display order.
MILESTONES = [
    ("mex", ["MEX"]), ("solar", ["SOLAR"]), ("tidal", ["TIDE"]), ("lab", ["LAB"]), ("veh plant", ["VP"]),
    ("air plant", ["AP"]), ("shipyard", ["SY"]), ("radar", ["RAD"]), ("constructor", ["CK", "CV", "CA", "CS"]),
    ("metal store", ["MSTOR", "UWMS"]), ("energy store", ["ESTOR", "UWES"]), ("geothermal", ["GEO"]),
    ("adv lab", ["ALAB", "AVP", "AAP"]), ("adv shipyard", ["ASY"]), ("adv constructor", ["ACK", "ACV", "ACA", "ACSUB"]),
    ("moho", ["MOHO"]), ("fusion", ["FUS", "UWFUS", "CKFUS"]),
]

LOG_PATTERNS = [
    ("saving for", re.compile(r"AI build: unit \d+ saving for (\w+)")),
    ("cannot afford", re.compile(r"AI build: cannot afford (\w+)")),
    ("no site", re.compile(r"AI build: no site found for (\w+)")),
    ("contested", re.compile(r"AI build: (\w+) at .* is contested ground")),
    ("not worth it", re.compile(r"AI build: (\w+) at .* is not worth its metal")),
    ("factory starts", re.compile(r"AI factory: \w+ \d+ starts (\w+)")),
    ("factory held", re.compile(r"AI factory: \w+ \d+ holds (\w+) for the tier-two")),
]


def strip_side(unit_type):
    for prefix in ("ARM", "COR"):
        if unit_type.startswith(prefix):
            return unit_type[len(prefix):]
    return unit_type


def f(row, key, default=None):
    v = row.get(key)
    if v is None or v == "":
        return default
    try:
        return float(v)
    except ValueError:
        return default


def mmss(seconds):
    if seconds is None:
        return "  -  "
    seconds = int(seconds)
    return "%2d:%02d" % (seconds // 60, seconds % 60)


def read_game(folder, game):
    eco_path = os.path.join(folder, "game-%s-ai-arena.csv" % game)
    ev_path = os.path.join(folder, "game-%s-ai-arena-events.csv" % game)
    log_path = os.path.join(folder, "game-%s.log" % game)
    samples = defaultdict(list)
    with open(eco_path, newline="") as fh:
        for r in csv.DictReader(fh):
            samples[int(r["player"])].append(r)
    events = defaultdict(list)
    if os.path.exists(ev_path):
        with open(ev_path, newline="") as fh:
            for r in csv.DictReader(fh):
                events[int(r["player"])].append(r)
    decisions = {name: Counter() for name, _ in LOG_PATTERNS}
    if os.path.exists(log_path):
        with open(log_path, errors="replace") as fh:
            for line in fh:
                if "AI " not in line:
                    continue
                for name, pattern in LOG_PATTERNS:
                    m = pattern.search(line)
                    if m:
                        decisions[name][m.group(1)] += 1
                        break
    return samples, events, decisions


def analyse_player(samples, events):
    out = {}
    if not samples:
        return out
    last = samples[-1]
    out["side"] = last["side"]
    out["status"] = last["status"]
    out["seconds"] = int(last["seconds"])
    has_new = "idleBuilders" in last

    # Milestones.
    done = [(float(e["completedSeconds"]), e["unitType"]) for e in events if e.get("completedSeconds")]
    done.sort()
    milestones = {}
    for label, suffixes in MILESTONES:
        times = [t for t, u in done if strip_side(u) in suffixes]
        milestones[label] = {"first": times[0] if times else None, "count": len(times)}
    out["milestones"] = milestones

    # Build order: buildings and factories' first few units, by start time.
    started = [(float(e["startedSeconds"]), e) for e in events if e.get("startedSeconds") and e["category"] != "commander"]
    started.sort(key=lambda p: p[0])
    out["order"] = [
        {"start": t, "done": f(e, "completedSeconds"), "type": e["unitType"], "building": e["isBuilding"] == "1"}
        for t, e in started]

    # Checkpoints.
    checkpoints = []
    for minute in CHECKPOINTS_MIN:
        at = [s for s in samples if int(s["seconds"]) <= minute * 60]
        if not at or int(samples[-1]["seconds"]) < minute * 60 - 30:
            continue
        s = at[-1]
        checkpoints.append({
            "minute": minute,
            "metalIncome": f(s, "metalIncome"), "energyIncome": f(s, "energyIncome"),
            "metalFill": 100.0 * f(s, "metal", 0.0) / max(1.0, f(s, "maxMetal", 1.0)),
            "energyFill": 100.0 * f(s, "energy", 0.0) / max(1.0, f(s, "maxEnergy", 1.0)),
            "units": int(f(s, "units", 0)), "buildings": int(f(s, "buildings", 0)), "army": int(f(s, "army", 0)),
            "builders": int(f(s, "builders", 0)), "armyMetal": f(s, "armyMetal"),
            "idleBuilders": f(s, "idleBuilders"), "factories": f(s, "factories"), "idleFactories": f(s, "idleFactories"),
            "phase": s.get("phase", "-"),
        })
    out["checkpoints"] = checkpoints

    # Efficiency over the whole game.
    n = float(len(samples))
    def share(pred):
        return 100.0 * sum(1 for s in samples if pred(s)) / n
    eff = {
        "metalStalled": share(lambda s: f(s, "metal", 0) < 0.02 * max(1.0, f(s, "maxMetal", 1)) and f(s, "metalDemand", 0) > f(s, "metalIncome", 0)),
        "energyStalled": share(lambda s: f(s, "energy", 0) < 0.02 * max(1.0, f(s, "maxEnergy", 1)) and f(s, "energyDemand", 0) > f(s, "energyIncome", 0)),
        "metalAtCap": share(lambda s: f(s, "metal", 0) >= 0.95 * max(1.0, f(s, "maxMetal", 1))),
        "energyAtCap": share(lambda s: f(s, "energy", 0) >= 0.95 * max(1.0, f(s, "maxEnergy", 1))),
        "demandOverIncome": sum(f(s, "metalDemand", 0) / max(0.1, f(s, "metalIncome", 0)) for s in samples) / n,
    }
    if has_new:
        produced = f(last, "metalProduced", 0.0)
        eproduced = f(last, "energyProduced", 0.0)
        eff["metalProduced"] = produced
        eff["metalWastedPct"] = 100.0 * f(last, "metalExcess", 0.0) / max(1.0, produced)
        eff["energyWastedPct"] = 100.0 * f(last, "energyExcess", 0.0) / max(1.0, eproduced)
        b = [s for s in samples if f(s, "builders", 0) > 0]
        eff["idleBuilderPct"] = 100.0 * sum(f(s, "idleBuilders", 0) / f(s, "builders", 1) for s in b) / max(1.0, float(len(b)))
        fa = [s for s in samples if f(s, "factories", 0) > 0]
        eff["idleFactoryPct"] = 100.0 * sum(f(s, "idleFactories", 0) / f(s, "factories", 1) for s in fa) / max(1.0, float(len(fa)))
        eff["armyMetalPeak"] = max(f(s, "armyMetal", 0.0) for s in samples)
    out["efficiency"] = eff

    lost = Counter(e["unitType"] for e in events if e.get("diedSeconds"))
    built = Counter(e["unitType"] for e in events if e.get("completedSeconds"))
    out["built"] = dict(built)
    out["lost"] = dict(lost)
    return out


def print_player(game, player, a, order_length):
    print("  p%d %s  %s at %s" % (player, a["side"], a["status"], mmss(a["seconds"])))
    ms = a["milestones"]
    print("    milestones (first done / how many): " + "  ".join(
        "%s %s/%d" % (label, mmss(v["first"]).strip(), v["count"]) for label, v in ms.items() if v["count"] > 0))
    missing = [label for label, v in ms.items() if v["count"] == 0]
    if missing:
        print("    never built: " + ", ".join(missing))
    if order_length > 0:
        buildings = [o for o in a["order"] if o["building"]][:order_length]
        print("    building order: " + "  ".join("%s@%s" % (strip_side(o["type"]), mmss(o["start"]).strip()) for o in buildings))
        mobile = [o for o in a["order"] if not o["building"]][:order_length]
        print("    unit order:     " + "  ".join("%s@%s" % (strip_side(o["type"]), mmss(o["start"]).strip()) for o in mobile))
    print("    min  m/s   e/s  mFill eFill units bldg army armyMetal bldrs idleB fact idleF phase")
    for c in a["checkpoints"]:
        def opt(v, fmt):
            return (fmt % v) if v is not None else "   -"
        print("    %3d %5.1f %5.0f  %4.0f%% %4.0f%% %5d %4d %4d %9s %5d %5s %4s %5s %s" % (
            c["minute"], c["metalIncome"], c["energyIncome"], c["metalFill"], c["energyFill"], c["units"], c["buildings"],
            c["army"], opt(c["armyMetal"], "%.0f"), c["builders"], opt(c["idleBuilders"], "%.0f"), opt(c["factories"], "%.0f"),
            opt(c["idleFactories"], "%.0f"), c["phase"]))
    e = a["efficiency"]
    line = "    whole game: metal stalled %.0f%% of samples, energy stalled %.0f%%, metal at cap %.0f%%, energy at cap %.0f%%, demand/income %.2f" % (
        e["metalStalled"], e["energyStalled"], e["metalAtCap"], e["energyAtCap"], e["demandOverIncome"])
    print(line)
    if "metalWastedPct" in e:
        print("                metal produced %.0f, wasted %.1f%%; energy wasted %.1f%%; builders idle %.0f%% of builder-time; factories idle %.0f%%; peak army worth %.0f metal" % (
            e["metalProduced"], e["metalWastedPct"], e["energyWastedPct"], e["idleBuilderPct"], e["idleFactoryPct"], e["armyMetalPeak"]))


def print_decisions(decisions):
    for name, counter in decisions.items():
        if counter:
            print("    %-14s %s" % (name + ":", "  ".join("%s x%d" % (k, v) for k, v in counter.most_common(8))))


def aggregate(results):
    by_side = defaultdict(list)
    for r in results:
        for player, a in r["players"].items():
            if a:
                by_side[a["side"]].append(a)
    print("=== aggregate by side ===")
    for side, items in sorted(by_side.items()):
        n = float(len(items))
        print("  %s over %d games" % (side, len(items)))
        for label, _ in MILESTONES:
            times = [a["milestones"][label]["first"] for a in items if a["milestones"][label]["first"] is not None]
            if times:
                print("    %-16s reached in %d/%d games, mean first at %s, mean count %.1f" % (
                    label, len(times), len(items), mmss(sum(times) / len(times)).strip(),
                    sum(a["milestones"][label]["count"] for a in items) / n))
        keys = ["metalStalled", "energyStalled", "metalAtCap", "energyAtCap", "metalWastedPct", "energyWastedPct", "idleBuilderPct", "idleFactoryPct", "armyMetalPeak"]
        parts = []
        for k in keys:
            vals = [a["efficiency"][k] for a in items if k in a["efficiency"]]
            if vals:
                parts.append("%s %.1f" % (k, sum(vals) / len(vals)))
        print("    efficiency means: " + ", ".join(parts))
        for minute in CHECKPOINTS_MIN:
            cps = [c for a in items for c in a["checkpoints"] if c["minute"] == minute]
            if cps:
                print("    at %2d min (%d games): metal %.1f/s, energy %.0f/s, units %.1f, army %.1f, builders %.1f" % (
                    minute, len(cps), sum(c["metalIncome"] for c in cps) / len(cps), sum(c["energyIncome"] for c in cps) / len(cps),
                    sum(c["units"] for c in cps) / float(len(cps)), sum(c["army"] for c in cps) / float(len(cps)),
                    sum(c["builders"] for c in cps) / float(len(cps))))


def main():
    parser = argparse.ArgumentParser(description="Say how each side played in a folder of arena games.")
    parser.add_argument("folder")
    parser.add_argument("--game", help="only this game (its seed number, or e.g. 2-control)")
    parser.add_argument("--brief", action="store_true", help="the aggregate only")
    parser.add_argument("--order", type=int, default=14, help="how many entries of each build order to print (0 for none)")
    parser.add_argument("--json", help="also write every figure to this file")
    args = parser.parse_args()

    paths = sorted(glob.glob(os.path.join(args.folder, "game-*-ai-arena.csv")))
    games = [os.path.basename(p)[len("game-"):-len("-ai-arena.csv")] for p in paths]
    if args.game:
        games = [g for g in games if g == args.game]
    if not games:
        print("no games found in " + args.folder)
        return 1

    results = []
    for game in games:
        samples, events, decisions = read_game(args.folder, game)
        players = {p: analyse_player(samples[p], events[p]) for p in sorted(samples)}
        results.append({"game": game, "players": players, "decisions": {k: dict(v) for k, v in decisions.items()}})
        if args.brief:
            continue
        print("=== game %s ===" % game)
        for p, a in players.items():
            if a:
                print_player(game, p, a, args.order)
        print("  what the AI logged (both sides):")
        print_decisions(decisions)
        print()

    aggregate(results)
    if args.json:
        with open(args.json, "w") as fh:
            json.dump(results, fh, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
