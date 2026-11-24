Buffer<uint> indices : register(t0);
RWStructuredBuffer<uint> result : register(u0);
[numthreads(64,1,1)]
void main(uint i : SV_DispatchThreadID) { result[i]=indices[i]; }
