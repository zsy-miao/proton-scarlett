Texture2D<float> image : register(t0);
SamplerState pointWrap : register(s0);
RWStructuredBuffer<float> results : register(u0);
[numthreads(16,8,1)] void main(uint3 id: SV_DispatchThreadID) {
    float2 uv=(float2(id.xy)+0.5)/float2(256,128)+float2(-2,3);
    results[id.y*256+id.x]=image.SampleLevel(pointWrap,uv,0);
}
