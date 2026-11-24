Texture2D<float4> previous : register(t0);
RWTexture2D<float4> next : register(u0);
[numthreads(16,16,1)] void main(uint3 id:SV_DispatchThreadID) {
    float4 c=previous.Load(int3(id.xy,0));
    next[id.xy]=float4(c.b,c.r,c.b,1);
}
