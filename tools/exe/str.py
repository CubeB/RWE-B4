import sys
d=open(r'D:/Total Annihilation/Total Annihilation/TotalA.exe','rb').read()
secs=[(0x400,0x401000,0xfa92a),(0xfae00,0x4fc000,0x468c),(0xff600,0x501000,0x10a00)]
def rd(vma,n):
    for off,base,size in secs:
        if base<=vma<base+size:
            return d[off+(vma-base):off+(vma-base)+n]
for a in sys.argv[1:]:
    v=int(a,16); b=rd(v,120)
    z=b.split(b'\0')[0]
    print("%08x: %r"%(v,z))
