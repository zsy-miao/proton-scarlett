Texture2D<float4> color : register(t0);
SamplerState linear_wrap : register(s0);
SamplerState point_wrap : register(s1);
RWByteAddressBuffer output_words : register(u0);

[numthreads(3,1,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float2 uv = (float2(3838 + id.x,2159) + 0.5) / float2(3840,2160);
    if (id.x==2) uv.x=1; // Exactly between the last red and wrapped first green texels.
    uint4 c = uint4(round(color.SampleLevel(linear_wrap,uv,0) * 255.0));
    output_words.Store(id.x * 4,c.r | c.g << 8 | c.b << 16 | c.a << 24);
    c = uint4(round(color.SampleLevel(point_wrap,uv,0) * 255.0));
    output_words.Store(12 + id.x * 4,c.r | c.g << 8 | c.b << 16 | c.a << 24);
}
