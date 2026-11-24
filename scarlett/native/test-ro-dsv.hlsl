Texture2D<float> z : register(t0);
Texture2D<uint2> s : register(t1);
RWStructuredBuffer<uint2> report : register(u0);
[RootSignature("DescriptorTable(SRV(t0,numDescriptors=2)),UAV(u0)")]
void ps(float4 pos : SV_Position) {
    uint2 p=uint2(pos.xy);
    report[p.x & 3]=uint2(asuint(z.Load(int3(p,0))),s.Load(int3(p,0)).g);
}
