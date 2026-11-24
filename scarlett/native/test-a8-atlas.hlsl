Texture2D<float4> atlas : register(t0);
RWStructuredBuffer<uint> result : register(u0);
[RootSignature("DescriptorTable(SRV(t0)),UAV(u0)")][numthreads(8,8,1)]
void main(uint3 p:SV_DispatchThreadID){uint w,h;atlas.GetDimensions(w,h);uint4 c=round(atlas.Load(int3(p.xy,0))*255);result[p.y*w+p.x]=c.x|(c.y<<8)|(c.z<<16)|(c.w<<24);}
