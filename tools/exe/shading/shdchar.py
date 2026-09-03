import math
pal=open("D:/RWE-extract/totala1/palettes/PALETTE.PAL","rb").read()
shd=open("D:/RWE-extract/totala1/palettes/PALETTE.SHD","rb").read()
RGB=[(pal[i*4],pal[i*4+1],pal[i*4+2]) for i in range(256)]
def lum(c): return 0.299*c[0]+0.587*c[1]+0.114*c[2]
def sat(c):
    mx,mn=max(c),min(c)
    return 0.0 if mx==0 else (mx-mn)/mx
# "real" entries: skip the duplicate blacks and the VGA reserved slots
real=[i for i in range(256) if sum(RGB[i])>12]
print("entries used in the analysis: %d (those with r+g+b>12)"%len(real))
print()
print("Per-row behaviour of the shipped table. err = mean |RGB(SHD[row][i]) - RGB(i)*k|")
print("over all three channels; nominal k = 0.06875*row.")
print()
print("row   k_nom  k_fit  meanLumRatio  meanRGBerr  maxRGBerr  meanHueShift  identity  meanSatRatio")
for row in range(32):
    k=0.06875*row
    num=den=0.0; errs=[]; hues=[]; sats=[]; ident=0
    for i in real:
        o=RGB[shd[row*256+i]]; s=RGB[i]
        if shd[row*256+i]==i: ident+=1
        ideal=tuple(min(255,int(c*k)) for c in s)
        errs.append(sum(abs(o[c]-ideal[c]) for c in range(3))/3.0)
        num+=lum(o); den+=lum(s)
        # hue shift: angle between the two colour vectors in RGB space
        no=math.sqrt(sum(c*c for c in o)); ns=math.sqrt(sum(c*c for c in s))
        if no>0 and ns>0:
            d=sum(o[c]*s[c] for c in range(3))/(no*ns)
            hues.append(math.degrees(math.acos(max(-1,min(1,d)))))
        if sat(s)>0.05: sats.append(sat(o)/sat(s))
    # least-squares fit of a single scalar k mapping source RGB to output RGB
    a=sum(RGB[i][c]*RGB[shd[row*256+i]][c] for i in real for c in range(3))
    b=sum(RGB[i][c]**2 for i in real for c in range(3))
    print("%3d  %6.4f %6.4f   %8.4f     %6.1f     %5d      %6.2f      %4d     %6.3f"%(
        row,k,a/b,num/den,sum(errs)/len(errs),int(max(errs)),
        sum(hues)/len(hues) if hues else 0,ident,
        sum(sats)/len(sats) if sats else 0))
