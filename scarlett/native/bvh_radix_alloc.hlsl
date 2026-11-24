// Exact RadixAlloc port: stable per-digit rank across 2,048 pairs in a tile.
RWByteAddressBuffer Keys : register(u0), Payload : register(u1);
RWByteAddressBuffer L1 : register(u2), L2 : register(u3);
RWByteAddressBuffer SortedKeys : register(u4), SortedPayload : register(u5);
cbuffer Params : register(b0) { uint Stride, Shift, BlockLog; }

groupshared uint digit[2048], count[16][256];

[numthreads(512,1,1)]
void main(uint3 group : SV_GroupID, uint tid : SV_GroupIndex)
{
    uint keyBytes, payloadBytes, l1Bytes, l2Bytes, outK, outP;
    Keys.GetDimensions(keyBytes); Payload.GetDimensions(payloadBytes);
    L1.GetDimensions(l1Bytes); L2.GetDimensions(l2Bytes);
    SortedKeys.GetDimensions(outK); SortedPayload.GetDimensions(outP);
    uint g = group.x, capacity = min(outK, outP) / 4;
    if (group.y || group.z || g >= Stride || Shift > 24 ||
        g >= min(keyBytes, payloadBytes) / 8192) return;

    uint keys[4], ids[4];
    for (uint i = tid; i < 4096; i += 512) count[i >> 8][i & 255] = 0;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint j = 0; j < 4; ++j) {
        uint i = tid + j * 512, addr = (g * 2048 + i) * 4;
        keys[j] = Keys.Load(addr); ids[j] = Payload.Load(addr);
        uint d = (keys[j] >> Shift) & 255;
        digit[i] = d;
        InterlockedAdd(count[i >> 7][d], 1);
    }
    GroupMemoryBarrierWithGroupSync();
    if (tid < 256) {
        uint sum = 0;
        for (uint block = 0; block < 16; ++block) {
            uint n = count[block][tid]; count[block][tid] = sum; sum += n;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint j = 0; j < 4; ++j) {
        uint i = tid + j * 512, d = digit[i], rank = count[i >> 7][d];
        if (d > (0xffffffffu - g) / Stride) continue;
        uint cell = d * Stride + g, block = cell >> (BlockLog & 31);
        if (cell >= l1Bytes / 4 || block >= l2Bytes / 4) continue;
        for (uint k = i & ~127u; k < i; ++k) rank += digit[k] == d;
        uint a = L1.Load(cell * 4), b = L2.Load(block * 4);
        if (a >= capacity || b >= capacity - a || rank >= capacity - a - b) continue;
        uint dst = (a + b + rank) * 4;
        SortedKeys.Store(dst, keys[j]); SortedPayload.Store(dst, ids[j]);
    }
}
