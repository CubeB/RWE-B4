#!/usr/bin/env python3
"""List the instructions of named functions in a compiled unit script (.cob).

    tools/exe/coblist.py ~/ta-mods/x-esc/TAESC.gp3/scripts/CORCA.cob StartBuilding StopBuilding

Written for docs/TOTALA-EXE.md section 107, where whether a construction
aircraft gets its second increment on the creation tick comes down to what its
script `set`s and when -- every SET_VALUE raises bit 0x4 of the unit's event
word, whatever the port -- and a mod ships the .cob, not the .bos. Port numbers
are TA's: 5 is INBUILDSTANCE, 6 BUSY, 20 ARMORED; see src/rwe/cob/CobValueId.h.
With no function names it lists every function. Opcodes follow
src/rwe/cob/CobOpCode.h.
"""

import struct
import sys

OPS = {
    0x10001000: ("move", 2), 0x10002000: ("turn", 2), 0x10003000: ("spin", 2), 0x10004000: ("stop-spin", 2),
    0x10005000: ("show", 1), 0x10006000: ("hide", 1), 0x10007000: ("cache", 1), 0x10008000: ("dont-cache", 1),
    0x1000B000: ("move-now", 2), 0x1000C000: ("turn-now", 2), 0x1000D000: ("shade", 1),
    0x1000E000: ("dont-shade", 1), 0x1000F000: ("emit-sfx", 1), 0x10011000: ("wait-for-turn", 2),
    0x10012000: ("wait-for-move", 2), 0x10013000: ("sleep", 0), 0x10021001: ("push-const", 1),
    0x10021002: ("push-local", 1), 0x10021004: ("push-static", 1), 0x10022000: ("create-local", 0),
    0x10023002: ("pop-local", 1), 0x10023004: ("pop-static", 1), 0x10024000: ("pop-stack", 0),
    0x10031000: ("add", 0), 0x10032000: ("sub", 0), 0x10033000: ("mul", 0), 0x10034000: ("div", 0),
    0x10035000: ("and", 0), 0x10036000: ("or", 0), 0x10037000: ("xor", 0), 0x10038000: ("not", 0),
    0x10041000: ("rand", 0), 0x10042000: ("get", 0), 0x10043000: ("get-with-args", 0), 0x10082000: ("set", 0),
    0x10051000: ("<", 0), 0x10052000: ("<=", 0), 0x10053000: (">", 0), 0x10054000: (">=", 0),
    0x10055000: ("==", 0), 0x10056000: ("!=", 0), 0x10057000: ("&&", 0), 0x10058000: ("||", 0),
    0x10059000: ("^^", 0), 0x1005A000: ("!", 0), 0x10061000: ("start-script", 2), 0x10062000: ("call-script", 2),
    0x10064000: ("jump", 1), 0x10065000: ("return", 0), 0x10066000: ("jump-if-zero", 1), 0x10067000: ("signal", 0),
    0x10068000: ("set-signal-mask", 0), 0x10071000: ("explode", 1), 0x10083000: ("attach-unit", 0),
    0x10084000: ("drop-unit", 0),
}


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    d = open(sys.argv[1], "rb").read()
    h = struct.unpack("<11I", d[:44])
    nscripts, codelen = h[1], h[3]
    code = struct.unpack(f"<{codelen}I", d[h[9] : h[9] + 4 * codelen])
    addrs = struct.unpack(f"<{nscripts}I", d[h[6] : h[6] + 4 * nscripts])
    names = [
        d[o : d.index(b"\0", o)].decode("latin-1")
        for o in struct.unpack(f"<{nscripts}I", d[h[7] : h[7] + 4 * nscripts])
    ]
    wanted = {n.lower() for n in sys.argv[2:]}
    starts = sorted(set(addrs)) + [codelen]
    for i in sorted(range(nscripts), key=lambda i: addrs[i]):
        if wanted and names[i].lower() not in wanted:
            continue
        print(f"{names[i]}:")
        pc, end = addrs[i], starts[starts.index(addrs[i]) + 1]
        while pc < end:
            name, n = OPS.get(code[pc], (f"?{code[pc]:08x}", 0))
            args = code[pc + 1 : pc + 1 + n]
            note = f"   ({names[args[0]]})" if name in ("start-script", "call-script") and args[0] < nscripts else ""
            print(f"  {pc:5d}  {name:<14} {' '.join(str(a) for a in args)}{note}")
            pc += 1 + n


if __name__ == "__main__":
    main()
