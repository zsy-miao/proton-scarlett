// Fixture only: VS71 has already applied the object and camera transforms.
struct V { float4 pos : SV_Position; float4 colour : COLOR0; float2 uv : TEXCOORD0; };
[maxvertexcount(6)]
void gs(triangle V p[3], inout TriangleStream<V> stream) {
    V a=p[0], b=p[1], c=p[2], d;
    if (!all(isfinite(a.pos)) || !all(isfinite(b.pos)) || !all(isfinite(c.pos)) ||
        !(a.pos.w>0 && a.pos.w==b.pos.w && a.pos.w==c.pos.w &&
          a.pos.x==b.pos.x && b.pos.y==c.pos.y &&
          a.pos.y>b.pos.y && c.pos.x>b.pos.x)) return;
    // The producer writes constant Z. Only qualify a constant-depth projection.
    if (a.pos.z!=b.pos.z || a.pos.z!=c.pos.z) return;
    d.pos=float4(c.pos.x,a.pos.y,a.pos.z,a.pos.w);
    d.uv=a.uv+(c.uv-b.uv);
    d.colour=a.colour+(c.colour-b.colour);
    stream.Append(a); stream.Append(b); stream.Append(c); stream.RestartStrip();
    stream.Append(a); stream.Append(c); stream.Append(d);
}
