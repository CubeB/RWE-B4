# Reproduce 0x4BADF0 exactly and compare against the shipped palettes/PALETTE.SHD
pal=open("D:/RWE-extract/totala1/palettes/PALETTE.PAL","rb").read()
shd=open("D:/RWE-extract/totala1/palettes/PALETTE.SHD","rb").read()
RGB=[(pal[i*4],pal[i*4+1],pal[i*4+2]) for i in range(256)]

# 0x4BA920: sums + exchange sort ascending, index array carried along
sums=[sum(RGB[i]) for i in range(256)]
idx=list(range(256))
for i in range(256):
    for j in range(i+1,256):
        if sums[i]>sums[j]:
            sums[i],sums[j]=sums[j],sums[i]
            idx[i],idx[j]=idx[j],idx[i]

def nearest(r,g,b):
    # 0x4BA9D0: window +-40 on r+g+b in the sorted order, min squared RGB distance,
    # first strict minimum wins
    t=r+g+b; lo=t-40; hi=t+40
    best=1000000000; bestpos=None
    for p in range(256):
        s=sums[p]
        if s<lo: continue
        if s>hi: break
        pr,pg,pb=RGB[idx[p]]
        d=(pr-r)**2+(pg-g)**2+(pb-b)**2
        if d<best:
            best=d; bestpos=p
    if bestpos is None: bestpos=0
    return idx[bestpos]

gen=bytearray(8192)
scale=0.0
for row in range(32):
    for i in range(256):
        c=[]
        for k in range(3):
            v=int(RGB[i][k]*scale)          # _ftol: truncate toward zero
            c.append(255 if (v&0xffff)>255 else v&0xff)
        gen[row*256+i]=nearest(*c)
    scale+=0.06875

diff=[i for i in range(8192) if gen[i]!=shd[i]]
print("generated vs shipped: %d of 8192 bytes differ"%len(diff))
if diff[:20]:
    for i in diff[:20]:
        print("   row %2d entry %3d: gen %3d shipped %3d"%(i//256,i%256,gen[i],shd[i]))
