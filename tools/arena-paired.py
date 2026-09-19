import re, sys, os, glob
# For each seed: the tuned player's result in the tuned game against the same
# player's result in the control game of that seed. Odd seeds tune p0, even p1.
pat = re.compile(r'p(\d)=(\w+) (\w+) units=(\d+) buildings=(\d+) army=(\d+)')
for folder in sys.argv[1:]:
    print('==', os.path.basename(folder))
    score = {}
    for log in sorted(glob.glob(os.path.join(folder, 'game-*.log')), key=lambda p: (int(re.search(r'game-(\d+)', p).group(1)), 'control' in p)):
        seed = int(re.search(r'game-(\d+)', log).group(1))
        control = 'control' in log
        line = [l for l in open(log, encoding='utf-8', errors='replace') if 'AI-ARENA-RESULT' in l]
        if not line:
            continue
        res = {int(m.group(1)): (m.group(2), m.group(3), int(m.group(4)), int(m.group(6))) for m in pat.finditer(line[-1])}
        score.setdefault(seed, {})[control] = res
    tally = {}
    for seed in sorted(score):
        if True not in score[seed] or False not in score[seed]:
            continue
        p = 0 if seed % 2 == 1 else 1
        t, c = score[seed][False], score[seed][True]
        def margin(r):
            me, other = r[p], r[1 - p]
            return (me[2] if me[1] == 'alive' else 0) - (other[2] if other[1] == 'alive' else 0)
        mt, mc = margin(t), margin(c)
        side = t[p][0]
        verdict = 'better' if mt > mc + 10 else 'worse' if mt < mc - 10 else 'same'
        tally.setdefault(side, []).append(verdict)
        print('  seed %2d %-4s tuned margin %+4d  control margin %+4d  %s' % (seed, side, mt, mc, verdict))
    for side, v in tally.items():
        print('  %s tuned: better %d, same %d, worse %d' % (side, v.count('better'), v.count('same'), v.count('worse')))
