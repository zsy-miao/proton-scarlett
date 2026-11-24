float4 vs(uint i:SV_VertexID):SV_Position {
    return float4(i==1?3:-1,i==2?3:-1,0.5,1);
}
float ps(uint s:SV_SampleIndex):SV_Depth { return (s+1)*0.125; }
Texture2DMS<float,4> depth : register(t0);
Texture2DMS<uint2,4> stencil : register(t1);
RWStructuredBuffer<uint2> values : register(u0);
[numthreads(4,1,1)] void cs(uint s:SV_DispatchThreadID) {
    values[s]=uint2(asuint(depth.Load(int2(0,0),s)),stencil.Load(int2(0,0),s).g);
}
