#!/usr/bin/env python3
"""Where things died in an arena run, and what they were up against.

    arena-deaths.py <outDir> [--before MINUTES] [--game N]

Reads the -events.csv files an arena run leaves behind and answers, per side:
which extractors were lost, how far from home, to how many armed enemies and of
what type, and whether anything of their own side was standing near; and where
the mobile army died -- at home, in the middle, or at the enemy's door.

"Home" is the middle of a side's buildings other than its extractors, which is
the base and not the outposts. Distances are given as a share of the way from
one home to the other, so 0.0 is its own base and 1.0 is the enemy's.
"""
import argparse
import collections
import csv
import glob
import os
import re


def load(path):
    with open(path, newline='') as fh:
        return [r for r in csv.DictReader(fh) if r.get('x') not in (None, '')]


def home(rows, player):
    pts = [(float(r['x']), float(r['z'])) for r in rows
           if int(r['player']) == player and r['isBuilding'] == '1'
           and r['category'] != 'extractor' and not r['unitType'].endswith('MEX')
           and r['completedTick']]
    if not pts:
        return None
    return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))


def share(r, mine, theirs):
    """How far along the line from my home to theirs this unit stood."""
    dx, dz = theirs[0] - mine[0], theirs[1] - mine[1]
    length2 = dx * dx + dz * dz
    if length2 == 0:
        return 0.0
    return ((float(r['x']) - mine[0]) * dx + (float(r['z']) - mine[1]) * dz) / length2


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('folder')
    ap.add_argument('--before', type=float, default=None, help='only deaths before this many minutes')
    ap.add_argument('--game', type=int, default=None)
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.folder, 'game-*-ai-arena-events.csv')),
                   key=lambda p: int(re.search(r'game-(\d+)', p).group(1)))
    totals = collections.defaultdict(lambda: collections.defaultdict(list))
    games = 0
    for path in files:
        seed = int(re.search(r'game-(\d+)', path).group(1))
        if args.game is not None and seed != args.game:
            continue
        rows = load(path)
        if not rows:
            print('game %d: no position columns; this run predates them' % seed)
            continue
        homes = {p: home(rows, p) for p in (0, 1)}
        if None in homes.values():
            continue
        games += 1
        sides = {}
        for r in rows:
            sides.setdefault(int(r['player']), r['unitType'][:3])
        for r in rows:
            if not r['diedTick'] or not r['completedTick']:
                continue
            minutes = float(r['diedSeconds']) / 60.0
            if args.before is not None and minutes >= args.before:
                continue
            p = int(r['player'])
            side = sides[p]
            s = share(r, homes[p], homes[1 - p])
            rec = (seed, minutes, s, int(r['enemiesNear'] or 0), r['nearestEnemyType'],
                   int(r['friendlyArmyNear'] or 0), int(r['friendlyTowersNear'] or 0), r['unitType'])
            if r['unitType'].endswith('MEX') or r['unitType'].endswith('MOHO'):
                totals[side]['extractor'].append(rec)
            elif r['category'] == 'army' and r['isBuilding'] == '0':
                totals[side]['army'].append(rec)

    if not games:
        return
    window = 'before %g min' % args.before if args.before else 'whole game'
    print('%d game(s), %s' % (games, window))
    for side in sorted(totals):
        ex = totals[side]['extractor']
        army = totals[side]['army']
        print('\n%s' % side)
        print('  extractors lost: %.1f a game' % (len(ex) / games))
        if ex:
            print('    way to the enemy:   home<0.25 %d   0.25-0.5 %d   past half %d' % (
                sum(1 for e in ex if e[2] < 0.25), sum(1 for e in ex if 0.25 <= e[2] < 0.5), sum(1 for e in ex if e[2] >= 0.5)))
            print('    armed enemies near: mean %.1f   (1-3: %d, 4-8: %d, 9+: %d, none: %d)' % (
                sum(e[3] for e in ex) / len(ex), sum(1 for e in ex if 1 <= e[3] <= 3), sum(1 for e in ex if 4 <= e[3] <= 8),
                sum(1 for e in ex if e[3] >= 9), sum(1 for e in ex if e[3] == 0)))
            print('    own army near: none at %d of %d;  own tower near: none at %d of %d' % (
                sum(1 for e in ex if e[5] == 0), len(ex), sum(1 for e in ex if e[6] == 0), len(ex)))
            killers = collections.Counter(e[4] or '-' for e in ex)
            print('    nearest enemy: ' + '  '.join('%s %d' % kv for kv in killers.most_common(6)))
        print('  army lost: %.1f a game' % (len(army) / games))
        if army:
            print('    died: own half, near home (<0.25) %d   own half (0.25-0.5) %d   enemy half (0.5-0.75) %d   at the enemy base (>0.75) %d' % (
                sum(1 for a in army if a[2] < 0.25), sum(1 for a in army if 0.25 <= a[2] < 0.5),
                sum(1 for a in army if 0.5 <= a[2] < 0.75), sum(1 for a in army if a[2] >= 0.75)))
            print('    odds when it died: mean %.1f enemies near against %.1f of its own army and %.1f own towers' % (
                sum(a[3] for a in army) / len(army), sum(a[5] for a in army) / len(army), sum(a[6] for a in army) / len(army)))
            near_tower = collections.Counter(a[4] or '-' for a in army)
            print('    nearest enemy: ' + '  '.join('%s %d' % kv for kv in near_tower.most_common(6)))


if __name__ == '__main__':
    main()
