Texture1D<float4> image : register(t0);
SamplerState pointSampler : register(s0);
RWStructuredBuffer<float4> result : register(u0);
[numthreads(64,1,1)] void main(uint x:SV_DispatchThreadID) {
    result[x]=image.SampleLevel(pointSampler,(x+0.5)/256.0,0);
}
