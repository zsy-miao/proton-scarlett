Texture2D<float4> t:register(t0);RWStructuredBuffer<uint> o:register(u0);
[RootSignature("DescriptorTable(SRV(t0)),UAV(u0)")][numthreads(2,1,1)]void main(uint i:SV_GroupIndex){uint4 c=(uint4)round(255*t.Load(int3(2046+i,2047,0)));o[i]=c.x|(c.y<<8)|(c.z<<16)|(c.w<<24);}
