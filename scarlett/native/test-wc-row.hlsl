#ifdef TYPED
Buffer<uint> input : register(t0);
#else
StructuredBuffer<uint> input : register(t0);
#endif
#if defined(TYPED) && defined(WRITE)
RWBuffer<uint> target : register(u0);
#else
RWStructuredBuffer<uint> target : register(u0);
#endif
#ifdef TYPED
[numthreads(4,1,1)]
#else
[numthreads(64,1,1)]
#endif
#ifdef WRITE
[RootSignature("DescriptorTable(UAV(u0))")]
void main(uint i : SV_DispatchThreadID) { target[i] ^= 0xd83ac217; }
#else
[RootSignature("DescriptorTable(SRV(t0)),UAV(u0)")]
void main(uint i : SV_DispatchThreadID) { target[i]=input[i]; }
#endif
