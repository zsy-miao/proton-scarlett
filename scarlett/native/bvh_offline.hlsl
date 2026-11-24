// All addresses are validated byte offsets into the private TLAS UAV.
cbuffer Node : register(b0) {
    uint dst, box, mask, self;
    uint4 child, parentSlot, leafSlot;
    uint4 bounds[6];
};
RWByteAddressBuffer tlas : register(u0);
uint word(uint i) { return bounds[i >> 2][i & 3]; }
[numthreads(1,1,1)] void main() {
    if (mask) tlas.InterlockedOr(76,mask);
    float3 lo=asfloat(0x7f7fffff), hi=-lo;
    [unroll] for (uint k=0;k<4;k++) {
        uint i=6*k;
        uint3 a=uint3(word(i),word(i+1),word(i+2));
        uint3 b=uint3(word(i+3),word(i+4),word(i+5));
        if (child[k]!=0xffffffff) {
            lo=min(lo,asfloat(a)); hi=max(hi,asfloat(b));
            tlas.Store(parentSlot[k],self);
            if (leafSlot[k]!=0xffffffff) tlas.Store(leafSlot[k],self);
        }
        tlas.Store3(dst+16+24*k,a); tlas.Store3(dst+28+24*k,b);
    }
    tlas.Store4(dst,child);
    if (box!=0xffffffff) { tlas.Store3(box,asuint(lo)); tlas.Store3(box+12,asuint(hi)); }
}
