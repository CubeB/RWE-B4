# Reduces a folder of per-game arena CSVs to the economy figures that the
# result line does not carry: how often each side sat metal-stalled, how far
# over its income it was committed, and when its economy milestones landed.
import csv, glob, os, sys
from collections import defaultdict

folder = sys.argv[1]
horizon = int(sys.argv[2]) if len(sys.argv) > 2 else 600

def fmt(x):
    return "%6.1f" % x if x is not None else "     -"

rows_out = []
for eco in sorted(glob.glob(os.path.join(folder, "game-*-ai-arena.csv"))):
    game = os.path.basename(eco).split("-")[1]
    ev = eco.replace("ai-arena.csv", "ai-arena-events.csv")
    samples = defaultdict(list)
    with open(eco) as f:
        for r in csv.DictReader(f):
            samples[int(r["player"])].append(r)
    events = defaultdict(list)
    if os.path.exists(ev):
        with open(ev) as f:
            for r in csv.DictReader(f):
                events[int(r["player"])].append(r)
    for p in sorted(samples):
        s = [r for r in samples[p] if int(r["seconds"]) <= horizon]
        n = len(s)
        stalled = sum(1 for r in s if float(r["metal"]) < 1.0 and float(r["metalDemand"]) > float(r["metalIncome"]))
        over = [float(r["metalDemand"]) / max(0.1, float(r["metalIncome"])) for r in s]
        full = sum(1 for r in s if float(r["metal"]) >= 0.95 * float(r["maxMetal"]))
        e = events[p]
        def nth(kind, k):
            done = sorted(float(r["completedSeconds"]) for r in e if r["unitType"] == kind and r["completedSeconds"])
            return done[k - 1] if len(done) >= k else None
        side = s[0]["side"] if s else "?"
        mex = "ARMMEX" if side == "ARM" else "CORMEX"
        firstFighter = sorted(float(r["completedSeconds"]) for r in e if r["category"] in ("army", "combat", "raider") and r["completedSeconds"])
        cats = defaultdict(int)
        for r in e:
            cats[r["category"]] += 1
        armyAt = next((int(r["army"]) for r in reversed(s)), 0)
        income = float(s[-1]["metalIncome"]) if s else 0
        def standing(kind):
            return sum(1 for r in e if r["unitType"] == kind and r["completedSeconds"] and float(r["completedSeconds"]) <= horizon and not (r["diedSeconds"] and float(r["diedSeconds"]) <= horizon))
        lab = "ARMLAB" if side == "ARM" else "CORLAB"
        rows_out.append((game, p, side, n, stalled, full, sum(over) / max(1, n), max(over) if over else 0, nth(mex, 4), nth(mex, 8), firstFighter[0] if firstFighter else None, armyAt, income, standing(mex), standing(lab)))

print("horizon %ds" % horizon)
print("game p side  stalled%% full%%  demand/income mean  max   mex4    mex8   1stArmy armyAtEnd income mex labs")
for g, p, side, n, st, fu, mean, mx, m4, m8, ff, army, inc, mexes, labs in rows_out:
    print("%4s %d %-5s %6.0f %5.0f   %8.1f %13.1f %s %s %s %5d %6.1f %3d %4d" % (g, p, side, 100.0 * st / max(1, n), 100.0 * fu / max(1, n), mean, mx, fmt(m4), fmt(m8), fmt(ff), army, inc, mexes, labs))
