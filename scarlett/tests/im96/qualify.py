"""Verify both selected whole containers in memory. Input: local shaders.rpf 1.0.2.1."""
import hashlib
import struct
import sys
import zlib

records = [
    (271202363,'sga_dr_vs_pos_color_tex_c32788bc_Wrapped',1208,1712,'c65eb8c9af8fcf7389a20872ba42eb1357ccc92a5ae5bb71842ced435741fdb4'),
    (271942256,'default_ps_tex_c32788bc_Wrapped',1162,1588,'5cd73cc975d0976da64f3b1a1e882e49bc99c064db540fabb4eb1bb8614cfe9f')]
with open(sys.argv[1],'rb') as f:
    for offset,name,size,raw,digest in records:
        f.seek(offset)
        label = name.encode()+b'\0'
        if f.read(len(label)+2) != bytes([len(label)]) + label + b'2':
            raise ValueError(name+' header')
        n,_,dup,r = struct.unpack('<4I',f.read(16))
        if (n,dup,r)!=(size,size,raw): raise ValueError(name+' size')
        z = zlib.decompressobj(-15)
        container = z.decompress(f.read(n),raw+1)
        if not z.eof or z.unconsumed_tail or len(z.unused_data)!=12 or len(container)!=raw or hashlib.sha256(container).hexdigest()!=digest:
            raise ValueError(name+' extent/digest')
        print('PASS',name,digest)
