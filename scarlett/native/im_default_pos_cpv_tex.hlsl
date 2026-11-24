// im:default_pos_cpv_tex, pass 0 (VS71 / PS86).
struct Transform { float4 x, y, z, t; };
StructuredBuffer<Transform> object : register(t7, space1);
cbuffer Camera : register(b0, space1) { float4 clipRow[4] : packoffset(c22); }
Texture2D<float4> source : register(t277, space1);
SamplerState materialSampler : register(s14, space1);

float legacyMul(float a, float b) { return a == 0 || b == 0 ? 0 : a*b; }
struct V { float4 pos : SV_Position; float4 colour : COLOR0; float2 uv : TEXCOORD0; };
V vs(float3 p : POSITION, float4 colour : COLOR0, float2 uv : TEXCOORD0) {
    Transform m = object[0];
    precise float3 q = mad(p.z, m.z.xyz, mad(p.y, m.y.xyz, mad(p.x, m.x.xyz, m.t.xyz)));
    precise float4 a;
    [unroll] for (int k = 0; k < 4; ++k) a[k] = legacyMul(q.x, clipRow[0][k]);
    V v;
    precise float4 projected = mad(q.z, clipRow[2], mad(q.y, clipRow[1], a)) + clipRow[3];
    v.pos = projected; v.colour = colour; v.uv = uv;
    return v;
}
float4 ps(V v) : SV_Target0 {
    float4 texel = source.Sample(materialSampler, v.uv);
    precise float4 result;
    [unroll] for (int k = 0; k < 4; ++k) {
        float product = legacyMul(texel[k], v.colour[k]);
        uint h = f32tof16(product);
        result[k] = f16tof32(h - (abs(f16tof32(h)) > abs(product) ? 1u : 0u));
    }
    return result;
}
