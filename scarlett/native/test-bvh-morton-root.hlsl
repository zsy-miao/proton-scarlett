cbuffer Params : register(b0) { uint4 lo, hi; }
RWByteAddressBuffer a : register(u0);
RWByteAddressBuffer b : register(u1);
RWByteAddressBuffer c : register(u2);
RWByteAddressBuffer d : register(u3);
RWByteAddressBuffer e : register(u4);

[numthreads(8,1,1)]
void main(uint i : SV_GroupIndex)
{
    uint k = i < 4 ? lo[i & 3] : hi[i & 3];
    a.Store(i*4, k+1);
    b.Store(i*4, k+2);
    c.Store(i*4, k+3);
    d.Store(i*4, k+4);
    e.Store(i*4, k+5);
}
