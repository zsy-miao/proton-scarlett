RWByteAddressBuffer original : register(u0);
RWByteAddressBuffer outputWords : register(u1);
[RootSignature("UAV(u0),UAV(u1)")][numthreads(8,8,1)]
void main(uint2 p:SV_DispatchThreadID){
    uint i=p.y*1280+p.x;
    uint at=((p.y>>3)*160+(p.x>>3))*256 + 4*((p.x&3)|((p.y&3)<<2)|((p.x&4)<<2)|((p.y&4)<<3));
    uint word=original.Load(at);outputWords.Store(4*i,word);original.Store(at,word^0xf0e1d2c3);
}
