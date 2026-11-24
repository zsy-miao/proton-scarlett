Texture2D<float4> tex : register(t0); RWStructuredBuffer<float4> result : register(u0);
[RootSignature("DescriptorTable(SRV(t0)),UAV(u0)")][numthreads(2,1,1)]
void main(uint i:SV_GroupIndex) { result[i]=tex.Load(int3(12+i,0,0)); }
