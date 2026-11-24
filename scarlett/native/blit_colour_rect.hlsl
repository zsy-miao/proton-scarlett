#include "blit_colour.hlsl"

// For VS_Blit's TL, BL, BR producer only. Its clip W is 1.
[maxvertexcount(6)]
void rect_gs(triangle Vertex v[3], inout TriangleStream<Vertex> outTriangles) {
    if (any(v[0].pos != float4(-1, 1, 0, 1)) ||
        any(v[1].pos != float4(-1,-1, 0, 1)) ||
        any(v[2].pos != float4( 1,-1, 0, 1)))
        return;
    Vertex tr;
    tr.pos = v[0].pos + (v[2].pos - v[1].pos);
    tr.c   = v[0].c   + (v[2].c   - v[1].c);
    tr.uv  = v[0].uv  + (v[2].uv  - v[1].uv);

    outTriangles.Append(v[0]);
    outTriangles.Append(v[1]);
    outTriangles.Append(v[2]);
    outTriangles.RestartStrip();
    outTriangles.Append(v[0]);
    outTriangles.Append(v[2]);
    outTriangles.Append(tr);
}
