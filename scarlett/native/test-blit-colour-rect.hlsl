#include "blit_colour.hlsl"

float4 probe(Vertex i) : SV_Target {
    // Alpha counts visits with ONE+ONE. Diagnostic blue also tests the GS colour gradient.
    return float4(8 * i.uv, 0, 1) / 255 + float4(0, 0, i.c.r, 0);
}
