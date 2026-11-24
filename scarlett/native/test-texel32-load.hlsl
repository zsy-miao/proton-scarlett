#if defined(TEST_RGBA1_UINT)
Texture2D<uint4> image : register(t0);
#else
Texture2D<float4> image : register(t0);
#endif
RWStructuredBuffer<uint> results : register(u0);
[numthreads(1,1,1)] void main() {
#if defined(TEST_RGBA1_UINT)
    uint4 v=image.Load(int3(0,0,0));
#else
    float4 f=image.Load(int3(0,0,0));
    uint4 v=asuint(float4(f.r,f.r*2,f.g,f.a));
#endif
    results[0]=v.r; results[1]=v.g; results[2]=v.b; results[3]=v.a;
}
