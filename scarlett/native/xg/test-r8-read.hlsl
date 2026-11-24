ByteAddressBuffer texels : register(t0);
RWStructuredBuffer<float> results : register(u0);

[numthreads(16,8,1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint x=tid.x, y=tid.y;
    uint tile=(y>>6)*4+(x>>6);
    uint byteOffset=(tile<<12) | (x & 15) | ((y & 31)<<4) |
                    ((x & 16)<<5) | ((y & 32)<<5) | ((x & 32)<<6);
    uint word=texels.Load(byteOffset & ~3u);
    uint v=(word>>((byteOffset & 3)*8)) & 255;
    results[y*256+x]=float(v)/255.0;
}
