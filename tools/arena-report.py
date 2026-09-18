#!/usr/bin/env python3
"""Turns one AI arena game into a page you can read.

The arena writes two CSVs: economy samples every ten seconds, and one row per
unit saying when it was started, when it finished and when it died. Numbers in
a spreadsheet answer "did this change help"; they do not answer "what on earth
was it doing at four minutes", which is the question you ask when you want to
tell the AI it is playing badly.

    python tools/arena-report.py                       # the last run
    python tools/arena-report.py path/to/ai-arena.csv
    python tools/arena-report.py --out D:/RWE/game.html

Writes a self-contained HTML file. No libraries, no build step: the charts are
inline SVG and the only thing fetched is the webfont.
"""

import csv
import os
import sys

TICKS_PER_SECOND = 30

# The categories the game writes, in the order they are stacked, with the
# colour each gets. Ordered as an economy is actually built rather than
# alphabetically, so a healthy opening reads top to bottom.
CATEGORIES = [
    ("commander", "Commander", "#c9b458"),
    ("economy", "Economy", "#4f9d69"),
    ("factory", "Factories", "#5b8bb5"),
    ("builder", "Builders", "#7fb3d5"),
    ("support", "Radar & storage", "#8a8fa3"),
    ("defence", "Defences", "#b5793f"),
    ("scout", "Scouts", "#9d7fb5"),
    ("army", "Army", "#c25b4e"),
    ("other", "Other", "#6b7280"),
]
CATEGORY_COLOUR = {k: c for k, _, c in CATEGORIES}
CATEGORY_LABEL = {k: l for k, l, _ in CATEGORIES}


def default_csv():
    appdata = os.environ.get("APPDATA")
    if appdata:
        p = os.path.join(appdata, "RWE", "ai-arena.csv")
        if os.path.exists(p):
            return p
    return "ai-arena.csv"


def read_rows(path):
    with open(path, newline="") as f:
        return [r for r in csv.DictReader(f)]


def num(v, default=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def esc(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def polyline(points, width, height, xmax, ymax, colour, dashed=False):
    if not points or ymax <= 0:
        return ""
    pts = []
    for x, y in points:
        px = (x / xmax) * width if xmax else 0
        py = height - (min(y, ymax) / ymax) * height
        pts.append("%.1f,%.1f" % (px, py))
    dash = ' stroke-dasharray="4 3"' if dashed else ""
    return ('<polyline fill="none" stroke="%s" stroke-width="1.6" '
            'stroke-linejoin="round" points="%s"%s/>' % (colour, " ".join(pts), dash))


def chart(title, series, seconds_max, height=110, width=560):
    """series: list of (label, colour, [(seconds, value)], dashed)."""
    ymax = 1.0
    for _, _, pts, _ in series:
        for _, v in pts:
            ymax = max(ymax, v)
    ymax *= 1.08

    grid = []
    for frac in (0.25, 0.5, 0.75, 1.0):
        y = height - frac * height
        grid.append('<line x1="0" y1="%.1f" x2="%d" y2="%.1f" class="grid"/>' % (y, width, y))
    # A minute mark, so the eye can find four minutes without counting.
    for minute in range(1, int(seconds_max // 60) + 1):
        x = (minute * 60 / seconds_max) * width
        grid.append('<line x1="%.1f" y1="0" x2="%.1f" y2="%d" class="grid vgrid"/>' % (x, x, height))

    lines = [polyline(pts, width, height, seconds_max, ymax, colour, dashed)
             for _, colour, pts, dashed in series]

    key = " ".join(
        '<span class="key"><i style="background:%s"></i>%s</span>' % (colour, esc(label))
        for label, colour, _, _ in series)

    return ('<figure class="chart">'
            '<figcaption>%s <span class="ymax">peak %d</span></figcaption>'
            '<svg viewBox="0 0 %d %d" preserveAspectRatio="none" role="img" aria-label="%s">'
            '%s%s</svg><div class="keys">%s</div></figure>'
            % (esc(title), ymax, width, height, esc(title), "".join(grid), "".join(lines), key))


def timeline(events, seconds_max, width=560):
    """A bar per unit: started to finished, then a thin tail while it lives."""
    order = {k: i for i, (k, _, _) in enumerate(CATEGORIES)}
    events = sorted(events, key=lambda e: (order.get(e["category"], 99),
                                           e["unitType"],
                                           num(e["startedSeconds"])))
    row_h = 13
    height = max(row_h * len(events), row_h)
    bars = []
    for i, e in enumerate(events):
        y = i * row_h
        colour = CATEGORY_COLOUR.get(e["category"], "#6b7280")
        start = num(e["startedSeconds"])
        done = num(e["completedSeconds"], start)
        died = e["diedSeconds"]
        end = num(died, seconds_max) if died else seconds_max

        x0 = (start / seconds_max) * width
        x1 = (max(done, start) / seconds_max) * width
        x2 = (end / seconds_max) * width

        # The build itself, solid; the life after it, a thin line. Reading the
        # solid part is reading how long the AI spent on that thing.
        if x2 > x1:
            bars.append('<rect x="%.1f" y="%.1f" width="%.1f" height="2" fill="%s" opacity="0.35"/>'
                        % (x1, y + row_h / 2 - 1, max(x2 - x1, 0.6), colour))
        bars.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%d" fill="%s" rx="1">'
                    '<title>%s  started %ds, %s%s</title></rect>'
                    % (x0, y + 2, max(x1 - x0, 1.5), row_h - 4, colour,
                       esc(e["unitType"]), int(start),
                       ("finished %ds" % int(done)) if e["completedSeconds"] else "never finished",
                       (", died %ds" % int(num(died))) if died else ""))
        if died:
            bars.append('<rect x="%.1f" y="%.1f" width="2.5" height="%d" fill="#e0524a"/>'
                        % (x2, y + 1, row_h - 2))

    labels = []
    for i, e in enumerate(events):
        labels.append('<div class="tl-label" style="top:%dpx">%s</div>' % (i * row_h, esc(e["unitType"])))

    vgrid = []
    for minute in range(1, int(seconds_max // 60) + 1):
        x = (minute * 60 / seconds_max) * width
        vgrid.append('<line x1="%.1f" y1="0" x2="%.1f" y2="%d" class="grid vgrid"/>' % (x, x, height))

    return ('<div class="timeline"><div class="tl-labels" style="height:%dpx">%s</div>'
            '<svg viewBox="0 0 %d %d" preserveAspectRatio="none" class="tl-svg" style="height:%dpx">'
            '%s%s</svg></div>' % (height, "".join(labels), width, height, height,
                                  "".join(vgrid), "".join(bars)))


def build(csv_path, out_path):
    rows = read_rows(csv_path)
    events_path = os.path.join(os.path.dirname(csv_path),
                               os.path.splitext(os.path.basename(csv_path))[0] + "-events.csv")
    events = read_rows(events_path) if os.path.exists(events_path) else []

    if not rows:
        raise SystemExit("no rows in " + csv_path)

    seconds_max = max(num(r["seconds"]) for r in rows) or 1
    players = sorted({int(r["player"]) for r in rows})

    sections = []
    summary_cards = []
    for p in players:
        prows = [r for r in rows if int(r["player"]) == p]
        pev = [e for e in events if int(e["player"]) == p]
        side = prows[-1]["side"]
        last = prows[-1]

        counts = {}
        for e in pev:
            counts[e["category"]] = counts.get(e["category"], 0) + 1
        built = " ".join(
            '<span class="chip"><i style="background:%s"></i>%s %d</span>'
            % (CATEGORY_COLOUR.get(k, "#6b7280"), esc(CATEGORY_LABEL.get(k, k)), counts[k])
            for k, _, _ in CATEGORIES if k in counts)

        lost = sum(1 for e in pev if e["diedSeconds"])
        stalls = sum(1 for r in prows if num(r["metal"]) <= 0.01 or num(r["energy"]) <= 0.01)
        stall_pct = 100.0 * stalls / max(len(prows), 1)

        summary_cards.append(
            '<div class="card p%d"><h3>%s <span class="slot">player %d</span></h3>'
            '<dl><div><dt>Army</dt><dd>%s</dd></div>'
            '<div><dt>Buildings</dt><dd>%s</dd></div>'
            '<div><dt>Metal income</dt><dd>%.1f</dd></div>'
            '<div><dt>Lost</dt><dd>%d</dd></div>'
            '<div><dt>Starved</dt><dd class="%s">%.0f%% of the game</dd></div></dl></div>'
            % (p, esc(side), p, esc(last["army"]), esc(last["buildings"]),
               num(last["metalIncome"]), lost,
               "bad" if stall_pct > 40 else ("warn" if stall_pct > 15 else ""), stall_pct))

        metal = chart("Metal: income against demand", [
            ("income", "#4f9d69", [(num(r["seconds"]), num(r["metalIncome"])) for r in prows], False),
            ("demand", "#c25b4e", [(num(r["seconds"]), num(r["metalDemand"])) for r in prows], True),
        ], seconds_max)
        energy = chart("Energy: income against demand", [
            ("income", "#c9b458", [(num(r["seconds"]), num(r["energyIncome"])) for r in prows], False),
            ("demand", "#c25b4e", [(num(r["seconds"]), num(r["energyDemand"])) for r in prows], True),
        ], seconds_max)
        stock = chart("Stockpile", [
            ("metal", "#4f9d69", [(num(r["seconds"]), num(r["metal"])) for r in prows], False),
            ("energy", "#c9b458", [(num(r["seconds"]), num(r["energy"])) for r in prows], False),
        ], seconds_max)
        force = chart("What it had standing", [
            ("army", "#c25b4e", [(num(r["seconds"]), num(r["army"])) for r in prows], False),
            ("buildings", "#5b8bb5", [(num(r["seconds"]), num(r["buildings"])) for r in prows], False),
            ("builders", "#7fb3d5", [(num(r["seconds"]), num(r["builders"])) for r in prows], False),
        ], seconds_max)

        sections.append(
            '<section class="player p%d"><header><h2>%s</h2><div class="chips">%s</div></header>'
            '<div class="charts">%s%s%s%s</div>'
            '<h4>Every unit, when it was started and how long it took</h4>%s</section>'
            % (p, esc(side + " (player " + str(p) + ")"), built,
               metal, energy, stock, force, timeline(pev, seconds_max)))

    minutes = int(seconds_max // 60)
    # A name that says which game this was, so a folder of them can be told
    # apart at a glance rather than all reading "Arena Game Review".
    sides = " v ".join(dict.fromkeys(
        [r["side"] for r in sorted(rows, key=lambda r: int(r["player"]))]))
    title = "%s, %d Minutes" % (sides, minutes)
    # Token replacement rather than %-formatting: the stylesheet is full of
    # per-cent signs and every one of them would have to be doubled.
    html = (TEMPLATE
            .replace("@@TITLE@@", esc(title))
            .replace("@@MINUTES@@", str(minutes))
            .replace("@@CARDS@@", "".join(summary_cards))
            .replace("@@SECTIONS@@", "".join(sections))
            .replace("@@SOURCE@@", esc(csv_path)))
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(html)
    return out_path


TEMPLATE = """<title>@@TITLE@@</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Oswald:wght@400;600&family=IBM+Plex+Sans:wght@400;600&family=IBM+Plex+Mono:wght@400;600&display=swap">
<style>
:root {
  --ground: #f2f1ee;
  --panel: #ffffff;
  --ink: #23262d;
  --ink-soft: #5d626e;
  --rule: #d9d7d1;
  --grid: #e6e4de;
  --arm: #3f6f9f;
  --core: #a8443a;
  --warn: #b5793f;
  --bad: #c25b4e;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --ground: #16181c;
    --panel: #1d2026;
    --ink: #e6e6e4;
    --ink-soft: #9aa0ad;
    --rule: #2e323a;
    --grid: #282c33;
    --arm: #6fa3d6;
    --core: #d4736a;
  }
}
:root[data-theme="dark"] {
  --ground: #16181c;
  --panel: #1d2026;
  --ink: #e6e6e4;
  --ink-soft: #9aa0ad;
  --rule: #2e323a;
  --grid: #282c33;
  --arm: #6fa3d6;
  --core: #d4736a;
}
body {
  background: var(--ground);
  color: var(--ink);
  font-family: "IBM Plex Sans", system-ui, -apple-system, sans-serif;
  font-size: 14px;
  line-height: 1.5;
  margin: 0;
  padding: 28px 20px 64px;
}
.wrap { max-width: 1120px; margin: 0 auto; display: flex; flex-direction: column; gap: 26px; }
h1 {
  font-family: Oswald, "Arial Narrow", sans-serif;
  font-weight: 600; font-size: 30px; letter-spacing: 0.02em;
  text-transform: uppercase; margin: 0; text-wrap: balance;
}
.sub { color: var(--ink-soft); margin: 4px 0 0; }
h2 {
  font-family: Oswald, "Arial Narrow", sans-serif;
  font-weight: 600; font-size: 19px; letter-spacing: 0.03em;
  text-transform: uppercase; margin: 0;
}
h3 { font-family: Oswald, sans-serif; font-weight: 600; font-size: 15px; letter-spacing: .04em; text-transform: uppercase; margin: 0 0 10px; }
h4 { font-size: 12px; text-transform: uppercase; letter-spacing: .08em; color: var(--ink-soft); margin: 22px 0 8px; font-weight: 600; }
.cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 14px; }
.card { background: var(--panel); border: 1px solid var(--rule); border-radius: 3px; padding: 14px 16px; border-left: 3px solid var(--arm); }
.card.p1 { border-left-color: var(--core); }
.slot { font-family: "IBM Plex Mono", monospace; font-size: 11px; color: var(--ink-soft); letter-spacing: 0; text-transform: none; }
dl { margin: 0; display: grid; gap: 3px; }
dl > div { display: flex; justify-content: space-between; gap: 12px; }
dt { color: var(--ink-soft); font-size: 12px; }
dd { margin: 0; font-family: "IBM Plex Mono", monospace; font-variant-numeric: tabular-nums; font-weight: 600; }
dd.warn { color: var(--warn); }
dd.bad { color: var(--bad); }
section.player { background: var(--panel); border: 1px solid var(--rule); border-radius: 3px; padding: 18px 20px 22px; border-top: 3px solid var(--arm); }
section.player.p1 { border-top-color: var(--core); }
section.player > header { display: flex; flex-wrap: wrap; align-items: baseline; gap: 10px 18px; margin-bottom: 14px; }
.chips, .keys { display: flex; flex-wrap: wrap; gap: 5px 12px; }
.chip, .key { font-size: 11px; color: var(--ink-soft); display: inline-flex; align-items: center; gap: 5px; }
.chip i, .key i { width: 9px; height: 9px; border-radius: 2px; display: inline-block; }
.charts { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 16px; }
.chart { margin: 0; }
figcaption { font-size: 12px; color: var(--ink-soft); margin-bottom: 4px; display: flex; justify-content: space-between; }
.ymax { font-family: "IBM Plex Mono", monospace; font-variant-numeric: tabular-nums; }
.chart svg { width: 100%; height: 110px; display: block; background: transparent; }
.grid { stroke: var(--grid); stroke-width: 1; }
.vgrid { stroke-dasharray: 2 4; }
.timeline { position: relative; display: flex; gap: 10px; overflow-x: auto; }
.tl-labels { position: relative; flex: 0 0 118px; font-family: "IBM Plex Mono", monospace; font-size: 10px; color: var(--ink-soft); }
.tl-label { position: absolute; left: 0; height: 13px; line-height: 13px; white-space: nowrap; }
.tl-svg { flex: 1 1 auto; min-width: 380px; }
footer { color: var(--ink-soft); font-size: 12px; border-top: 1px solid var(--rule); padding-top: 12px; }
code { font-family: "IBM Plex Mono", monospace; font-size: 12px; }
</style>
<div class="wrap">
  <header>
    <h1>@@TITLE@@</h1>
    <p class="sub">@@MINUTES@@ minutes, computer against computer. The solid bar is how long a
      thing took to build; the thin tail is how long it then lived; a red cap is where it died.</p>
  </header>
  <div class="cards">@@CARDS@@</div>
  @@SECTIONS@@
  <footer>Generated by <code>tools/arena-report.py</code> from <code>@@SOURCE@@</code>.
    Vertical guides are one minute apart.</footer>
</div>
"""


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = None
    for i, a in enumerate(sys.argv):
        if a == "--out" and i + 1 < len(sys.argv):
            out = sys.argv[i + 1]
    csv_path = args[0] if args else default_csv()
    if not os.path.exists(csv_path):
        raise SystemExit("no such file: " + csv_path)
    if out is None:
        out = os.path.join(os.path.dirname(csv_path) or ".", "ai-arena.html")
    print(build(csv_path, out))


if __name__ == "__main__":
    main()
