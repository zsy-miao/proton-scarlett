StructuredBuffer<uint> geometry : register(t2,space1);
Texture2D<float> y0 : register(t6,space1), u0 : register(t5,space1), v0 : register(t7,space1);
#ifdef TEST_ALPHA
Texture2D<float> alphaPlane : register(t8,space1);
#endif
Texture3D<float> volume : register(t10,space1);
cbuffer A : register(b0,space1) {uint a;}; cbuffer B : register(b3,space1) {uint b;};
SamplerState linearClamp : register(s3,space1);
struct V {float4 p:SV_Position; float2 uv:TEXCOORD0; nointerpolation uint sum:COLOR0;};
V vs(float3 p:POSITION,float2 uv:TEXCOORD0,float4 color:COLOR0){V o;o.p=float4(p,1);o.uv=uv;o.sum=geometry[0]+3*a+5*b+(uint)color.x;return o;}
uint ps(V i):SV_Target {float2 uv=float2(i.uv.x*4-1,i.uv.y);uint alpha=0;
#ifdef TEST_ALPHA
    alpha=13*(uint)round(alphaPlane.SampleLevel(linearClamp,uv,0));
#endif
    return alpha+i.sum+7*b+(uint)round(y0.SampleLevel(linearClamp,uv,0)+3*u0.SampleLevel(linearClamp,uv,0)+7*v0.SampleLevel(linearClamp,uv,0)+11*volume.SampleLevel(linearClamp,float3(uv,.5),0));}
