// A different sentinel for each pixel proves the PS consumes this table CBV.
cbuffer Colour : register(b27, space1) { uint4 values : packoffset(c2); }
uint ps(float4 position : SV_Position) : SV_Target { return values[(uint)position.x]; }
