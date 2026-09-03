import struct,os,sys
pal=open("D:/RWE-extract/totala1/palettes/PALETTE.PAL","rb").read()
shd=open("D:/RWE-extract/totala1/palettes/PALETTE.SHD","rb").read()
RGB=[(pal[i*4],pal[i*4+1],pal[i*4+2]) for i in range(256)]
def lum(c): return 0.299*c[0]+0.587*c[1]+0.114*c[2]
def sat(c):
    mx,mn=max(c),min(c)
    return 0.0 if mx==0 else (mx-mn)/mx

def gaf_load(path,hist):
    d=open(path,"rb").read()
    ver,n,_=struct.unpack_from("<III",d,0)
    ptrs=struct.unpack_from("<%dI"%n,d,12)
    for p in ptrs:
        frames,u1,u2=struct.unpack_from("<HHI",d,p)
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
    except Exception as e: pass
tot=sum(hist)
print("texels counted over every frame of every unit texture GAF: %d"%tot)
print("top 12 texel indices: %s"%", ".join("%d(%.1f%%)"%(i,100.0*hist[i]/tot) for i in sorted(range(256),key=lambda i:-hist[i])[:12]))
print()
print("What each shade row does to the REAL texel population of TA unit textures.")
print("f_black = fraction landing on palette 0; f_white = fraction landing on 255 or 80/251,251,251;")
print("meanLum = mean output luminance (input mean = %.1f); satRatio = mean saturation ratio."%(sum(hist[i]*lum(RGB[i]) for i in range(256))/tot))
print()
print("row  meanLum  lumRatio  f_black  f_white  satRatio")
inlum=sum(hist[i]*lum(RGB[i]) for i in range(256))/tot
insat=sum(hist[i]*sat(RGB[i]) for i in range(256))/tot
for row in range(32):
    ml=fb=fw=ms=0.0
    for i in range(256):
        if not hist[i]: continue
        o=shd[row*256+i]; c=RGB[o]
        ml+=hist[i]*lum(c); ms+=hist[i]*sat(c)
        if o==0 or sum(c)==0: fb+=hist[i]
        if o in (255,80): fw+=hist[i]
    print("%3d  %7.1f  %7.3f  %6.1f%%  %6.1f%%  %7.3f"%(row,ml/tot,(ml/tot)/inlum,100.0*fb/tot,100.0*fw/tot,(ms/tot)/insat))
