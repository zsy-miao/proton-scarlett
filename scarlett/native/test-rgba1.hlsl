Texture2D<float4> image : register(t0);
SamplerState pointWrap : register(s0);
RWStructuredBuffer<float> results : register(u0);
[numthreads(1,1,1)] void main() {
    float4 rgba=image.SampleLevel(pointWrap,float2(-1.5,3.5),0);
    results[0]=rgba.r; results[1]=rgba.g; results[2]=rgba.b; results[3]=rgba.a;
}
