// Fixed translation of the two digest-qualified SM6.6 native programs.
ByteAddressBuffer vertices : register(t0, space3);
struct Vertex {
    float4 pos : SV_Position;
    nointerpolation float4 attr : TEXCOORD0;
};
uint address(uint header, uint id) {
    return ((header >> 7) & 0x1fffffc) + id * ((header >> 3) & 60);
}
Vertex vs(uint id : SV_VertexID) {
    Vertex o;
    o.pos = float4(0,0,0,1);
    o.attr = 0;
    uint h = vertices.Load(0);
    if ((h & 15) == 3) o.pos.xyz = asfloat(vertices.Load3(address(h,id)));
    h = vertices.Load(12);
    if ((h & 15) == 4) o.attr = asfloat(vertices.Load4(address(h,id)));
    return o;
}
// v_cvt_pkrtz_f16_f32 before the UNORM export. Correct a round-to-nearest
// conversion only if it went away from zero (also handles finite overflow).
float rtz(float v) {
    uint h = f32tof16(v);
    if (abs(f16tof32(h)) > abs(v)) --h;
    return f16tof32(h);
}
float4 ps(Vertex v) : SV_Target0 {
    float4 h = float4(rtz(v.attr.x),rtz(v.attr.y),rtz(v.attr.z),rtz(v.attr.w));
    // The qualified no-blend RGBA8 target permits selecting the byte before host export.
    return round(saturate(h) * 255.0) / 255.0;
}

// The sRGB attachment encodes RGB after the native half-RTZ export. Rounding
// first to linear UNORM8 would lose dark colors. Alpha is linear UNORM8.
float4 ps_srgb(Vertex v) : SV_Target0 {
    return float4(rtz(v.attr.x), rtz(v.attr.y), rtz(v.attr.z),
        round(saturate(rtz(v.attr.w))*255.0)/255.0);
}
