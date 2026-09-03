import math
pal=open("D:/RWE-extract/totala1/palettes/PALETTE.PAL","rb").read()
shd=open("D:/RWE-extract/totala1/palettes/PALETTE.SHD","rb").read()
RGB=[(pal[i*4],pal[i*4+1],pal[i*4+2]) for i in range(256)]
def lum(c): return 0.299*c[0]+0.587*c[1]+0.114*c[2]

# texel population of the stock unit textures, recomputed cheaply from the cached histogram
import struct,os
def gaf_load(path,hist):
    d=open(path,"rb").read()
    ver,n,_=struct.unpack_from("<III",d,0)
    for p in struct.unpack_from("<%dI"%n,d,12):
        off,u=struct.unpack_from("<II",d,p+40)
        w,h,px,py,ti,comp,sub,unk2,fdo,unk3=struct.unpack_from("<HHhhBBHIII",d,off)
        if sub: continue
        if comp==0:
            for b in d[fdo:fdo+w*h]: hist[b]+=1
        else:
            q=fdo
            for y in range(h):
                ln=struct.unpack_from("<H",d,q)[0]; q+=2
                end=q+ln; x=0
                while q<end and x<w:
                    m=d[q]; q+=1
                    if m&1: x+=(m>>1)+1
                    elif m&2:
                        c=d[q]; q+=1
                        for _ in range((m>>2)+1):
                            if x<w: hist[c]+=1; x+=1
                    else:
                        for _ in range((m>>2)+1):
                            if q<end and x<w: hist[d[q]]+=1; q+=1; x+=1
                q=end
hist=[0]*256
D="D:/RWE-extract/totala1/textures"
for f in os.listdir(D):
    try: gaf_load(os.path.join(D,f),hist)
    except Exception: pass

# fit out = clamp( k * ( s*c + (1-s)*lum(c) ) ) per row, weighted by texel frequency.
# grid search, it is only 32 rows.
print("row   k     s    RMS(0-255)   RMS of the naive 0.06875*row multiply")
rows=[]
for row in range(32):
    best=None
    for ki in range(1,401):
        k=ki*0.01
        for si in range(0,101,2):
            s=si*0.01
            e=0.0; n=0
            for i in range(256):
                w=hist[i]
                if not w: continue
                c=RGB[i]; L=lum(c); o=RGB[shd[row*256+i]]
                for ch in range(3):
                    v=k*(s*c[ch]+(1-s)*L)
                    v=255.0 if v>255 else v
                    e+=w*(v-o[ch])**2
                n+=3*w
            r=math.sqrt(e/n)
            if best is None or r<best[0]: best=(r,k,s)
    # naive
    kn=0.06875*row; e=0.0; n=0
    for i in range(256):
        w=hist[i]
        if not w: continue
        c=RGB[i]; o=RGB[shd[row*256+i]]
        for ch in range(3):
            v=min(255.0,kn*c[ch]); e+=w*(v-o[ch])**2
        n+=3*w
    naive=math.sqrt(e/n)
    print("%3d  %.3f %.2f   %6.2f       %6.2f"%(row,best[1],best[2],best[0],naive))
    rows.append((row,best[1],best[2]))
print()
print("const float SHD_K[32] = float[32](%s);"%", ".join("%.3f"%r[1] for r in rows))
print("const float SHD_S[32] = float[32](%s);"%", ".join("%.2f"%r[2] for r in rows))
