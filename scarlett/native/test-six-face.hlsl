#ifdef TEST_ARRAY6
Texture2DArray<float4> image : register(t0);
#else
TextureCube<float4> image : register(t0);
SamplerState pointSampler : register(s0);
#endif
RWStructuredBuffer<float4> result : register(u0);
[numthreads(6,1,1)] void main(uint i:SV_GroupThreadID) {
#ifdef TEST_ARRAY6
    result[i]=image.Load(int4(0,0,i,0));
#else
    const float3 dir[6]={float3(1,0,0),float3(-1,0,0),float3(0,1,0),float3(0,-1,0),float3(0,0,1),float3(0,0,-1)};
    result[i]=image.SampleLevel(pointSampler,dir[i],0);
#endif
}
