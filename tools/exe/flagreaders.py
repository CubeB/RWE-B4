# Find every routine that reads one bit of the weapon definition's flag word.
#
# The weapon TDF parser packs some thirty booleans into the dword at
# wdef+0x111, so grepping the disassembly for the offset gives you the parser
# and every reader of every flag at once -- a hundred-odd hits, most of them
# nothing to do with the bit you care about. The compiler isolates a bit in one
# of two ways, either testing the dword against a mask or shifting the bit down
# and testing the low byte, and this walks forward from each load looking for
# whichever it used.
#
# Usage: python flagreaders.py <bit> [listing]
#   bit      0-31, from the table in docs/TOTALA-EXE.md section 10
#   listing  a flat objdump of .text, one instruction per line as
#            "  <hex addr>:\t<text>"; defaults to totala.asm beside this file.
#
# The parser's own stores show up as `and <reg>,~mask` lines in the 0x42Exxx
# range; filter those out and what is left is the readers.

import os
import re
import sys

LISTING = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "totala.asm")

# The flag word's offset within the weapon definition.
FLAGS_OFFSET = "0x111"

bit = int(sys.argv[1])
mask = 1 << bit

line_re = re.compile(r"^\s+([0-9a-f]+):\t(.*)$")
instructions = []
for line in open(LISTING, errors="replace"):
    m = line_re.match(line.rstrip("\n"))
    if m:
        instructions.append((int(m.group(1), 16), m.group(2).strip()))

load_re = re.compile(r"^mov\s+(e[a-z][a-z]),DWORD PTR \[(.+?)\+" + FLAGS_OFFSET + r"\]$")

for i, (addr, text) in enumerate(instructions):
    m = load_re.match(text)
    if not m:
        continue
    reg = m.group(1)

    # Follow the register forward until something clobbers it.
    for j in range(i + 1, min(i + 25, len(instructions))):
        addr2, text2 = instructions[j]
        if re.match(r"^(mov|lea|xor)\s+" + reg + r",", text2):
            break

        direct = re.search(r"(test|and|or)\s+" + reg + r",0x([0-9a-f]+)", text2)
        if direct and (int(direct.group(2), 16) & mask):
            print("%08x  %-44s   -> %08x  %s" % (addr, text, addr2, text2))
            break

        shift = re.search(r"(shr|sar)\s+" + reg + r",0x([0-9a-f]+)", text2)
        if shift:
            places = int(shift.group(2), 16)
            if places > bit:
                break
            # The bit is now somewhere in the low byte; find the test of it.
            for k in range(j + 1, min(j + 6, len(instructions))):
                addr3, text3 = instructions[k]
                low = re.search(r"(test|and)\s+([a-z]l|[a-z]h),0x([0-9a-f]+)", text3)
                if not low:
                    continue
                value = int(low.group(3), 16)
                if low.group(2).endswith("h"):
                    value <<= 8
                if (value << places) & mask:
                    print("%08x  %-44s   -> %08x %s ; %08x %s" % (addr, text, addr2, text2, addr3, text3))
                    break
            break
