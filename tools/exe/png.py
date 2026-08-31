import zlib, struct, sys, os

def read_png(path):
    d = open(path,'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n'
    pos = 8
    idat = b''
    w=h=bd=ct=None
    pal=None; trns=None
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]
        typ = d[pos+4:pos+8]
        data = d[pos+8:pos+8+ln]
        if typ==b'IHDR':
            w,h,bd,ct,_,_,il = struct.unpack('>IIBBBBB', data)
            assert il==0, 'interlaced'
        elif typ==b'PLTE': pal=data
        elif typ==b'tRNS': trns=data
        elif typ==b'IDAT': idat += data
        elif typ==b'IEND': break
        pos += 12+ln
    raw = zlib.decompress(idat)
    ch = {0:1,2:3,3:1,4:2,6:4}[ct]
    assert bd==8, bd
    bpp = ch
    stride = w*bpp
    out = bytearray()
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; p+=1
        line = bytearray(raw[p:p+stride]); p+=stride
        if f==1:
            for i in range(bpp, stride): line[i]=(line[i]+line[i-bpp])&255
        elif f==2:
            for i in range(stride): line[i]=(line[i]+prev[i])&255
        elif f==3:
            for i in range(stride):
                a = line[i-bpp] if i>=bpp else 0
                line[i]=(line[i]+((a+prev[i])>>1))&255
        elif f==4:
            for i in range(stride):
                a = line[i-bpp] if i>=bpp else 0
                b = prev[i]
                c = prev[i-bpp] if i>=bpp else 0
                pa=abs(b-c); pb=abs(a-c); pc=abs(a+b-2*c)
                pr = a if (pa<=pb and pa<=pc) else (b if pb<=pc else c)
                line[i]=(line[i]+pr)&255
        out += line
        prev = line
    # convert to RGBA
    px = bytearray(w*h*4)
    for i in range(w*h):
        if ct==6: px[i*4:i*4+4]=out[i*4:i*4+4]
        elif ct==2: px[i*4:i*4+3]=out[i*3:i*3+3]; px[i*4+3]=255
        elif ct==3:
            idx=out[i]
            px[i*4:i*4+3]=pal[idx*3:idx*3+3]
            px[i*4+3]= trns[idx] if (trns and idx<len(trns)) else 255
        elif ct==0: v=out[i]; px[i*4:i*4+3]=bytes([v,v,v]); px[i*4+3]=255
        elif ct==4: v=out[i*2]; px[i*4:i*4+3]=bytes([v,v,v]); px[i*4+3]=out[i*2+1]
    return w,h,px

def write_png(path,w,h,px):
    raw=bytearray()
    for y in range(h):
        raw.append(0)
        raw += px[y*w*4:(y+1)*w*4]
    d=zlib.compress(bytes(raw),9)
    def chunk(t,data):
        return struct.pack('>I',len(data))+t+data+struct.pack('>I',zlib.crc32(t+data)&0xffffffff)
    out=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',d)+chunk(b'IEND',b'')
    open(path,'wb').write(out)
