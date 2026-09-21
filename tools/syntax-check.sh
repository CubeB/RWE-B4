#!/bin/sh
# Compile one translation unit for diagnostics only, without touching the
# build tree.
#
# `make` in either tree is not safe to run while anything is editing sources,
# and two of them at once is worse (CLAUDE.md, "never edit a source file while
# a make is running"). This reads the exact command CMake recorded for a file
# in compile_commands.json, adds -fsyntax-only, and runs that. Nothing is
# written, no object is stamped, and several may run at once.
#
# A header has no entry of its own, so check a .cpp that includes it.
#
# Usage: tools/syntax-check.sh <source.cpp> [build-dir]

set -eu

SRC="${1:?usage: tools/syntax-check.sh <source.cpp> [build-dir]}"
BUILD_DIR="${2:-build}"
DB="$BUILD_DIR/compile_commands.json"
PYTHON="${PYTHON:-/d/msys64/mingw64/bin/python.exe}"

[ -f "$DB" ] || { echo "syntax-check: no $DB" >&2; exit 2; }
command -v "$PYTHON" >/dev/null 2>&1 || PYTHON=python3

CMD=$("$PYTHON" - "$DB" "$SRC" <<'PY'
import json, sys, os
db, want = sys.argv[1], sys.argv[2].replace(chr(92), '/')
base = os.path.basename(want)
entries = json.load(open(db))
hit = None
for e in entries:
    f = e['file'].replace(chr(92), '/')
    if f.endswith(want) or (hit is None and os.path.basename(f) == base):
        hit = e
        if f.endswith(want):
            break
if hit is None:
    sys.exit('syntax-check: %s is not in compile_commands.json' % want)
cmd = hit['command']
# Drop the output object and anything that would write to the build tree.
out = []
skip = False
for tok in cmd.split():
    if skip:
        skip = False
        continue
    if tok == '-o':
        skip = True
        continue
    if tok.startswith('-Xclang') or tok.endswith('.obj'):
        continue
    out.append(tok)
out.append('-fsyntax-only')
print(hit['directory'] + '\n' + ' '.join(out))
PY
)

DIR=$(printf '%s\n' "$CMD" | head -1)
LINE=$(printf '%s\n' "$CMD" | tail -1)

cd "$DIR"
PATH="/d/msys64/mingw64/bin:$PATH"
export PATH
eval "$LINE"
