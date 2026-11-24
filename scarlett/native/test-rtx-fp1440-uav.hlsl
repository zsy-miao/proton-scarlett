RWTexture2D<float4> image:register(u0);
[RootSignature("DescriptorTable(UAV(u0))")][numthreads(1,1,1)]void main(){image[uint2(2559,1439)]=float4(0,1,1,0.5);}
