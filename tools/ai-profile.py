import re, sys, collections
pat = re.compile(r'AI profile summary: player (\d+) at tick (\d+), total ([\d.]+) ms: (.*)')
item = re.compile(r'(\w+)=([\d.]+)ms/(\d+) \(worst ([\d.]+)ms, (\d+) spikes\)')
for path in sys.argv[1:]:
    tot = collections.defaultdict(float); worst = collections.defaultdict(float); calls = collections.defaultdict(int)
    windows = []
    for line in open(path, encoding='utf-8', errors='replace'):
        m = pat.search(line)
        if not m:
            continue
        windows.append((float(m.group(3)), int(m.group(2)), int(m.group(1)), m.group(4)))
        for n, t, c, w, s in item.findall(m.group(4)):
            tot[n] += float(t); calls[n] += int(c); worst[n] = max(worst[n], float(w))
    print(path, 'windows', len(windows), 'sum %.0f ms' % sum(tot.values()))
    for n in sorted(tot, key=lambda k: -tot[k]):
        print('  %-24s %9.1f ms  calls %7d  mean %.4f  worst %.2f' % (n, tot[n], calls[n], tot[n] / max(1, calls[n]), worst[n]))
    windows.sort(reverse=True)
    for w in windows[:3]:
        print('  heaviest window: %.1f ms at tick %d player %d' % w[:3])
        print('    ' + ' '.join('%s=%.0f' % (n, float(t)) for n, t, c, ww, s in item.findall(w[3]) if float(t) > 5))
