Texture3D<float> image : register(t0);
SamplerState linearWrap : register(s0);
SamplerState pointWrap : register(s1);
SamplerState pointClampW : register(s2);
SamplerState linearClampW : register(s3);
RWStructuredBuffer<float4> samples : register(u0);

[RootSignature("DescriptorTable(SRV(t0)),DescriptorTable(Sampler(s0,numDescriptors=4)),UAV(u0)")]
[numthreads(3,1,1)]
void main(uint i : SV_GroupIndex) {
    // Outside W at both ends; then outside U/V at a non-center position to test point filtering.
    float3 uvw = i==0 ? float3(.25,.25,2.375) : i==1 ? float3(.75,.75,-.25) : float3(1.375,-.375,.375);
    samples[i]=float4(image.SampleLevel(pointWrap,uvw,0),image.SampleLevel(pointClampW,uvw,0),
                      image.SampleLevel(linearWrap,uvw,0),image.SampleLevel(linearClampW,uvw,0));
}
