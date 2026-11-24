StructuredBuffer<uint> input : register(t0);
RWStructuredBuffer<uint> target : register(u0);
[numthreads(64,1,1)]
#ifdef WRITE
[RootSignature("DescriptorTable(UAV(u0))")]
void main(uint i : SV_DispatchThreadID) { target[i] ^= 0xd83ac217; }
#else
[RootSignature("DescriptorTable(SRV(t0)),UAV(u0)")]
void main(uint i : SV_DispatchThreadID) { target[i]=input[i]; }
#endif
