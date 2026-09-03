pal=open("D:/RWE-extract/totala1/palettes/PALETTE.PAL","rb").read()
shd=open("D:/RWE-extract/totala1/palettes/PALETTE.SHD","rb").read()
RGB=[(pal[i*4],pal[i*4+1],pal[i*4+2]) for i in range(256)]
reps=[(87,"mid grey (139,139,139)"),
      (84,"light grey (187,187,187)"),
      (91,"dark grey (75,75,75)"),
      (180,"khaki/olive (155,131,47)"),
      (104,"ARM blue (51,75,215)"),
      (201,"CORE red (215,35,0)"),
      (164,"green (75,171,43)"),
      (16,"pale pink (255,235,243)"),
      (249,"pure red (255,0,0)"),
      (255,"white (255,255,255)")]
for idx,name in reps:
    print()
    print("palette %3d  %s"%(idx,name))
    print("  row : idx  (r,g,b)            ideal = src*0.06875k")
    for row in range(32):
        o=shd[row*256+idx]; c=RGB[o]; k=0.06875*row
        ideal=tuple(min(255,int(v*k)) for v in RGB[idx])
        flag=""
        if o==idx: flag=" <- IDENTITY"
        print("  %3d : %3d  (%3d,%3d,%3d)   ideal (%3d,%3d,%3d)%s"%(row,o,c[0],c[1],c[2],ideal[0],ideal[1],ideal[2],flag))
