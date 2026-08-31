import struct,sys
p = r"D:\Total Annihilation\Total Annihilation\TotalA.exe"
d = open(p,'rb').read()
secs = [(".text",0x401000,0x400,0xfa92a),(".rdata",0x4fc000,0xfae00,0x468c),(".data",0x501000,0xff600,0x10a00)]
def v2f(va):
    for n,v,fo,sz in secs:
        if v <= va < v+sz: return fo + (va-v)
va=int(sys.argv[1],16); n=int(sys.argv[2]) if len(sys.argv)>2 else 12
o=v2f(va)
for i in range(n):
    val=struct.unpack_from("<I", d, o+4*i)[0]
    print("  +0x%02x -> 0x%08x" % (i*4, val))
