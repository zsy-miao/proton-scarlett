#define RS "DescriptorTable(SRV(t0,numDescriptors=6), UAV(u0,numDescriptors=5)), UAV(u5), RootConstants(num32BitConstants=3,b0)"
Buffer<float3> allMips : register(t0);
Buffer<float3> slices[5] : register(t1);
RWBuffer<uint> packed[5] : register(u0);
RWByteAddressBuffer result : register(u5);
cbuffer Which : register(b0) {uint mip, baseWord, linearAt;}
uint addr(uint2 p){return ((p.y/8)*(160u>>mip)+p.x/8)*64+(p.x&3)+((p.y&3)<<2)+((p.x&4)<<2)+((p.y&4)<<3);}
[RootSignature(RS)][numthreads(8,8,1)]void write_alias(uint3 p:SV_DispatchThreadID){
 uint w=1280u>>mip;if(p.x>=w||p.y>=(720u>>mip))return;uint i=p.y*w+p.x,k=i+13*mip,a=addr(p.xy);
 uint v=((k&1)?0x3c0:0)|((k&2)?(0x3c0<<11):0)|((k&4)?(0x1e0<<22):0);
 packed[mip][a]=packed[mip][a]==(i^(mip<<24))?v:0xffffffff;
}
[RootSignature(RS)][numthreads(8,8,1)]void sample_alias(uint3 p:SV_DispatchThreadID){
 uint w=1280u>>mip;if(p.x>=w||p.y>=(720u>>mip))return;uint a=addr(p.xy);float3 v=slices[mip][a],full=allMips[baseWord+a];
 result.Store(4*(linearAt+p.y*w+p.x),all(full==v)?(uint)v.x+2*(uint)v.y+4*(uint)v.z:99);
}
