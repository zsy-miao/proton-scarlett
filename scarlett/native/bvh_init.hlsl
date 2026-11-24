// Host translation of the single-wave initializer; b0 is a validated snapshot.
cbuffer Init : register(b0) {
    uint flags, elements, nodesOffset, elementsOffset;
    uint parentsOffset, count, perPartition, globalCount;
};
RWByteAddressBuffer header : register(u0);
[numthreads(1,1,1)] void main() {
    header.Store4(0, uint4(flags,0,0,elements));
    header.Store4(16,uint4(0x7f7fffff,0x7f7fffff,0x7f7fffff,0xff7fffff));
    header.Store4(32,uint4(0xff7fffff,0xff7fffff,nodesOffset,0));
    header.Store4(48,uint4(0xffffffff,elementsOffset,parentsOffset,0x007f7f7f));
    header.Store4(64,uint4(count,perPartition,globalCount,0));
    for (uint i=0;i<count;i++) header.Store(80+28*i,0);
}
