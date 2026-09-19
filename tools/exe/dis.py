#!/usr/bin/env python3
"""Disassemble a VA range of TotalA.exe, one instruction a line, nops dropped.

    TOTALA_EXE=~/.rwe/Data/totala.exe uv run tools/exe/dis.py 0x48B710 0x48B91D

A thin wrapper over binutils' `objdump -d -M intel --start-address
--stop-address`, which reads the PE section table itself, so no VA-to-offset
mapping is needed. Written for the 0x2c decode (docs/TA-DEMOS.md, "0x2c, unit
state"), where the useful unit of reading was one routine at a time: the
builder, then each serialiser its calls lead to.

Two cautions carried over from docs/TOTALA-EXE.md section 109. objdump sweeps
linearly, so a jump table inside the range disassembles as nonsense and can hide
a real instruction after it; and a `call` target is resolved for you here, which
is what makes grepping a full listing for `call   0x415c10` find every user of a
routine when xref.py (absolute references only) finds none.

`--listing FILE` instead slices a listing already produced with
`objdump -d -M intel TotalA.exe > FILE`, which is much faster when reading many
small ranges.
"""

import argparse
import os
import re
import subprocess
import sys


def lines_from_objdump(exe, lo, hi):
    out = subprocess.run(
        ['objdump', '-d', '-M', 'intel', '--start-address=%#x' % lo, '--stop-address=%#x' % hi, exe],
        check=True, capture_output=True, text=True).stdout
    return out.splitlines()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('lo', help='first VA, hex')
    ap.add_argument('hi', help='VA to stop before, hex')
    ap.add_argument('--listing', help='slice an existing objdump listing instead of running objdump')
    args = ap.parse_args()

    lo = int(args.lo, 16)
    hi = int(args.hi, 16)
    if args.listing:
        lines = open(args.listing).read().splitlines()
    else:
        exe = os.environ.get('TOTALA_EXE', r'D:/Total Annihilation/Total Annihilation/TotalA.exe')
        lines = lines_from_objdump(exe, lo, hi)

    for line in lines:
        m = re.match(r'\s+([0-9a-f]+):\t[0-9a-f ]+\t(.*)', line)
        if not m:
            continue
        va = int(m.group(1), 16)
        if lo <= va < hi and not m.group(2).startswith('nop'):
            print('%x\t%s' % (va, m.group(2).strip()))


if __name__ == '__main__':
    sys.exit(main())
