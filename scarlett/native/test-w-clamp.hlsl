Texture3D<float4> image : register(t0);
SamplerState linearWrap : register(s0);
SamplerState pointWrap : register(s1);
SamplerState pointClampW : register(s2);
SamplerState linearClampW : register(s3);
SamplerState pointClamp : register(s4);
SamplerState linearClamp : register(s5);
SamplerState pointMirrorUV : register(s6);
SamplerState linearMirrorUV : register(s7);
SamplerState linearMirror : register(s8);
SamplerState black : register(s9);
SamplerState white : register(s10);
RWStructuredBuffer<float4> samples : register(u0);

[RootSignature("DescriptorTable(SRV(t0)),DescriptorTable(Sampler(s0,numDescriptors=11)),UAV(u0)")]
[numthreads(4,1,1)]
void main(uint i : SV_GroupIndex) {
    // Outside W at both ends; then outside U/V at a non-center position to test point filtering.
    float3 uvw = i==0 ? float3(.25,.25,2.375) : i==1 ? float3(.75,.75,-.25)
               : i==2 ? float3(1.375,-.375,.375) : float3(2.25,-1.25,2.25);
    samples[2*i]=float4(image.SampleLevel(pointWrap,uvw,0).r,image.SampleLevel(pointClampW,uvw,0).r,
                      image.SampleLevel(linearWrap,uvw,0).r,image.SampleLevel(linearClampW,uvw,0).r);
    samples[2*i+1]=float4(image.SampleLevel(pointClamp,uvw,0).r,image.SampleLevel(linearClamp,uvw,0).r,
                          image.SampleLevel(pointMirrorUV,uvw,0).r,image.SampleLevel(linearMirrorUV,uvw,0).r);
    if(i==0){
        samples[8]=image.SampleLevel(linearMirror,float3(-.5,.25,1.25),0);
        samples[9]=image.SampleLevel(black,float3(.5,.5,2),0);
        samples[10]=image.SampleLevel(white,float3(-1,.5,.5),0);
    }
}
