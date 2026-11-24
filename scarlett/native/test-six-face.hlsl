#ifdef TEST_ARRAY6
Texture2DArray<float4> image : register(t0);
#else
#ifdef TEST_CUBE_PAIR
TextureCubeArray<float4> image : register(t0);
#else
TextureCube<float4> image : register(t0);
#endif
SamplerState pointSampler : register(s0);
#endif
RWStructuredBuffer<float4> result : register(u0);
#ifdef TEST_CUBE_PAIR
[numthreads(12,1,1)]
#else
[numthreads(6,1,1)]
#endif
void main(uint i:SV_GroupThreadID) {
#ifdef TEST_ARRAY6
    result[i]=image.Load(int4(0,0,i,0));
#else
    const float3 dir[6]={float3(1,0,0),float3(-1,0,0),float3(0,1,0),float3(0,-1,0),float3(0,0,1),float3(0,0,-1)};
#ifdef TEST_CUBE_PAIR
    result[i]=image.SampleLevel(pointSampler,float4(dir[i%6],i/6),0);
#else
    result[i]=image.SampleLevel(pointSampler,dir[i],0);
#endif
#endif
}
