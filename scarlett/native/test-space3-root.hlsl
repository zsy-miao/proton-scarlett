ByteAddressBuffer input : register(t0, space3);
float4 vs(uint id:SV_VertexID):SV_Position {
    return float4(id==1 ? 3 : -1, id==2 ? 3 : -1, 0, 1);
}
uint ps(float4 p:SV_Position):SV_Target { return input.Load(4*(uint)p.x); }
