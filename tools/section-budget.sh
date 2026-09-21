#!/bin/sh
# Fails if any object file has grown close to the COFF section ceiling.
#
# An object numbers its sections with a signed 16-bit index, so 32767 is the
# hard limit. Past it the assembler switches the object to `pe-bigobj` without
# saying so, and a toolchain that cannot read that format back -- the MinGW64
# CI runner's did not -- drops every COMDAT definition while keeping the
# references. The failure surfaces as a link error in Debug only, all of it
# from one .obj, every missing symbol a template or a lambda. CLAUDE.md,
# "A translation unit can outgrow what a COFF object can describe", has the
# whole story.
#
# This script exists so that ceiling stops depending on someone remembering to
# run objdump. Run it after a Debug build; Release folds the instantiations
# away and never comes close.
#
# Usage: tools/section-budget.sh [build-dir] [budget-percent]

set -eu

BUILD_DIR="${1:-build}"
BUDGET_PCT="${2:-75}"

CEILING=32767
BUDGET=$((CEILING * BUDGET_PCT / 100))

if [ ! -d "$BUILD_DIR" ]; then
    echo "section-budget: no such build directory: $BUILD_DIR" >&2
    exit 2
fi

# Only what CMake itself produced. A build tree collects stray objects --
# hand-compiled experiments from a measurement session are how this was found,
# and one of them read 140% of the ceiling years after it stopped mattering.
# CMake puts every object it owns under a `<target>.dir/` directory, for both
# the Makefile and the Visual Studio generators.
OBJECTS=$(find "$BUILD_DIR" \( -name '*.obj' -o -name '*.o' \) 2>/dev/null     | grep -E '\.dir/' | sort)

if [ -z "$OBJECTS" ]; then
    echo "section-budget: no CMake objects under $BUILD_DIR -- has it been built?" >&2
    exit 2
fi

over=0
checked=0
# Report the worst offenders whatever the outcome: the trend is the point,
# not just the pass or fail.
report=$(
    for obj in $OBJECTS; do
        count=$(objdump -h "$obj" 2>/dev/null | grep -cE '^ *[0-9]+ ' || true)
        [ -z "$count" ] && continue
        [ "$count" -eq 0 ] && continue
        printf '%d\t%s\n' "$count" "$obj"
    done | sort -rn
)

checked=$(printf '%s\n' "$report" | grep -c . || true)

echo "Section budget: $BUDGET of $CEILING ($BUDGET_PCT%), $checked objects in $BUILD_DIR"
echo

printf '%s\n' "$report" | head -10 | while IFS="$(printf '\t')" read -r count obj; do
    pct=$((count * 100 / CEILING))
    printf '  %6d  %3d%%  %s\n' "$count" "$pct" "${obj#"$BUILD_DIR"/}"
done

echo

over=$(printf '%s\n' "$report" | awk -v b="$BUDGET" -F'\t' '$1 > b' | grep -c . || true)

if [ "$over" -gt 0 ]; then
    echo "FAIL: $over object(s) over budget. Split the dense ones -- only code that" >&2
    echo "instantiates templates costs sections; code that is merely long does not." >&2
    exit 1
fi

echo "OK: worst object is within budget."
