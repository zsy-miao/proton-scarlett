struct Vertex {
    float4 pos : SV_Position;
    linear float4 c : COLOR0;
    linear float2 uv : TEXCOORD0;
};
Vertex vs(float3 xyz : POSITION, float4 c : COLOR0, float2 uv : TEXCOORD0) {
    Vertex v = { float4(xyz, 1), c, uv };
    return v;
}
cbuffer Modulation : register(b27, space1) { float4 scale : packoffset(c2); }

// Same DX9 multiply rule and half-RTZ correction as tests/native-ps/native_pair.py.
float lane(float a, float b) {
    precise float x = (a == 0 || b == 0) ? 0.0 : a*b;
    uint h = f32tof16(x);
    if (abs(f16tof32(h)) > abs(x)) --h;
    return f16tof32(h);
}
float4 ps(Vertex i) : SV_Target0 {
    return float4(lane(i.c.r, scale.r), lane(i.c.g, scale.g),
                  lane(i.c.b, scale.b), lane(i.c.a, scale.a));
}
