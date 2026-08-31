import sys, struct, re
p = r"D:\Total Annihilation\Total Annihilation\TotalA.exe"
d = open(p,'rb').read()
secs = [(".text",0x401000,0x400,0xfa92a),(".rdata",0x4fc000,0xfae00,0x468c),(".data",0x501000,0xff600,0x10a00),(".tls",0x52c000,0x110000,0x14),(".rsrc",0x52d000,0x110200,0xa58)]
def f2v(off):
    for n,v,fo,sz in secs:
        if fo <= off < fo+sz: return v + (off-fo)
    return None
for a in sys.argv[1:]:
    va = int(a,16)
    pat = struct.pack("<I", va)
    print("=== refs to 0x%08x ===" % va)
    for m in re.finditer(re.escape(pat), d):
        off = m.start()
        rv = f2v(off)
        sec = [n for n,v,fo,sz in secs if fo<=off<fo+sz]
        print("  at file 0x%06x va 0x%08x (%s) ctx: %s" % (off, rv or 0, sec[0] if sec else '?', d[off-6:off+6].hex()))
