#!/bin/sh
# Fails if a type is forward declared with one class key and defined with the
# other -- `struct Foo;` against `class Foo { ... };`.
#
# To gcc and clang that is legal and means nothing. To MSVC it is two types:
# the class key goes into the decorated name, so a function whose signature
# mentions the type is mangled one way in the translation unit that saw the
# forward declaration and the other way in the one that saw the definition,
# and neither finds the other. It surfaces as an unresolved external in the
# MSVC jobs alone, with the same human-readable signature on both sides of
# the error, differing only in the word `struct` or `class`.
#
# That is a thirty-minute round trip to learn from CI, and no Linux job will
# ever tell you. Hence this: the same check for the price of one grep.
#
# UnitState is what it was written for. It is a class; PerceptionManager.h
# said struct, which split contactStillStanding and tryGetUnitState in two and
# failed all four Windows jobs of 5428ffd4 while every Linux job passed.
#
# Every pattern below strips a trailing carriage return first, and that is not
# tidiness. Two files in the tree were committed with CRLF endings, and a
# \r is neither space nor tab, so `[ \t]*$` did not match a single line in
# either of them -- declaration or definition. The check therefore never
# learned that GameSimulation is a struct, and skipped all twenty-odd
# forward declarations of it as "defined elsewhere". It passed 54aeed98's
# parent green while that commit's `class GameSimulation;` in DemoRecorder.h
# failed both MSVC jobs, which is the exact bug this file exists to catch.
#
# It went unnoticed because MSYS2's tools strip the CR on the way in, so the
# same script run on the same bytes reported 93 declarations and a failure on
# Windows and 91 and success on Linux. The two files are LF now; this keeps
# the check honest if CRLF ever comes back.
#
# Usage: tools/class-key.sh [source-dir]

set -eu

SRC_DIR="${1:-src}"

if [ ! -d "$SRC_DIR" ]; then
    echo "class-key: no such source directory: $SRC_DIR" >&2
    exit 2
fi

FILES=$(find "$SRC_DIR" \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null | sort)

if [ -z "$FILES" ]; then
    echo "class-key: no C++ sources under $SRC_DIR" >&2
    exit 2
fi

# A single pass collects both halves and END pairs them up. A name defined
# under both keys somewhere in the tree is ambiguous rather than wrong -- a
# nested type sharing a name with a free one, say -- so it is left alone; this
# check has no notion of namespace or of enclosing scope and should not
# pretend to one.
report=$(
    # shellcheck disable=SC2086
    awk '
        { line = $0; sub(/\r$/, "", line); sub(/\/\/.*$/, "", line) }

        # A forward declaration: the whole statement on one line.
        line ~ /^[ \t]*(class|struct)[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*;[ \t]*$/ {
            key = line; sub(/^[ \t]*/, "", key); sub(/[ \t].*$/, "", key)
            name = line; sub(/^[ \t]*(class|struct)[ \t]+/, "", name)
            sub(/[ \t]*;.*$/, "", name)
            n++
            fk[n] = key; fn[n] = name
            ff[n] = FILENAME; fl[n] = FNR
            next
        }

        # A definition: same opening, but carrying a body, a base list or
        # nothing at all rather than a semicolon.
        line ~ /^[ \t]*(class|struct)[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*(final[ \t]*)?(:[^;]*)?[ \t]*[{]?[ \t]*$/ {
            key = line; sub(/^[ \t]*/, "", key); sub(/[ \t].*$/, "", key)
            name = line; sub(/^[ \t]*(class|struct)[ \t]+/, "", name)
            sub(/[ \t:{].*$/, "", name)
            if (!(name in defkey))
                defkey[name] = key
            else if (defkey[name] != key)
                defkey[name] = "both"
        }

        END {
            for (i = 1; i <= n; i++) {
                name = fn[i]
                if (!(name in defkey)) continue      # defined elsewhere
                if (defkey[name] == "both") continue # ambiguous, see above
                if (defkey[name] == fk[i]) continue  # agrees
                printf "%s\t%d\t%s\t%s\t%s\n", ff[i], fl[i], fk[i], name, defkey[name]
            }
            printf "checked\t%d\n", n
        }
    ' $FILES
)

checked=$(printf '%s\n' "$report" | awk -F'\t' '$1 == "checked" { print $2 }')
bad=$(printf '%s\n' "$report" | awk -F'\t' '$1 != "checked"' | grep -c . || true)

echo "Class keys: $checked forward declaration(s) in $SRC_DIR"
echo

if [ "$bad" -eq 0 ]; then
    echo "OK: every forward declaration agrees with its definition."
    exit 0
fi

printf '%s\n' "$report" | awk -F'\t' '$1 != "checked" {
    printf "  %s:%s: declared \"%s %s\" but defined as \"%s\"\n", $1, $2, $3, $4, $5
}'

echo
echo "FAIL: $bad forward declaration(s) disagree with the definition. MSVC mangles" >&2
echo "the class key into the symbol name, so each of these is two types to the" >&2
echo "linker. Change the declaration to match the definition." >&2
exit 1
