RWTexture2D<float4> outputImage : register(u0);
[numthreads(16,16,1)] void main(uint3 id : SV_DispatchThreadID) { outputImage[id.xy] = float4(0,1,0,1); }
