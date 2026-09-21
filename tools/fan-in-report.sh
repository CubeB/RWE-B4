#!/bin/sh
# What a header change costs, and where the code is piling up.
#
# Two numbers decide how expensive this tree is to work in, and neither is
# visible from any one file. The first is fan-in: how many translation units
# include a header, which is how many recompile every time it is touched. The
# second is churn: where the commits actually land, which is where that cost is
# paid over and over. A header with high fan-in AND high churn is the one to
# narrow first -- it is the product of the two that hurts.
#
# This reports; it never fails. It is for deciding what to do next, not for
# gating a build. tools/section-budget.sh is the gate.
#
# Usage: tools/fan-in-report.sh [days]

set -eu

DAYS="${1:-30}"
TOP=15

# How many files include this header. Keyed on the path relative to src/,
# which is how the angle-bracket form is written -- keying on the basename
# instead credits each of the three files named util.h with all of the
# others' includers, which read 156 apiece before this was fixed. The quote
# form is counted only from the header's own directory, the only place it
# resolves.
includers() {
    h=$1
    rel=${h#src/}
    dir=$(dirname "$h")
    base=$(basename "$h")
    {
        grep -rlE "#include[[:space:]]+<$rel>" src 2>/dev/null || true
        grep -rlE "#include[[:space:]]+\"$base\"" "$dir" 2>/dev/null || true
    } | sort -u | grep -v "^$h$" | grep -c . || true
}

echo "=============================================================="
echo " Header fan-in: how many files recompile when this is touched"
echo "=============================================================="
echo

find src -name '*.h' | while read -r h; do
    printf '%5d  %s\n' "$(includers "$h")" "$h"
done | sort -rn | head -$TOP

echo
echo "=============================================================="
echo " Largest translation units and headers"
echo "=============================================================="
echo

find src \( -name '*.cpp' -o -name '*.h' \) -print0 \
    | xargs -0 wc -l 2>/dev/null \
    | grep -v ' total$' \
    | sort -rn \
    | head -$TOP \
    | awk '{printf "%5d  %s\n", $1, $2}'

echo
echo "=============================================================="
echo " Churn: commits and added lines over the last $DAYS days"
echo "=============================================================="
echo
printf '%5s %7s  %s\n' "cmts" "+lines" "file"

git log --since="$DAYS days ago" --numstat --format= -- src 2>/dev/null \
    | awk '$1 ~ /^[0-9]+$/ { added[$3] += $1; commits[$3] += 1 }
           END { for (f in added) printf "%5d %7d  %s\n", commits[f], added[f], f }' \
    | sort -k1 -rn \
    | head -$TOP

echo
echo "=============================================================="
echo " Churn x fan-in: the headers worth narrowing"
echo "=============================================================="
echo
printf '%7s %5s %5s  %s\n' "product" "cmts" "fanin" "header"

git log --since="$DAYS days ago" --numstat --format= -- src 2>/dev/null \
    | awk '$1 ~ /^[0-9]+$/ && $3 ~ /\.h$/ { commits[$3] += 1 }
           END { for (f in commits) print commits[f], f }' \
    | sort -rn | head -25 | while read -r c h; do
        [ -f "$h" ] || continue
        n=$(includers "$h")
        printf '%7d %5d %5d  %s\n' "$((c * n))" "$c" "$n" "$h"
    done | sort -rn | head -$TOP
