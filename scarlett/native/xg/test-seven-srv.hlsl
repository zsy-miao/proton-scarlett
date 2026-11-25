Texture2D<float> image : register(t0);
RWStructuredBuffer<uint> bits : register(u0);
[RootSignature("DescriptorTable(SRV(t0)),UAV(u0)")]
[numthreads(8,8,1)]
void main(uint3 p : SV_DispatchThreadID)
{
    if (p.x < (128u >> p.z) && p.y < (72u >> p.z))
        bits[(p.z*72+p.y)*128+p.x] = asuint(image.Load(int3(p)));
}
