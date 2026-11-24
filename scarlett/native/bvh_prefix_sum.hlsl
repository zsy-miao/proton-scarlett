// Compile separately with -D N=64,128,256,512,1024,2048,4096 (one value at a time).
#if N == 64 || N == 128
#define T 32
#elif N == 256 || N == 512
#define T 64
#elif N == 1024
#define T 256
#elif N == 2048 || N == 4096
#define T 512
#else
#error Unsupported PrefixSum kernel
#endif
#define W (N/T)
RWByteAddressBuffer InCounterOutPrefix : register(u0);
RWByteAddressBuffer OutThreadGroupSums : register(u1);
cbuffer ShaderParams : register(b0) { uint ShouldOutputTGSum; }
groupshared uint partial[T];

[numthreads(T,1,1)]
void main(uint3 group : SV_GroupID, uint tid : SV_GroupIndex)
{
    uint addr = (group.x * N + tid * W) * 4;
    uint x[W], total = 0;
    [unroll] for (uint j = 0; j < W; ++j) {
        x[j] = InCounterOutPrefix.Load(addr + 4*j);
        total += x[j];
    }
    uint inclusive = total;
    partial[tid] = inclusive;
    GroupMemoryBarrierWithGroupSync();
    // Xbox swizzles inside each half-wave; step 32 is ReadLane(31) for upper lanes.
    [unroll] for (uint step = 1; step < min(T, 64); step *= 2) {
        if (tid & step) {
            inclusive += partial[(tid & ~(2*step-1)) | (step-1)];
            partial[tid] = inclusive; // Every source is in the non-writing half.
        }
        GroupMemoryBarrierWithGroupSync();
    }
#if T > 64
    for (uint wave = 0; wave < (tid >> 6); ++wave)
        inclusive += partial[64*wave + 63];
#endif
    uint prefix = inclusive - total;
    [unroll] for (uint j = 0; j < W; ++j) {
        InCounterOutPrefix.Store(addr + 4*j, prefix);
        prefix += x[j];
    }
    if (tid == T-1 && (ShouldOutputTGSum & 1))
        OutThreadGroupSums.Store(4*group.x, inclusive);
}
