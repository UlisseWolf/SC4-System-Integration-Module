import struct
def read(path):
    d=open(path,'rb').read()
    assert d[:4]==b'DBPF'
    major,minor=struct.unpack_from('<II',d,4)
    idx_ver=struct.unpack_from('<I',d,0x3C)[0]
    count,off,size=struct.unpack_from('<III',d,0x24)
    entries=[]
    for i in range(count):
        t,g,ins,o,s=struct.unpack_from('<IIIII',d,off+i*20)
        entries.append(dict(t=t,g=g,i=ins,data=d[o:o+s]))
    return d,entries
def write(path, entries, header_src):
    # DBPF 1.0, index version 7, no DIR (entries are uncompressed)
    body=bytearray(header_src[:0x60])
    body[0x24:0x30]=b'\0'*12
    out=bytearray(body)
    idx=[]
    for e in entries:
        idx.append((e['t'],e['g'],e['i'],len(out),len(e['data'])))
        out+=e['data']
    ioff=len(out)
    for t,g,i,o,s in idx: out+=struct.pack('<IIIII',t,g,i,o,s)
    struct.pack_into('<III',out,0x24,len(idx),ioff,20*len(idx))
    open(path,'wb').write(out)
