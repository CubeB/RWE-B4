#!/usr/bin/env bash
#
# The bash counterpart to ai-arena.ps1: computer-versus-computer games reduced
# to a table, for judging an AI change by playing it.
#
# WHY THIS EXISTS. ai-arena.ps1 is PowerShell and some of the machines this
# gets worked on are Linux without pwsh. The arena itself is an engine flag
# (--ai-arena) and not something the script owns, so the games are the same
# games; only the runner differs. Written for the all-water fix measured on
# 2026-09-18, whose numbers are in docs/ROADMAP.md, Phase 2.
#
# WHAT IT REPRODUCES of the PowerShell one, which is the part that matters and
# the part its own header says took a day to learn:
#
#   -tune runs its own control. Every seed is played twice, once with the
#   change and once with no tune anywhere, so the table shows the change
#   against its absence rather than one player against another.
#
#   It alternates which player gets the change. Odd seeds give it to player 0
#   and even seeds to player 1, so a seat that happens to be stronger is shared
#   equally between the arms instead of being confounded with the change.
#
#   It reports arms, not players -- "tuned" and "untuned" are the rows that
#   matter -- and prints the control arm per player underneath, because that is
#   where a seat imbalance would still show up.
#
#   Seats are dealt (--start-location random) by default, for the reason given
#   at length over there: fixed start positions compare one seat with another.
#
# WHAT IT DOES NOT. No -confirm second long run (run it again with -s 1800).
# No -tuneA/-tuneB legacy pinning. No -countTypes columns. No replay
# recording, so nothing here can be watched afterwards; add --record-replay to
# run_game if you want that. It runs the games one at a time: six at once was
# tried and the machine ran out of memory.
#
# READ THE ARMS, NOT JUST THE MEANS. Every figure but secs and ended is a
# snapshot taken at game end, so an arm whose games finished early is being
# measured at a different moment from one that ran to the cap. The `ended`
# column is printed per game for exactly that reason.
#
# THE RUN BEHIND THE ROADMAP TABLE (78.8 lost to 5.5, and the six games in
# twenty that lost 230+ going to none) was, from the repository root, with
# build/rwe built Debug:
#
#   tools/ai-arena.sh -m "Brain Coral" -f 1 -l 10 -s 900 \
#       -A ARM -B ARM -d standard \
#       -t noticeProductionHarassment=false
#
# Note the direction: that fix is ON by default, so the TUNED arm is the one
# with it switched OFF and is expected to come out worse. The control arm, no
# tune anywhere, is the fixed AI.

set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(dirname "$here")"

exe="$repo/build/rwe"
out="${TMPDIR:-/tmp}/rwe-arena"
map="Coast To Coast"
seconds=900
first=1
last=10
difficulty=standard
side_a=ARM
side_b=ARM
start_location=random
tune=""
keep=0

usage() {
    cat <<'EOF'
usage: ai-arena.sh [options]

  -t <knob=value[,knob=value...]>  the change under test; alternates seats and
                                   self-controls. Required to learn anything.
  -m <map>            map name            (default "Coast To Coast")
  -f <seed>           first seed          (default 1)
  -l <seed>           last seed           (default 10)
  -s <seconds>        game length cap     (default 900)
  -d <difficulty>     easy|standard|hard|brutal (default standard)
  -A <side>           player 0 side       (default ARM)
  -B <side>           player 1 side       (default ARM)
  -S <fixed|random>   start locations     (default random)
  -e <path>           engine binary       (default <repo>/build/rwe)
  -o <dir>            log directory       (default $TMPDIR/rwe-arena)
  -k                  keep existing logs instead of clearing the directory
  -h                  this

Set -A and -B to the same side when using -t: different factions would put the
faction difference in the same column as the change.
EOF
}

while getopts "t:m:f:l:s:d:A:B:S:e:o:kh" opt; do
    case "$opt" in
        t) tune="$OPTARG" ;;
        m) map="$OPTARG" ;;
        f) first="$OPTARG" ;;
        l) last="$OPTARG" ;;
        s) seconds="$OPTARG" ;;
        d) difficulty="$OPTARG" ;;
        A) side_a="$OPTARG" ;;
        B) side_b="$OPTARG" ;;
        S) start_location="$OPTARG" ;;
        e) exe="$OPTARG" ;;
        o) out="$OPTARG" ;;
        k) keep=1 ;;
        h) usage; exit 0 ;;
        *) usage; exit 2 ;;
    esac
done

if [ ! -x "$exe" ]; then
    echo "no such executable: $exe" >&2
    echo "build it first, or point -e at one" >&2
    exit 1
fi
if [ -z "$tune" ]; then
    echo "warning: no -t, so both arms are identical and the table means nothing" >&2
fi
if [ "$side_a" != "$side_b" ] && [ -n "$tune" ]; then
    echo "warning: -A and -B differ, so the faction difference is measured too" >&2
fi

mkdir -p "$out"
if [ "$keep" -eq 0 ]; then
    rm -f "$out"/game-*.log
fi

# One game. `tuned` is the player the tune goes to, or -1 for a control game.
run_game() {
    local seed=$1 arm=$2 tuned=$3
    local log="$out/game-$seed-$arm.log"
    local args=(--log "$log" --ai-arena "$seconds" --seed "$seed" --map "$map"
        --player "A;Computer;$side_a;0" --player "B;Computer;$side_b;1"
        --ai-difficulty "$difficulty" --start-location "$start_location")
    if [ "$tuned" -ge 0 ]; then
        local IFS=','
        local knob
        for knob in $tune; do
            [ -n "$knob" ] && args+=(--ai-tune "$tuned:$knob")
        done
    fi
    "$exe" "${args[@]}" > /dev/null 2>&1
    if ! grep -q AI-ARENA-RESULT "$log" 2>/dev/null; then
        echo "  seed $seed $arm: no result line (see $log)" >&2
    fi
}

echo "map '$map', seeds $first..$last, ${seconds}s, $difficulty, $side_a vs $side_b, seats $start_location"
echo "tune: ${tune:-<none>}"
echo "logs: $out"
for seed in $(seq "$first" "$last"); do
    # Odd seeds give the change to player 0, even seeds to player 1.
    tuned=$(( seed % 2 == 1 ? 0 : 1 ))
    echo "  seed $seed ..."
    run_game "$seed" tuned "$tuned"
    run_game "$seed" control -1
done

# Reduce. The last AI-ARENA-RESULT line of each log is the game's end state.
{
    for f in "$out"/game-*.log; do
        [ -e "$f" ] || continue
        base="${f##*/}"; base="${base%.log}"; base="${base#game-}"
        seed="${base%%-*}"; arm="${base#*-}"
        line="$(grep AI-ARENA-RESULT "$f" | tail -1)"
        [ -n "$line" ] && echo "$seed $arm $line"
    done
} | awk '
function val(s, key,   m, t) {
    # The token after "key=" in s.
    if (!match(s, key "=[^ ]+")) return "";
    t = substr(s, RSTART + length(key) + 1, RLENGTH - length(key) - 1);
    return t;
}
{
    seed = $1; arm = $2;
    line = $0;
    n = split(line, seg, / \| /);
    # seg[1] holds the header and the leading "seed arm", seg[2] and seg[3]
    # the two players, seg[4] the ending.
    secs = val(seg[1], "seconds");
    ended = val(seg[4], "ended");
    for (p = 0; p <= 1; p++) {
        s = seg[p + 2];
        split(s, w, " ");
        status[p] = w[2];
        u[p] = val(s, "units") + 0;
        b[p] = val(s, "buildings") + 0;
        a[p] = val(s, "army") + 0;
        lo[p] = val(s, "lost") + 0;
        mi[p] = val(s, "metalIncome") + 0;
    }
    if (!header++) printf "\n%4s %-8s %5s %-9s  %s\n", "seed", "arm", "secs", "ended", "units/bldgs/lost/metal per player";
    printf "%4d %-8s %5s %-9s  p0 %s %3d/%3d/%4d/%3d   p1 %s %3d/%3d/%4d/%3d\n",
        seed, arm, secs, ended,
        status[0], u[0], b[0], lo[0], mi[0],
        status[1], u[1], b[1], lo[1], mi[1];

    # Arms. In a tuned game the tuned seat is 0 on odd seeds and 1 on even.
    if (arm == "tuned") {
        t = (seed % 2 == 1) ? 0 : 1;
        add("tuned", u[t], b[t], lo[t], mi[t], status[t]);
        o = 1 - t;
        add("untuned", u[o], b[o], lo[o], mi[o], status[o]);
    } else {
        add("control", u[0], b[0], lo[0], mi[0], status[0]);
        add("control", u[1], b[1], lo[1], mi[1], status[1]);
        add("control p0", u[0], b[0], lo[0], mi[0], status[0]);
        add("control p1", u[1], b[1], lo[1], mi[1], status[1]);
    }
}
function add(k, uu, bb, ll, mm, st) {
    cnt[k]++; su[k] += uu; sb[k] += bb; sl[k] += ll; sm[k] += mm;
    if (st != "alive") sw[k]++;
}
END {
    printf "\n%-14s %4s %7s %7s %7s %7s %6s\n", "arm", "n", "units", "bldgs", "lost", "metal", "wiped";
    split("tuned untuned control", want, " ");
    for (i = 1; i <= 3; i++) row(want[i]);
    printf "\nthe control arm per player, where a seat imbalance would show:\n";
    row("control p0");
    row("control p1");
}
function row(k) {
    if (!(k in cnt)) return;
    printf "%-14s %4d %7.1f %7.1f %7.1f %7.1f %6d\n",
        k, cnt[k], su[k]/cnt[k], sb[k]/cnt[k], sl[k]/cnt[k], sm[k]/cnt[k], sw[k] + 0;
}
'
