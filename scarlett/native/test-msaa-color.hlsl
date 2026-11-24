float4 vs(uint i:SV_VertexID):SV_Position {
    return float4(i==1?3:-1,i==2?3:-1,0.5,1);
}
float4 ps(uint s:SV_SampleIndex):SV_Target {
    return float4(4+4*s,12+8*s,128+12*s,240-16*s)/255.0;
}
Texture2DMS<float4,4> tex : register(t0);
RWStructuredBuffer<uint> values : register(u0);
[numthreads(4,1,1)] void cs(uint s:SV_DispatchThreadID) {
    uint4 v=uint4(round(tex.Load(int2(0,0),s)*255));
    values[s]=v.r|(v.g<<8)|(v.b<<16)|(v.a<<24);
}
