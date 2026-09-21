#!/bin/sh
# Proves a refactor changed nothing the simulation can observe.
#
# The simulation is lockstep, so every tick has a sync hash and two builds that
# agree on every hash are the same simulation. RWE_HASH_LOG writes one line a
# tick. This runs a fixed set of headless arena games, keeps their hash logs,
# and compares a later build's against them.
#
# That is a far stronger check than the test suite for a change that is meant
# to move code without changing behaviour: an extraction can keep 800 tests
# green and still reorder a random draw or a float addition, and the hash
# catches both on the tick they happen.
#
# Usage:
#   tools/hash-oracle.sh record   [dir]   # before the change
#   tools/hash-oracle.sh compare  [dir]   # after it, having rebuilt
#
# The default dir is hash-oracle under the system temp directory. Override the
# engine with RWE_EXE, the data with RWE_DATA, and the length in simulated
# seconds with RWE_SECONDS.

set -eu

MODE="${1:?usage: tools/hash-oracle.sh record|compare [dir]}"
DIR="${2:-${TMPDIR:-/tmp}/hash-oracle}"

EXE="${RWE_EXE:-./build/rwe.exe}"
DATA="${RWE_DATA:-D:\\RWE-Data}"
SECONDS_OF_GAME="${RWE_SECONDS:-600}"

# Two maps and two seeds, listed at each loop below. One map is land-locked
# and one has water, because a water map is the only place the wake particles,
# the sea transports and the shipyard code run at all; two seeds because the
# AI's opening is drawn.

[ -x "$EXE" ] || { echo "hash-oracle: no engine at $EXE -- build it, or set RWE_EXE" >&2; exit 2; }

# getLocalDataPath reads APPDATA and the engine exits 1 without it, which in a
# bare shell looks like the run failing for no reason.
if [ -z "${APPDATA:-}" ]; then
    echo "hash-oracle: APPDATA is unset; the engine needs it to find its data folder" >&2
    exit 2
fi

mkdir -p "$DIR"

run_one() {
    map=$1
    seed=$2
    tag=$(printf '%s' "$map" | tr -d ' ')-$seed
    out="$3"

    # The engine is a Windows binary: give it a Windows path, not an MSYS one.
    case "$DIR" in
        /*) win_dir=$(cygpath -w "$DIR" 2>/dev/null || printf '%s' "$DIR") ;;
        *) win_dir=$DIR ;;
    esac

    RWE_HASH_LOG="$win_dir\\$out-$tag.txt" \
        "$EXE" --ai-arena "$SECONDS_OF_GAME" --seed "$seed" --map "$map" \
            --player "A;Computer;ARM;0" --player "B;Computer;CORE;1" \
            --data-path "$DATA" > /dev/null 2>&1 || {
        echo "  $tag: engine exited non-zero" >&2
        return 1
    }
    printf '%s' "$tag"
}

case "$MODE" in
    record)
        echo "Recording $SECONDS_OF_GAME simulated seconds per scenario into $DIR"
        for s in "Crystal Maze:42" "Crystal Maze:7" "Coast To Coast:42" "Coast To Coast:7"; do
            map=${s%:*}
            seed=${s##*:}
            tag=$(run_one "$map" "$seed" base)
            f="$DIR/base-$tag.txt"
            printf '  %-22s %s ticks\n' "$tag" "$(grep -c . "$f" 2>/dev/null || echo 0)"
        done
        echo "Baseline recorded. Rebuild, then: tools/hash-oracle.sh compare $DIR"
        ;;
    compare)
        fail=0
        echo "Comparing against the baseline in $DIR"
        for s in "Crystal Maze:42" "Crystal Maze:7" "Coast To Coast:42" "Coast To Coast:7"; do
            map=${s%:*}
            seed=${s##*:}
            tag=$(run_one "$map" "$seed" now)
            base="$DIR/base-$tag.txt"
            now="$DIR/now-$tag.txt"
            if [ ! -f "$base" ]; then
                echo "  $tag: no baseline -- run record first" >&2
                fail=1
            elif cmp -s "$base" "$now"; then
                printf '  %-22s identical, %s ticks\n' "$tag" "$(grep -c . "$now")"
            else
                tick=$(diff "$base" "$now" | sed -n '2p' | awk '{print $2}')
                printf '  %-22s DIFFERS, first at tick %s\n' "$tag" "${tick:-?}"
                fail=1
            fi
        done
        if [ "$fail" -ne 0 ]; then
            echo
            echo "The simulation changed. If that was not the point of the change, the" >&2
            echo "tick above is where to look: RWE_STATE_DUMP around it says which field." >&2
            exit 1
        fi
        echo "Nothing the simulation can observe has changed."
        ;;
    *)
        echo "hash-oracle: unknown mode $MODE (want record or compare)" >&2
        exit 2
        ;;
esac
