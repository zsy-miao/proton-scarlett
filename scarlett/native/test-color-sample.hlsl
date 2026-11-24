Texture2D<float4> color : register(t0);
SamplerState nearest : register(s0);
RWByteAddressBuffer output_words : register(u0);

[numthreads(2,1,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float2 uv = (float2(3838 + id.x,2159) + 0.5) / float2(3840,2160);
    uint4 c = uint4(round(color.SampleLevel(nearest,uv,0) * 255.0));
    output_words.Store(id.x * 4,c.r | c.g << 8 | c.b << 16 | c.a << 24);
}
