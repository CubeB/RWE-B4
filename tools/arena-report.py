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
    python tools/arena-report.py --run <dir>            # a whole run folder
    python tools/arena-report.py --run <dir> --text     # sections 2/4/5 as text

Writes a self-contained HTML file. No libraries, no build step: the charts are
inline SVG and the only thing fetched is the webfont, with a system-font
fallback if it does not load.

When ``event-log.jsonl`` sits beside the CSV (as ``ai_arena --out`` and
``tools/playtest/run.py`` both leave it), the page grows an "AI review" with
six more views built from it: a decision timeline, a why-tags table, a death
map, a fights table, and a chronological list of moments. Without that file
the report is exactly what it always was.
"""

import csv
import json
import os
import sys
from collections import defaultdict

TICKS_PER_SECOND = 30

EVENT_LOG_NAME = "event-log.jsonl"
RUN_JSON_NAME = "run.json"

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

# Fight clustering (see docstring on cluster_fights): a death continues an
# open cluster if it is within this many world units of one of the cluster's
# existing deaths and within this many seconds of the cluster's last one.
FIGHT_SPACE_RADIUS = 600.0
FIGHT_TIME_GAP = 20.0
FIGHT_MIN_DEATHS = 2

# Decision-timeline lanes, in the order they are stacked, and the colour each
# gets when it is not a refusal/failure marker (those are all one colour, see
# REFUSAL_COLOUR, so a red streak reads as "this kept not working" at a
# glance regardless of which system it was).
AREA_ORDER = ["build", "factory", "army", "navy", "scout", "transport", "commander", "economy", "other"]
AREA_COLOUR = {
    "build": "#5b8bb5",
    "factory": "#3f6f9f",
    "army": "#c25b4e",
    "navy": "#2f7d8f",
    "scout": "#9d7fb5",
    "transport": "#8a8fa3",
    "commander": "#c9b458",
    "economy": "#4f9d69",
    "other": "#6b7280",
}
REFUSAL_COLOUR = "#e0524a"
PHASE_PALETTE = ["#3f6f9f", "#c9b458", "#4f9d69", "#c25b4e", "#9d7fb5", "#5b8bb5", "#b5793f", "#8a8fa3"]

# A lane with more markers than this is bucketed by time (see thin_lane) so a
# 1900-refusal lane still renders as a light SVG rather than one circle per
# refusal.
DECISION_WIDTH = 720
MAX_MARKERS_PER_LANE = 220

# Colours for player markers beyond the usual two (ARM/CORE): the arena
# almost always runs 1v1, but nothing stops a bigger game.
PLAYER_COLOURS = ["#8a8fa3", "#c9b458", "#9d7fb5", "#4f9d69", "#b5793f", "#5b8bb5"]

# Death-marker size when the log carries no cost for the unit that died
# (which is normal -- see unit_deaths below), keyed by the categorise()
# buckets AiArenaReport.cpp writes to the events CSV. This is a rough stand-in
# for value, not a real one, and every place that uses it says so.
DEATH_SIZE_BY_CATEGORY = {
    "commander": 10.0,
    "factory": 7.5,
    "economy": 5.0,
    "defence": 5.5,
    "support": 4.5,
    "builder": 4.0,
    "army": 4.0,
    "scout": 3.0,
    "other": 3.5,
}
DEFAULT_DEATH_SIZE = 3.5

# An ai_status sample with at least this many idle builders counts as a
# "spike" worth a moment, provided it is a local peak (see build_moments).
IDLE_BUILDER_SPIKE_MIN = 2


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


def mmss(seconds):
    if seconds is None:
        return "  -  "
    seconds = int(seconds)
    return "%d:%02d" % (seconds // 60, seconds % 60)


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


# --------------------------------------------------------------------------
# event-log.jsonl: parsing and the shared vocabulary helpers (see also
# tools/playtest/q.py's docstring, which pins the field names below).
# --------------------------------------------------------------------------

def read_event_log(path):
    """Tolerant line-by-line parse. A malformed line, or a line that is valid
    JSON but not an object, or an object with no ``ev``, is skipped rather
    than failing the whole report -- the log is written a line at a time by a
    running game, so a truncated last line is normal, not corruption."""
    events = []
    if not path or not os.path.exists(path):
        return events
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                e = json.loads(line)
            except json.JSONDecodeError:
                continue
            if not isinstance(e, dict) or "ev" not in e:
                continue
            events.append(e)
    return events


def ev_player(e):
    try:
        return int(e.get("player"))
    except (TypeError, ValueError):
        return None


def ev_secs(e):
    try:
        return float(e.get("secs"))
    except (TypeError, ValueError):
        return None


def derive_area(ev):
    """The decision-timeline lane an event name belongs to. Commander orders
    are their own lane even though they are logged as ``army_commander_*``,
    because "what is the commander doing" is a different question from "what
    is the army doing"."""
    if not ev:
        return "other"
    if ev.startswith("army_commander"):
        return "commander"
    prefix = ev.split("_", 1)[0]
    if prefix in ("build", "factory", "army", "navy", "scout", "transport"):
        return prefix
    return "other"


def is_refusal(ev):
    """Whether an event name is a refusal/failure worth colouring apart from
    an ordinary decision. Follows the families named in the design brief
    literally, including the whole ``build_site_*`` family even though
    ``build_site_search`` can also report success (``why: "found"``) -- a
    site search is still "the first attempt didn't work" by the time one is
    logged at all."""
    if not ev:
        return False
    if ev.endswith("_refusal") or ev.endswith("_unaffordable") or ev.endswith("_not_worth"):
        return True
    if ev.startswith("build_site_"):
        return True
    return False


def is_decision_event(e):
    """True for the events the decision timeline and why-tags table draw
    from: everything except the ``ai_*`` status/telemetry stream, the
    pathfinder counters, and unit deaths (which get their own sections)."""
    ev = e.get("ev")
    if not ev or ev.startswith("ai_") or ev in ("path_stats", "unit_death"):
        return False
    return ev_player(e) is not None


def _stable_index(name, n):
    """A hash that does not depend on PYTHONHASHSEED, so the same phase name
    always gets the same background colour across runs of this script."""
    h = 0
    for ch in name:
        h = (h * 31 + ord(ch)) & 0xFFFFFFFF
    return h % n if n else 0


def phase_colour(name):
    return PHASE_PALETTE[_stable_index(name or "", len(PHASE_PALETTE))]


def player_colour(p):
    if p == 0:
        return "var(--arm)"
    if p == 1:
        return "var(--core)"
    return PLAYER_COLOURS[(p - 2) % len(PLAYER_COLOURS)]


def phase_bands(log_events, player, seconds_max):
    """``[(start_secs, end_secs, phase_name), ...]`` for one player, covering
    ``[0, seconds_max]`` with no gaps when there is enough to build one.
    Prefers ``ai_transition`` (kind ``"phase"``) records, which name both
    sides of the change; falls back to collapsing consecutive ``ai_status``
    samples of the same phase when no transition was logged."""
    transitions = []
    statuses = []
    for e in log_events:
        if ev_player(e) != player:
            continue
        ev = e.get("ev")
        s = ev_secs(e)
        if s is None:
            continue
        if ev == "ai_transition" and e.get("kind") == "phase" and e.get("to"):
            transitions.append((s, e.get("from"), e.get("to")))
        elif ev == "ai_status" and e.get("phase"):
            statuses.append((s, e["phase"]))

    transitions.sort(key=lambda t: t[0])
    bands = []
    if transitions:
        first_s, first_from, _ = transitions[0]
        if first_from:
            bands.append((0.0, first_s, first_from))
        for i, (s, _frm, to) in enumerate(transitions):
            end = transitions[i + 1][0] if i + 1 < len(transitions) else seconds_max
            if end > s:
                bands.append((s, end, to))
        return bands

    if statuses:
        statuses.sort(key=lambda t: t[0])
        cur_phase, cur_start = None, 0.0
        for s, ph in statuses:
            if ph != cur_phase:
                if cur_phase is not None:
                    bands.append((cur_start, s, cur_phase))
                cur_phase, cur_start = ph, s
        bands.append((cur_start, seconds_max, cur_phase))
    return bands


def collect_timeline_events(log_events, player):
    """Decision-timeline markers for one player, sorted by time: every
    decision event, plus a synthetic ``economy_stall`` marker at the start of
    each metal/energy stall spell an ``ai_status`` sample reports (there is
    no logged ``economy_*`` decision event, so this is the only way the
    "economy" lane gets anything to show)."""
    out = []
    for e in log_events:
        if not is_decision_event(e) or ev_player(e) != player:
            continue
        s = ev_secs(e)
        if s is None:
            continue
        ev = e.get("ev")
        out.append({
            "secs": s, "ev": ev, "area": derive_area(ev),
            "why": e.get("why"), "subject": e.get("subject"), "detail": e.get("detail"),
            "refusal": is_refusal(ev),
        })

    statuses = sorted(
        (e for e in log_events if e.get("ev") == "ai_status" and ev_player(e) == player),
        key=lambda e: ev_secs(e) if ev_secs(e) is not None else 0.0)
    prev_metal, prev_energy = False, False
    for e in statuses:
        s = ev_secs(e)
        if s is None:
            continue
        m, en = bool(e.get("metal_stalled")), bool(e.get("energy_stalled"))
        if m and not prev_metal:
            out.append({"secs": s, "ev": "economy_stall", "area": "economy", "why": "metal_stalled",
                        "subject": "metal", "detail": "metal stall begins", "refusal": True})
        if en and not prev_energy:
            out.append({"secs": s, "ev": "economy_stall", "area": "economy", "why": "energy_stalled",
                        "subject": "energy", "detail": "energy stall begins", "refusal": True})
        prev_metal, prev_energy = m, en

    out.sort(key=lambda d: d["secs"])
    return out


def thin_lane(markers, seconds_max, width=DECISION_WIDTH, max_markers=MAX_MARKERS_PER_LANE):
    """Buckets ``markers`` (already time-sorted) into at most ``max_markers``
    time slices so a dense lane (transport_refusal ran to nearly 2000 in one
    real game) still draws as a light SVG. Each bucket keeps its first
    marker and a count of how many landed in it; the renderer folds that
    count into the hover title."""
    if seconds_max <= 0 or len(markers) <= max_markers:
        return [dict(m, count=1) for m in markers]
    buckets_n = max(1, min(width, max_markers))
    bucket_w = seconds_max / buckets_n
    buckets = {}
    order = []
    for m in markers:
        b = int(m["secs"] / bucket_w) if bucket_w > 0 else 0
        if b not in buckets:
            buckets[b] = dict(m, count=1)
            order.append(b)
        else:
            rep = buckets[b]
            rep["count"] += 1
            rep["refusal"] = rep["refusal"] or m["refusal"]
    return [buckets[b] for b in order]


def render_player_decision_band(player, areas, lanes, bands, seconds_max, width=DECISION_WIDTH):
    lane_h = 20
    height = lane_h * len(areas) + 4
    parts = []
    for s0, s1, phase in bands:
        x0 = (s0 / seconds_max) * width if seconds_max else 0
        x1 = (s1 / seconds_max) * width if seconds_max else 0
        parts.append('<rect x="%.1f" y="0" width="%.1f" height="%d" fill="%s" fill-opacity="0.16">'
                     '<title>%s: %s\u2013%s</title></rect>'
                     % (x0, max(x1 - x0, 0.5), height, phase_colour(phase), esc(phase),
                        mmss(s0).strip(), mmss(s1).strip()))
    for i, area in enumerate(areas):
        y = i * lane_h
        for m in lanes.get(area, []):
            x = (m["secs"] / seconds_max) * width if seconds_max else 0
            colour = REFUSAL_COLOUR if m["refusal"] else AREA_COLOUR.get(area, "#6b7280")
            title = "%s  %s%s%s%s" % (
                mmss(m["secs"]).strip(), esc(m["ev"]),
                ("  " + esc(m["subject"])) if m.get("subject") else "",
                (" (" + esc(m["why"]) + ")") if m.get("why") else "",
                ("  x%d" % m["count"]) if m.get("count", 1) > 1 else "")
            r = 2.6 if m.get("count", 1) <= 1 else 3.6
            parts.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s" opacity="0.85"><title>%s</title></circle>'
                        % (x, y + lane_h / 2, r, colour, title))

    labels = "".join('<div class="tl-label" style="top:%dpx">%s</div>' % (i * lane_h, esc(area))
                     for i, area in enumerate(areas))
    return ('<div class="dt-player"><h5>player %d</h5><div class="timeline">'
            '<div class="tl-labels" style="height:%dpx">%s</div>'
            '<svg viewBox="0 0 %d %d" preserveAspectRatio="none" class="tl-svg" style="height:%dpx">'
            '%s</svg></div></div>'
            % (player, height, labels, width, height, height, "".join(parts)))


def render_decision_timeline(log_events, players, seconds_max, width=DECISION_WIDTH):
    per_player_lanes = {}
    areas_present = set()
    for p in players:
        markers = collect_timeline_events(log_events, p)
        by_area = defaultdict(list)
        for m in markers:
            by_area[m["area"]].append(m)
        thinned = {a: thin_lane(v, seconds_max, width) for a, v in by_area.items()}
        per_player_lanes[p] = thinned
        areas_present.update(thinned.keys())

    areas = [a for a in AREA_ORDER if a in areas_present]
    if not areas:
        return ""

    bands = {p: phase_bands(log_events, p, seconds_max) for p in players}
    figs = [render_player_decision_band(p, areas, per_player_lanes[p], bands[p], seconds_max, width)
            for p in players]
    return "".join(figs)


def why_tag_counts(log_events, players):
    """``{player: [(ev, why, count, first_secs, last_secs), ...]}``, sorted
    by count descending -- what a player kept trying and failing (or
    succeeding) to do, and how often."""
    table = {p: {} for p in players}
    for e in log_events:
        why = e.get("why")
        if why is None:
            continue
        p = ev_player(e)
        if p not in table:
            continue
        s = ev_secs(e)
        if s is None:
            continue
        key = (e.get("ev"), why)
        d = table[p]
        if key not in d:
            d[key] = [0, s, s]
        row = d[key]
        row[0] += 1
        row[1] = min(row[1], s)
        row[2] = max(row[2], s)
    result = {}
    for p, d in table.items():
        rows = [(ev, why, c, first, last) for (ev, why), (c, first, last) in d.items()]
        rows.sort(key=lambda r: (-r[2], r[0], r[1]))
        result[p] = rows
    return result


# --------------------------------------------------------------------------
# Deaths, fights, moments.
# --------------------------------------------------------------------------

def category_lookup(unit_rows):
    lut = {}
    for r in unit_rows:
        try:
            p = int(r["player"])
        except (KeyError, TypeError, ValueError):
            continue
        lut[(p, r.get("unitType"))] = r.get("category")
    return lut


def death_marker_size(category, is_commander, cost):
    if is_commander:
        return 10.0
    if cost is not None and cost > 0:
        return max(3.0, min(12.0, 2.6 + (cost ** 0.5) * 0.35))
    return DEATH_SIZE_BY_CATEGORY.get(category, DEFAULT_DEATH_SIZE)


def unit_deaths(log_events, unit_rows, players):
    """Normalised ``unit_death`` events: category (from the events CSV, when
    it has a row for that player/type -- ``AiArenaReport.cpp``'s
    ``categorise()`` has no "extractor" bucket, extractors are "economy" like
    any other energy or metal building, so extractor detection below follows
    ``arena-deaths.py`` and looks at the type name instead), a commander
    flag, and an approximate size for the death map / fights value (real
    cost is essentially never logged -- see death_marker_size)."""
    lut = category_lookup(unit_rows)
    out = []
    for e in log_events:
        if e.get("ev") != "unit_death":
            continue
        p = ev_player(e)
        s = ev_secs(e)
        x, z = e.get("x"), e.get("z")
        if p is None or s is None or x is None or z is None:
            continue
        subject = e.get("subject") or "?"
        category = lut.get((p, subject))
        is_commander = category == "commander" or subject.endswith("COM")
        cost = e.get("cost")
        try:
            cost = float(cost) if cost is not None else None
        except (TypeError, ValueError):
            cost = None
        out.append({
            "player": p, "secs": s, "x": float(x), "z": float(z), "subject": subject,
            "cause": e.get("cause"), "killer_type": e.get("killer_type"),
            "killer_player": e.get("killer_player"), "category": category,
            "commander": is_commander, "cost": cost,
            "size": death_marker_size(category, is_commander, cost),
        })
    out.sort(key=lambda d: d["secs"])
    return out


def cluster_fights(deaths, players, space=FIGHT_SPACE_RADIUS, time_gap=FIGHT_TIME_GAP, min_deaths=FIGHT_MIN_DEATHS):
    """Greedily clusters ``deaths`` (time-sorted) into engagements: a death
    continues the nearest still-open cluster if it landed within ``space``
    world units of one of that cluster's existing deaths and within
    ``time_gap`` seconds of the cluster's most recent one; clusters that
    match more than one death are merged. A cluster with fewer than
    ``min_deaths`` deaths is not a fight -- most single deaths are a scout
    walking into a sentry, not an engagement."""
    space2 = space * space
    clusters = []
    for d in deaths:
        pos = (d["x"], d["z"])
        matches = [c for c in clusters
                  if (d["secs"] - c["last_secs"]) <= time_gap
                  and any((pos[0] - p[0]) ** 2 + (pos[1] - p[1]) ** 2 <= space2 for p in c["positions"])]
        if matches:
            target = matches[0]
            for extra in matches[1:]:
                target["deaths"].extend(extra["deaths"])
                target["positions"].extend(extra["positions"])
                target["last_secs"] = max(target["last_secs"], extra["last_secs"])
                clusters.remove(extra)
            target["deaths"].append(d)
            target["positions"].append(pos)
            target["last_secs"] = d["secs"]
        else:
            clusters.append({"deaths": [d], "positions": [pos], "last_secs": d["secs"]})

    fights = []
    for c in clusters:
        if len(c["deaths"]) < min_deaths:
            continue
        ds = c["deaths"]
        start = min(x["secs"] for x in ds)
        end = max(x["secs"] for x in ds)
        cx = sum(p[0] for p in c["positions"]) / len(c["positions"])
        cz = sum(p[1] for p in c["positions"]) / len(c["positions"])
        radius = max(((p[0] - cx) ** 2 + (p[1] - cz) ** 2) ** 0.5 for p in c["positions"])

        by_player = {p: {"deaths": 0, "value": 0.0, "types": set()} for p in players}
        for x in ds:
            row = by_player.setdefault(x["player"], {"deaths": 0, "value": 0.0, "types": set()})
            row["deaths"] += 1
            row["value"] += x["size"]
            row["types"].add(x["subject"])
        ranked = sorted(by_player.items(), key=lambda kv: kv[1]["value"])
        winner = None
        if len(ranked) >= 2 and ranked[0][1]["value"] < ranked[1][1]["value"]:
            winner = ranked[0][0]

        fights.append({
            "start": start, "end": end, "x": cx, "z": cz, "radius": radius,
            "deaths": len(ds), "by_player": by_player, "winner": winner,
        })
    fights.sort(key=lambda f: f["start"])
    return fights


def build_moments(log_events, deaths, fights, players, seconds_max, run_meta):
    """A short chronological list of what a reviewer should jump to. Every
    entry has ``secs`` and ``text``; "the end" is always last."""
    moments = []

    contacts = []
    for e in log_events:
        if e.get("ev") != "ai_status":
            continue
        try:
            known = int(e.get("known_enemies"))
        except (TypeError, ValueError):
            continue
        if known > 0:
            s, p = ev_secs(e), ev_player(e)
            if s is not None:
                contacts.append((s, p))
    if contacts:
        s, p = min(contacts, key=lambda t: t[0])
        moments.append({"secs": s, "text": "first contact" + (" (player %d sees an enemy)" % p if p is not None else "")})

    seen_extractor, seen_factory = set(), set()
    for d in deaths:
        subj = d["subject"] or ""
        if (subj.endswith("MEX") or subj.endswith("MOHO")) and d["player"] not in seen_extractor:
            seen_extractor.add(d["player"])
            moments.append({"secs": d["secs"], "text": "player %d loses its first extractor (%s)" % (d["player"], subj)})
        if d["category"] == "factory" and d["player"] not in seen_factory:
            seen_factory.add(d["player"])
            moments.append({"secs": d["secs"], "text": "player %d loses its first factory (%s)" % (d["player"], subj)})

    prev = {}
    statuses = sorted((e for e in log_events if e.get("ev") == "ai_status"),
                      key=lambda e: ev_secs(e) if ev_secs(e) is not None else 0.0)
    for e in statuses:
        p, s = ev_player(e), ev_secs(e)
        if p is None or s is None:
            continue
        pm, pe = prev.get(p, (False, False))
        m, en = bool(e.get("metal_stalled")), bool(e.get("energy_stalled"))
        if m and not pm:
            moments.append({"secs": s, "text": "player %d starts a metal stall" % p})
        if en and not pe:
            moments.append({"secs": s, "text": "player %d starts an energy stall" % p})
        prev[p] = (m, en)

    by_player_idle = defaultdict(list)
    for e in log_events:
        if e.get("ev") != "ai_status" or e.get("idle_builders") is None:
            continue
        p, s = ev_player(e), ev_secs(e)
        if p is None or s is None:
            continue
        try:
            ib = float(e["idle_builders"])
        except (TypeError, ValueError):
            continue
        by_player_idle[p].append((s, ib))
    for p, seq in by_player_idle.items():
        seq.sort(key=lambda t: t[0])
        for i in range(1, len(seq) - 1):
            s, v = seq[i]
            if v >= IDLE_BUILDER_SPIKE_MIN and v > seq[i - 1][1] and v >= seq[i + 1][1]:
                moments.append({"secs": s, "text": "player %d has %d idle builders" % (p, int(v))})

    for e in log_events:
        if e.get("ev") == "army_commander_danger":
            s, p = ev_secs(e), ev_player(e)
            if s is not None:
                moments.append({"secs": s, "text": "player %s's commander is in danger" % (p if p is not None else "?")})

    for f in sorted(fights, key=lambda f: -f["deaths"])[:3]:
        moments.append({"secs": f["start"], "text": "big fight near (%d,%d): %d units die" % (int(f["x"]), int(f["z"]), f["deaths"])})

    end_text = "the game ends"
    if run_meta:
        ended = run_meta.get("ended")
        winner = run_meta.get("winner")
        if ended:
            end_text += " (%s)" % ended
        if winner is not None:
            end_text += ", winner player %s" % winner
    moments.append({"secs": seconds_max, "text": end_text, "_end": True})

    moments.sort(key=lambda m: (m["secs"], m.get("_end", False)))
    return moments


def render_death_map(deaths, fights, seconds_max):
    if not deaths:
        return ""
    xs = [d["x"] for d in deaths]
    zs = [d["z"] for d in deaths]
    pad = 60.0
    xmin, xmax = min(xs) - pad, max(xs) + pad
    zmin, zmax = min(zs) - pad, max(zs) + pad
    if xmax - xmin < 1:
        xmax = xmin + 1
    if zmax - zmin < 1:
        zmax = zmin + 1
    width, height = 640.0, 440.0

    def px(x):
        return (x - xmin) / (xmax - xmin) * width

    def pz(z):
        return (z - zmin) / (zmax - zmin) * height

    max_minute = int(max(d["secs"] for d in deaths) // 60) + 1

    circles = []
    for f in fights:
        cx, cz = px(f["x"]), pz(f["z"])
        r = max(16.0, (f["radius"] / (xmax - xmin)) * width + 10.0)
        circles.append('<circle cx="%.1f" cy="%.1f" r="%.1f" class="fight-ring"><title>fight %s\u2013%s, %d deaths</title></circle>'
                       % (cx, cz, r, mmss(f["start"]).strip(), mmss(f["end"]).strip(), f["deaths"]))

    dots = []
    for d in deaths:
        cx, cz = px(d["x"]), pz(d["z"])
        minute = d["secs"] / 60.0
        colour = player_colour(d["player"])
        cls = "death-dot commander" if d["commander"] else "death-dot"
        title = "%s  p%d %s  killed by %s%s" % (
            mmss(d["secs"]).strip(), d["player"], esc(d["subject"]), esc(d["killer_type"] or "unknown"),
            (" (" + esc(d["cause"]) + ")") if d["cause"] else "")
        dots.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s" class="%s" data-min="%.4f"><title>%s</title></circle>'
                    % (cx, cz, d["size"], colour, cls, minute, title))

    slider = ('<div class="death-slider"><label for="deathSlider">show deaths up to '
              '<span id="deathSliderLabel">minute %d</span></label>'
              '<input type="range" id="deathSlider" min="0" max="%d" value="%d" '
              'oninput="rweFilterDeaths(this.value)"></div>'
              % (max_minute, max_minute, max_minute))

    return ('<figure class="death-map">%s<svg viewBox="0 0 %.0f %.0f" preserveAspectRatio="xMidYMid meet" '
            'role="img" aria-label="Death map">%s%s</svg></figure>'
            % (slider, width, height, "".join(circles), "".join(dots)))


def render_fights_table(fights):
    if not fights:
        return ""
    rows = []
    for f in fights:
        player_bits = []
        for p in sorted(f["by_player"]):
            v = f["by_player"][p]
            types = ", ".join(sorted(v["types"])) if v["types"] else "-"
            player_bits.append("p%d: %d lost (value \u2248%.0f, %s)" % (p, v["deaths"], v["value"], types))
        winner_txt = ("player %d" % f["winner"]) if f["winner"] is not None else "even"
        rows.append('<tr><td>%s\u2013%s</td><td>%d, %d</td><td>%s</td><td>%s</td></tr>'
                    % (mmss(f["start"]).strip(), mmss(f["end"]).strip(), int(f["x"]), int(f["z"]),
                       " / ".join(esc(b) for b in player_bits), esc(winner_txt)))
    return ('<table class="rt"><thead><tr><th>time</th><th>location</th><th>lost</th><th>lost less</th></tr></thead>'
            '<tbody>%s</tbody></table>' % "".join(rows))


def render_why_tags_section(why_table, players):
    parts = []
    for p in players:
        rows = why_table.get(p, [])[:20]
        if not rows:
            continue
        trs = "".join(
            '<tr><td>%s</td><td>%s</td><td>%d</td><td>%s</td><td>%s</td></tr>'
            % (esc(ev), esc(why), c, mmss(first).strip(), mmss(last).strip())
            for ev, why, c, first, last in rows)
        parts.append('<div class="why-table"><h5>player %d</h5><table class="rt">'
                     '<thead><tr><th>event</th><th>why</th><th>count</th><th>first</th><th>last</th></tr></thead>'
                     '<tbody>%s</tbody></table></div>' % (p, trs))
    if not parts:
        return ""
    return ('<details class="review-section" open><summary>Why-tags</summary>'
           '<p class="note">The most frequent (event, why) pairs per player -- what it kept trying and, '
           'often, failing to do.</p><div class="why-tables">%s</div></details>' % "".join(parts))


def render_moments_list(moments):
    if not moments:
        return ""
    items = "".join('<li><span class="t">%s</span> %s</li>' % (mmss(m["secs"]).strip(), esc(m["text"]))
                    for m in moments)
    return '<ol class="moments">%s</ol>' % items


def render_review_html(review):
    if review is None:
        return ""
    blocks = []

    dt = render_decision_timeline(review["log_events"], review["players"], review["seconds_max"])
    if dt:
        blocks.append('<details class="review-section" open><summary>Decision timeline</summary>'
                      '<p class="note">One band per player, lanes by area. Red markers are refusals or '
                      'failures (<code>*_refusal</code>, <code>*_unaffordable</code>, <code>*_not_worth</code>, '
                      '<code>build_site_*</code>). Background shading is the AI phase. A dense lane is bucketed '
                      'by time -- hover a marker for the count, event, subject and why.</p>%s</details>' % dt)

    blocks.append(render_why_tags_section(review["why_table"], review["players"]))

    dm = render_death_map(review["deaths"], review["fights"], review["seconds_max"])
    if dm:
        blocks.append('<details class="review-section" open><summary>Death map</summary>'
                      '<p class="note">Every death, coloured by owning player; commander deaths ringed gold. '
                      'Marker size follows unit cost where the log carries one, else a rough size by category '
                      '(cost is almost never logged for a death -- see the fights table). Shaded rings mark '
                      'fights. Drag the slider to watch the map build up minute by minute.</p>%s</details>' % dm)

    ft = render_fights_table(review["fights"])
    if ft:
        blocks.append('<details class="review-section" open><summary>Fights</summary>'
                      '<p class="note">Deaths within %d world units of an open cluster continue it; a gap of '
                      'more than %ds closes it. Clusters under %d deaths are not shown as fights. "Value" is an '
                      'approximation from the same size heuristic as the death map, real unit cost being rarely '
                      'logged.</p>%s</details>'
                      % (int(FIGHT_SPACE_RADIUS), int(FIGHT_TIME_GAP), FIGHT_MIN_DEATHS, ft))

    mo = render_moments_list(review["moments"])
    if mo:
        blocks.append('<details class="review-section" open><summary>Moments</summary>%s</details>' % mo)

    blocks = [b for b in blocks if b]
    if not blocks:
        return ""
    return '<section class="ai-review"><h2>AI review</h2>%s</section>' % "".join(blocks)


def find_event_log(csv_path):
    p = os.path.join(os.path.dirname(csv_path) or ".", EVENT_LOG_NAME)
    return p if os.path.exists(p) else None


def find_run_meta(csv_path):
    p = os.path.join(os.path.dirname(csv_path) or ".", RUN_JSON_NAME)
    if not os.path.exists(p):
        return None
    try:
        with open(p, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def load_review_data(csv_path, players, seconds_max, unit_rows):
    """Everything the six new sections need, or ``None`` when there is no
    ``event-log.jsonl`` beside ``csv_path`` -- in which case the report is
    exactly what it was before this file existed."""
    log_path = find_event_log(csv_path)
    if not log_path:
        return None
    log_events = read_event_log(log_path)
    run_meta = find_run_meta(csv_path)
    why_table = why_tag_counts(log_events, players)
    deaths = unit_deaths(log_events, unit_rows, players)
    fights = cluster_fights(deaths, players)
    moments = build_moments(log_events, deaths, fights, players, seconds_max, run_meta)
    return {
        "log_events": log_events, "players": players, "seconds_max": seconds_max,
        "run_meta": run_meta, "why_table": why_table, "deaths": deaths,
        "fights": fights, "moments": moments,
    }


def build(csv_path, out_path):
    rows = read_rows(csv_path)
    events_path = os.path.join(os.path.dirname(csv_path),
                               os.path.splitext(os.path.basename(csv_path))[0] + "-events.csv")
    unit_rows = read_rows(events_path) if os.path.exists(events_path) else []

    if not rows:
        raise SystemExit("no rows in " + csv_path)

    seconds_max = max(num(r["seconds"]) for r in rows) or 1
    players = sorted({int(r["player"]) for r in rows})

    sections = []
    summary_cards = []
    for p in players:
        prows = [r for r in rows if int(r["player"]) == p]
        pev = [e for e in unit_rows if int(e["player"]) == p]
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

    review = load_review_data(csv_path, players, seconds_max, unit_rows)
    review_html = render_review_html(review)

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
            .replace("@@REVIEW@@", review_html)
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
h5 { font-size: 11px; text-transform: uppercase; letter-spacing: .06em; color: var(--ink-soft); margin: 16px 0 4px; font-weight: 600; }
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
section.ai-review { display: flex; flex-direction: column; gap: 12px; }
.review-section { background: var(--panel); border: 1px solid var(--rule); border-radius: 3px; padding: 14px 18px 18px; }
.review-section summary {
  cursor: pointer; font-family: Oswald, sans-serif; font-weight: 600; font-size: 15px;
  letter-spacing: .03em; text-transform: uppercase;
}
.review-section .note { color: var(--ink-soft); font-size: 12px; margin: 8px 0 14px; max-width: 80ch; }
.dt-player .tl-label { height: 20px; line-height: 20px; }
table.rt { width: 100%; border-collapse: collapse; font-size: 12px; margin-bottom: 12px; }
table.rt th, table.rt td { padding: 3px 8px; border-bottom: 1px solid var(--rule); text-align: left; }
table.rt th { color: var(--ink-soft); font-weight: 600; text-transform: uppercase; font-size: 10px; letter-spacing: .04em; }
.why-tables { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 0 20px; }
.death-map svg { width: 100%; height: auto; max-width: 640px; display: block; background: transparent; border: 1px solid var(--rule); border-radius: 3px; }
.death-map figcaption, .death-slider { font-size: 12px; color: var(--ink-soft); }
.death-slider { display: flex; align-items: center; gap: 8px; margin-bottom: 6px; }
.death-slider input[type=range] { flex: 1; max-width: 420px; }
.fight-ring { fill: var(--warn); fill-opacity: 0.10; stroke: var(--warn); stroke-opacity: 0.5; stroke-width: 1; }
.death-dot.commander { stroke: #c9b458; stroke-width: 2; }
ol.moments { list-style: none; margin: 0; padding: 0; }
ol.moments li { padding: 4px 0; border-bottom: 1px dashed var(--rule); font-size: 13px; }
ol.moments .t { font-family: "IBM Plex Mono", monospace; color: var(--ink-soft); margin-right: 10px; font-variant-numeric: tabular-nums; }
</style>
<div class="wrap">
  <header>
    <h1>@@TITLE@@</h1>
    <p class="sub">@@MINUTES@@ minutes, computer against computer. The solid bar is how long a
      thing took to build; the thin tail is how long it then lived; a red cap is where it died.</p>
  </header>
  <div class="cards">@@CARDS@@</div>
  @@SECTIONS@@
  @@REVIEW@@
  <footer>Generated by <code>tools/arena-report.py</code> from <code>@@SOURCE@@</code>.
    Vertical guides are one minute apart.</footer>
</div>
<script>
function rweFilterDeaths(v) {
  var mv = parseFloat(v);
  document.querySelectorAll('.death-dot').forEach(function (el) {
    var m = parseFloat(el.getAttribute('data-min'));
    el.style.display = (isNaN(m) || m <= mv) ? '' : 'none';
  });
  var label = document.getElementById('deathSliderLabel');
  if (label) { label.textContent = 'minute ' + Math.round(mv); }
}
</script>
"""


def text_report(csv_path):
    """Sections 2 (why-tags), 4 (fights) and 5 (moments) as aligned plain
    text, for ``--text``. Returns a note rather than raising when there is
    nothing to show."""
    rows = read_rows(csv_path)
    if not rows:
        return "no rows in " + csv_path
    events_path = os.path.join(os.path.dirname(csv_path),
                               os.path.splitext(os.path.basename(csv_path))[0] + "-events.csv")
    unit_rows = read_rows(events_path) if os.path.exists(events_path) else []
    seconds_max = max(num(r["seconds"]) for r in rows) or 1
    players = sorted({int(r["player"]) for r in rows})

    review = load_review_data(csv_path, players, seconds_max, unit_rows)
    if review is None:
        return "no %s found beside %s; nothing to show" % (EVENT_LOG_NAME, csv_path)

    lines = ["=== why-tags ==="]
    for p in players:
        lines.append("player %d" % p)
        rows2 = review["why_table"].get(p, [])[:20]
        if not rows2:
            lines.append("  (none)")
        for ev, why, c, first, last in rows2:
            lines.append("  %-28s %-20s x%-5d %s - %s" % (ev, why, c, mmss(first).strip(), mmss(last).strip()))

    lines.append("")
    lines.append("=== fights ===")
    if not review["fights"]:
        lines.append("  (none)")
    for f in review["fights"]:
        player_bits = []
        for p in sorted(f["by_player"]):
            v = f["by_player"][p]
            types = ",".join(sorted(v["types"])) if v["types"] else "-"
            player_bits.append("p%d %d lost (value~%.0f: %s)" % (p, v["deaths"], v["value"], types))
        winner_txt = ("player %d" % f["winner"]) if f["winner"] is not None else "even"
        lines.append("  %s-%s @ (%d,%d)  %s  lost less: %s" % (
            mmss(f["start"]).strip(), mmss(f["end"]).strip(), int(f["x"]), int(f["z"]),
            "  ".join(player_bits), winner_txt))

    lines.append("")
    lines.append("=== moments ===")
    for m in review["moments"]:
        lines.append("  %s  %s" % (mmss(m["secs"]).strip(), m["text"]))

    return "\n".join(lines)


def main():
    argv = sys.argv[1:]
    out = None
    run_dir = None
    text_flag = False
    positional = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--out" and i + 1 < len(argv):
            out = argv[i + 1]
            i += 2
            continue
        if a == "--run" and i + 1 < len(argv):
            run_dir = argv[i + 1]
            i += 2
            continue
        if a == "--text":
            text_flag = True
            i += 1
            continue
        if not a.startswith("--"):
            positional.append(a)
        i += 1

    if run_dir:
        csv_path = os.path.join(run_dir, "ai-arena.csv")
    else:
        csv_path = positional[0] if positional else default_csv()
    if not os.path.exists(csv_path):
        raise SystemExit("no such file: " + csv_path)

    out_explicit = out is not None
    if text_flag:
        print(text_report(csv_path))
    if (not text_flag) or out_explicit:
        if out is None:
            out = os.path.join(os.path.dirname(csv_path) or ".", "ai-arena.html")
        print(build(csv_path, out))


if __name__ == "__main__":
    main()
