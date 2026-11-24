Texture2D<float> depth : register(t0);
Texture2D<uint2> stencil : register(t1);
SamplerComparisonState cmp : register(s0);
RWStructuredBuffer<uint4> result : register(u0);

[RootSignature("DescriptorTable(SRV(t0,numDescriptors=2)),UAV(u0),StaticSampler(s0,filter=FILTER_COMPARISON_MIN_MAG_MIP_POINT,comparisonFunc=COMPARISON_LESS,addressU=TEXTURE_ADDRESS_CLAMP,addressV=TEXTURE_ADDRESS_CLAMP,addressW=TEXTURE_ADDRESS_CLAMP)")]
[numthreads(4,1,1)]
void main(uint i:SV_GroupIndex) {
    uint2 p=uint2(1020+i,1023);
    float z=depth.Load(int3(p,0)); uint s=stencil.Load(int3(p,0)).g;
    float pass=depth.SampleCmpLevelZero(cmp,(p+.5)/1024,.5);
    result[i]=uint4(asuint(z),s,(uint)pass,pass && s==0xa5 ? 0xcafe : 0x1234);
}
