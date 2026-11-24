RWByteAddressBuffer wc : register(u0), wb : register(u1), report : register(u2);
cbuffer Params : register(b0) { uint phase; uint salt; };
static const uint A=27u<<20, B=74u<<20, E=174u<<20;
bool valid(uint p) { return p>=A && p<E && (p&3)==0; }
uint load(uint p) {
    if (!valid(p)) return 0;
    if (p<B) return wc.Load(p-A);
    return wb.Load(p-B);
}
bool store(uint p,uint value) {
    if (!valid(p)) return false;
    if (p<B) wc.Store(p-A,value); else wb.Store(p-B,value);
    return true;
}
[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint p=A+4*(id.x+32768*id.y);
    if (phase==0) store(p,load(p)^salt);
    else if (id.x==0 && id.y==0) {
        report.Store4(0,uint4(load(A),load(B-4),load(B),load(E-4)));
        uint flags=uint(store(A-4,7)) | uint(store(B-4,0xcafe0001))<<1 |
                   uint(store(B,0xcafe0002))<<2 | uint(store(E,7))<<3 |
                   uint(store(0xfffffffc,7))<<4 | uint(store(A+1,7))<<5;
        report.Store(16,flags);
    }
}
