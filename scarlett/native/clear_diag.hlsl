#include "clear_pair.hlsl"
// One canonical nonuniform control. No extra resource or root parameter.
struct Export { float4 rgba : SV_Target0; uint mask : SV_Coverage; };
Export diagonal(Vertex v) {
    Export o;
    uint2 p=uint2(v.pos.xy);
    o.mask = ((p.x ^ p.y) & 1) ? 0 : 0xffff;
    o.rgba = ps(v);
    return o;
}
