Texture2D<uint> tex : register(t0); RWStructuredBuffer<uint> result : register(u0);
[RootSignature("DescriptorTable(SRV(t0)),UAV(u0)")][numthreads(2,1,1)]
void main(uint i:SV_GroupIndex) { result[i]=tex.Load(int3(12+i,3,0)); }
