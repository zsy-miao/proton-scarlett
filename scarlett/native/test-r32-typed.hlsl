// Explicit integer mip-zero operations for the three translated guest slots.
#define RS "DescriptorTable(SRV(t0,offset=0),UAV(u0,offset=1),SRV(t1,offset=2)),UAV(u1),RootConstants(num32BitConstants=1,b0)"
Buffer<uint> words : register(t0);
RWBuffer<uint> rewrite : register(u0);
#ifdef TEST_RADV
Buffer<uint> colors : register(t1);
float4 loadRGB(uint k){uint v=colors.Load(k);return float4(float3(v&511,(v>>9)&511,(v>>18)&511)*exp2(int(v>>27)-24),1);}
#else
Buffer<float4> colors : register(t1);
float4 loadRGB(uint k){return colors.Load(k);}
#endif
RWByteAddressBuffer report : register(u1);
cbuffer Phase : register(b0) {uint pass;}
uint tileWord(uint2 p){return ((p.y>>3)*160+(p.x>>3))*64 + ((p.x&3)|((p.y&3)<<2)|((p.x&4)<<2)|((p.y&4)<<3));}
[RootSignature(RS)][numthreads(8,8,1)]
void sample_alias(uint2 p:SV_DispatchThreadID){
    uint k=tileWord(p), i=(pass*921600+p.y*1280+p.x)*20;
    report.Store(i,words.Load(k));report.Store4(i+4,asuint(loadRGB(k)));
}
[RootSignature(RS)][numthreads(8,8,1)]
void write_alias(uint2 p:SV_DispatchThreadID){uint k=tileWord(p);rewrite[k]=rewrite[k]^0xf0e1d2c3;}
