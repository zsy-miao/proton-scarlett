Texture1D<float4> image : register(t0);
SamplerState pointSampler : register(s0);
RWStructuredBuffer<float4> result : register(u0);
#ifndef ROW_WIDTH
#define ROW_WIDTH 256
#endif
[numthreads(64,1,1)] void main(uint x:SV_DispatchThreadID) {
    result[x]=image.SampleLevel(pointSampler,(x+0.5)/ROW_WIDTH,0);
}
