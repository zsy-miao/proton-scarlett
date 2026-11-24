Texture3D<float4> image : register(t0);
RWStructuredBuffer<float> results : register(u0);
[numthreads(1,1,1)] void main() {
    uint w,h,d; image.GetDimensions(w,h,d);
    float4 rgba = (w==1 && h==1 && d==1) ? image.Load(int4(0,0,0,0)) : 0;
    results[0]=rgba.r; results[1]=rgba.g; results[2]=rgba.b; results[3]=rgba.a;
}
