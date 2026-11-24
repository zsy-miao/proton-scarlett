// Synthetic lowering for this one logical byte-address view.
ByteAddressBuffer first : register(t0);
RWByteAddressBuffer middle : register(u0), last : register(u1), report : register(u2);
cbuffer Params : register(b0) { uint phase; uint edge1; uint edge2; uint end; };

uint loadWord(uint p) {
    if ((p & 3) || p >= end) return 0;
    if (p < edge1) return first.Load(p);
    if (p < edge2) return middle.Load(p - edge1);
    return last.Load(p - edge2);
}
bool storeWord(uint p, uint v) {
    if ((p & 3) || p < edge1 || p >= end) return false;
    if (p < edge2) middle.Store(p - edge1, v);
    else last.Store(p - edge2, v);
    return true;
}
bool storePair(uint p, uint2 v) {
    // This fixture rejects a partially forbidden store before touching either lane.
    if ((p & 3) || p < edge1 || p > end - 8) return false;
    storeWord(p, v.x);
    storeWord(p + 4, v.y);
    return true;
}
[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint i = id.x + 32768 * id.y, p = i * 4;
    if (phase == 0) {
        uint value = loadWord(p);
        bool accepted = storeWord(p, ~value);
        report.Store(p, value ^ (accepted ? 0xa0000000u : 0xb0000000u));
    } else if (i == 0) {
        report.Store4(end, uint4(loadWord(edge1-4), loadWord(edge1), loadWord(edge2-4), loadWord(edge2)));
        uint2 pair = uint2(0xcafe0001, 0xcafe0002);
        uint flags = uint(storePair(edge1-4,pair)) | (uint(storePair(edge2-4,pair)) << 1);
        flags |= uint(storePair(end-4,pair)) << 2;
        flags |= uint(storeWord(0xfffffffc,7)) << 3;
        flags |= uint(storeWord(edge1+1,7)) << 4;
        report.Store2(end+16, uint2(flags, loadWord(end) | loadWord(0xfffffffc) | loadWord(edge1+1)));
    }
}
