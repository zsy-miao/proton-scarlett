ByteAddressBuffer source : register(t0);
RWByteAddressBuffer result : register(u0);
cbuffer Words : register(b0) { uint4 words[4]; };

[numthreads(16,1,1)]
void main(uint i : SV_GroupIndex)
{
    result.Store(i*4,source.Load((15-i)*4)^words[i/4][i%4]);
}
