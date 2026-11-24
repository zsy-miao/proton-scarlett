// im:CS_BLIT_UI_SDR_TO_BACKBUFFER VS25 / PS29. Registers are in guest space1.
struct V { float4 pos:SV_Position; float4 c:COLOR0; float2 uv:TEXCOORD0; };
V vs(float3 p:POSITION, float4 c:COLOR0, float2 uv:TEXCOORD0) {
    V o = {float4(p,1), c, uv}; return o;
}
Texture2D<float4> ui : register(t257,space1);
Texture2DArray<float> noise : register(t274,space1);
SamplerState pointClamp : register(s0,space1);
cbuffer Display : register(b26,space1) {
    float gainHi : packoffset(c5.x);
    float gainLo : packoffset(c6.z);
    float knee : packoffset(c6.w);
    float pivot : packoffset(c7.z);
    float lumaScale : packoffset(c8.w);
    float contrast : packoffset(c9.y);
}
cbuffer Pass : register(b27,space1) {
    uint useNoise : packoffset(c2.x);
    uint varySlice : packoffset(c2.y);
    uint shapeNoise : packoffset(c2.z);
    float amplitude : packoffset(c2.w);
    float alphaExponent : packoffset(c3.x);
    uint useSRGB : packoffset(c3.y);
    uint frame : packoffset(c5.w);
}
float lm(float x,float y) { return (x==0 || y==0) ? 0 : x*y; }
float power(float x,float p) { return exp2(lm(log2(x),p)); }
float decode(float x) {
    if (!useSRGB) return power(x,2.2);
    return x > .04045 ? power(lm(x+.055,.947867334),2.4) : lm(x,.0773993805);
}
float row(float3 x, float3 w) {
    return mad(x.z,w.z,mad(x.y,w.y,lm(x.x,w.x)));
}
float halfRTZ(float x) {
    uint h=f32tof16(x);
    return f16tof32(h-(abs(f16tof32(h))>abs(x)?1u:0u));
}
float4 ps(V i):SV_Target0 {
    float4 sample0=ui.SampleLevel(pointClamp,i.uv,0);
    float3 x=float3(decode(sample0.r),decode(sample0.g),decode(sample0.b));
    float3 a=float3(row(x,float3(.412390888,.357584298,.180480838)),
                    row(x,float3(.212639064,.715168595,.0721923336)),
                    row(x,float3(.0193308201,.119194724,.950532317)));
    float3 b=float3(row(a,float3(1.01303005,.00610530982,-.0149710001)),
                    row(a,float3(.00769822998,.998165011,-.00503202993)),
                    row(a,float3(-.00284131011,.00468515977,.924507022)));
    float3 c=float3(mad(b.z,-.0000974799987,lm(b.x,1.04981101)),
                    row(b,float3(-.495902956,1.37331295,.0982400328)),
                    mad(b.z,.991252124,lm(b.x,.00000004)));
    float3 rgb=float3(row(c,float3(1.4514395,-.236510813,-.214928553)),
                      row(c,float3(-.0765538812,1.17623007,-.0996759385)),
                      row(c,float3(.00831613038,-.00603245012,.997716248)));
    float y=row(rgb.grb,float3(.674081683,.272228718,.0536895208));
    float t=saturate(lm(y-knee,rcp(1-knee)));
    float gain=mad(lm(lm(t,t),gainHi-gainLo),3-2*t,gainLo);
    float mapped=mad(mad(gain,y,-pivot),rcp(max(1e-6,contrast)),pivot);
    float scale=lm(mapped,rcp(lm(max(1e-6,y),max(1e-6,lumaScale))));
    rgb=float3(lm(scale,rgb.r),lm(scale,rgb.g),lm(scale,rgb.b));
    if (useNoise) {
        uint2 xy=uint2(i.pos.xy)&63;
        float n=mad(noise.Load(int4(xy,varySlice ? frame&31 : 31,0)),2,-1);
        if (shapeNoise) n=lm(sign(n),1-sqrt(1-abs(n)));
        rgb=mad(amplitude,n,rgb);
    }
    return float4(halfRTZ(rgb.r),halfRTZ(rgb.g),halfRTZ(rgb.b),halfRTZ(power(sample0.a,alphaExponent)));
}
