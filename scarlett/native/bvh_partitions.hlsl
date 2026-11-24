// Each group consumes one validated command and its resolved index snapshot.
cbuffer Partition : register(b0) {
    uint countOffset, listOffset, count, mask;
    uint4 indices[8];
};
RWByteAddressBuffer tlas : register(u0);
[numthreads(32,1,1)] void main(uint lane : SV_GroupIndex) {
    if (!lane) {
        tlas.Store(countOffset,count);
        if (mask) {
            if (count) tlas.InterlockedOr(76,mask);
            else tlas.InterlockedAnd(76,~mask);
        }
    }
    if (lane<count) tlas.Store(listOffset+4*lane,indices[lane>>2][lane&3]);
}
