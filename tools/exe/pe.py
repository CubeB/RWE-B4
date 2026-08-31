import struct, re, sys
p=r'D:/Total Annihilation/Total Annihilation/TotalA.exe'
d=open(p,'rb').read()
e_lfanew=struct.unpack('<I', d[0x3c:0x40])[0]
assert d[e_lfanew:e_lfanew+4]==b'PE\0\0'
coff=e_lfanew+4
nsec, = struct.unpack('<H', d[coff+2:coff+4])
optsz, = struct.unpack('<H', d[coff+16:coff+18])
opt=coff+20
magic,=struct.unpack('<H', d[opt:opt+2])
imagebase,=struct.unpack('<I', d[opt+28:opt+32])
print('nsec',nsec,'magic',hex(magic),'imagebase',hex(imagebase))
secs=[]
st=opt+optsz
for i in range(nsec):
    o=st+i*40
    name=d[o:o+8].rstrip(b'\0').decode()
    vsize,vaddr,rawsize,rawptr=struct.unpack('<IIII', d[o+8:o+24])
    secs.append((name,vaddr,vsize,rawptr,rawsize))
    print(name, hex(vaddr), hex(vsize), hex(rawptr), hex(rawsize))
def off2va(off):
    for name,va,vs,rp,rs in secs:
        if rp<=off<rp+rs: return imagebase+va+(off-rp)
    return None
def va2off(v):
    r=v-imagebase
    for name,va,vs,rp,rs in secs:
        if va<=r<va+vs: return rp+(r-va)
    return None
names=[b'smoke 1',b'smoke 2',b'fire1',b'alfboom1',b'radlogo',b'radlogohigh',b'nuclogo',b'h2oboom2',b'lavasplash',b'cannonshell',b'plasmasm',b'plasmamd',b'ultrashell',b'flamestream',b'explosion',b'explode2',b'explode3',b'explode4',b'explode5',b'nuke1',b'shadow']
addrs={}
for n in names:
    offs=[m.start() for m in re.finditer(re.escape(n)+b'\x00', d)]
    for o in offs:
        if d[o-1:o]==b'\x00':
            va=off2va(o)
            if va: addrs.setdefault(n,[]).append((hex(o),hex(va)))
for n in names: print(n, addrs.get(n))
