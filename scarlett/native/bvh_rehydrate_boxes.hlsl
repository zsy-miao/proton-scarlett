// 64 compressed node masks, then concatenated child IDs, then bound overrides.
RWByteAddressBuffer src : register(u0), dst : register(u1);
cbuffer Params : register(b0) { uint nodes, parents, childBase, linkTriangles; };

float halfAt(RWByteAddressBuffer buf,uint addr)
{
    return f16tof32(buf.Load(addr & ~3u) >> ((addr & 2)*8));
}
uint outward(float v,bool upper)
{
    bool away=upper ? v>0 : v<0;
    float t=asfloat((asuint(v)+(away ? 8191u : 0u)) & ~8191u);
    float tiny=0.00006103515625;
    if(upper) t=away ? max(t,tiny) : (t>-tiny ? 0 : t);
    else t=away ? min(t,-tiny) : (t<tiny ? 0 : t);
    return f32tof16(t);
}
[numthreads(64,1,1)]
void main(uint3 group:SV_GroupID,uint lane:SV_GroupIndex)
{
    const uint stride=COMPACT ? 64 : 128;
    uint base=nodes+group.x*64*stride, mask=src.Load(base+lane*4);
    uint count=min(mask>>24,4u), changes=mask & 0xffffff;
    uint ids=0,prior=0,overrides=0;
    // The on-disk prefix covers all 64 masks irrespective of native subgroup width.
    [loop] for(uint j=0;j<64;j++){
        uint m=src.Load(base+j*4),n=min(m>>24,4u);ids+=n;
        if(j<lane){prior+=n;overrides+=countbits(m&0xffffff);}
    }
    uint child=base+4*(64+prior), patch=base+4*(64+ids)+(COMPACT?2:4)*overrides;
    uint node=base+stride*lane, id=(node>>6)*8+(COMPACT?4:5);
    [unroll] for(uint i=0;i<4;i++) {
        uint c=i<count ? src.Load(child+4*i) : ~0u, tag=c&7, off=(c>>3)*64;
        dst.Store(node+4*i,c);
        float lo=asfloat(0x7f800000u),hi=-lo;float3 mn=lo,mx=hi;
        if(tag==0 || tag==1) {
            uint n=tag==0 && (dst.Load(off+48)&128)!=0 ? 4 : 3;
            for(uint j=0;j<n;j++){float3 v=asfloat(dst.Load3(off+(tag+j)*12));mn=min(mn,v);mx=max(mx,v);}
        } else if(tag==4 || tag==5) {
            [unroll] for(uint j=0;j<4;j++) {
                uint a=off+16+j*(tag==4?12:24);
                if(tag==4){mn=min(mn,float3(halfAt(dst,a),halfAt(dst,a+2),halfAt(dst,a+4)));
                    mx=max(mx,float3(halfAt(dst,a+6),halfAt(dst,a+8),halfAt(dst,a+10)));}
                else{mn=min(mn,asfloat(dst.Load3(a)));mx=max(mx,asfloat(dst.Load3(a+12)));}
            }
        }
        uint pair=0;
        [unroll] for(uint k=0;k<6;k++) {
            float v=k<3 ? mn[k%3] : mx[k%3];uint value=COMPACT ? outward(v,k>=3) : asuint(v);
            if(changes & (1u<<(6*i+k))) {
                value=src.Load(patch & ~3u);if(COMPACT)value=(value>>((patch&2)*8))&65535;
                patch+=COMPACT?2:4;
            }
            uint elem=6*i+k;
            if(!COMPACT)dst.Store(node+16+elem*4,value);
            else if(k&1)dst.Store(node+16+(elem-1)*2,pair|(value<<16));else pair=value;
        }
        if((tag&6)==4 || (tag==0 && linkTriangles))dst.Store(parents+((off-childBase)>>6)*4,id);
    }
    if(!COMPACT)dst.Store4(node+112,0);
}
