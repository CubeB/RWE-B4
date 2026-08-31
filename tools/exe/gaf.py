import struct,sys
def load(p):
    d=open(p,'rb').read()
    ver,n,_=struct.unpack_from('<III',d,0)
    ptrs=struct.unpack_from('<%dI'%n,d,12)
    out=[]
    for p_ in ptrs:
        frames,u1,u2=struct.unpack_from('<HHI',d,p_)
        name=d[p_+8:p_+40].split(b'\0')[0].decode('latin1')
        fe=[]
        for i in range(frames):
            off,u=struct.unpack_from('<II',d,p_+40+i*8)
            w,h,px,py,ti,comp,sub,unk2,fdo,unk3=struct.unpack_from('<HHhhBBHIII',d,off)
            fe.append(dict(w=w,h=h,px=px,py=py,ti=ti,comp=comp,sub=sub,fdo=fdo))
        out.append((name,fe))
    return out
if __name__=='__main__':
    for name,fe in load(sys.argv[1]):
        print('ENTRY %r frames=%d'%(name,len(fe)))
        for i,f in enumerate(fe):
            print('  %2d w=%3d h=%3d posX=%4d posY=%4d transp=%3d comp=%d sub=%d'%(i,f['w'],f['h'],f['px'],f['py'],f['ti'],f['comp'],f['sub']))
