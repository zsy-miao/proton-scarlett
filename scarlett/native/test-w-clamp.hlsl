Texture3D<float> image : register(t0);
SamplerState linearWrap : register(s0);
SamplerState pointWrap : register(s1);
SamplerState pointClampW : register(s2);
SamplerState linearClampW : register(s3);
SamplerState pointClamp : register(s4);
SamplerState linearClamp : register(s5);
SamplerState pointMirrorUV : register(s6);
SamplerState linearMirrorUV : register(s7);
RWStructuredBuffer<float4> samples : register(u0);

[RootSignature("DescriptorTable(SRV(t0)),DescriptorTable(Sampler(s0,numDescriptors=8)),UAV(u0)")]
[numthreads(4,1,1)]
void main(uint i : SV_GroupIndex) {
    // Outside W at both ends; then outside U/V at a non-center position to test point filtering.
    float3 uvw = i==0 ? float3(.25,.25,2.375) : i==1 ? float3(.75,.75,-.25)
               : i==2 ? float3(1.375,-.375,.375) : float3(2.25,-1.25,2.25);
    samples[2*i]=float4(image.SampleLevel(pointWrap,uvw,0),image.SampleLevel(pointClampW,uvw,0),
                      image.SampleLevel(linearWrap,uvw,0),image.SampleLevel(linearClampW,uvw,0));
    samples[2*i+1]=float4(image.SampleLevel(pointClamp,uvw,0),image.SampleLevel(linearClamp,uvw,0),
                          image.SampleLevel(pointMirrorUV,uvw,0),image.SampleLevel(linearMirrorUV,uvw,0));
}
