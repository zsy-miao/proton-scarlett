ByteAddressBuffer raw : register(t0);
RWStructuredBuffer<uint> result : register(u0);
[numthreads(64,1,1)] void main(uint i : SV_DispatchThreadID) {
    result[i] = raw.Load(i*4);
    if (!i) { uint size; raw.GetDimensions(size); result[64]=size; }
}
