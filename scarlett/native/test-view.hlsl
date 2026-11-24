ByteAddressBuffer raw : register(t0);
Texture1D<uint> empty : register(t1);
cbuffer constants : register(b0) { uint zero; };
RWBuffer<uint> empty_out : register(u1);
RWByteAddressBuffer output_words : register(u0);
[numthreads(1,1,1)]
void main() {
    empty_out[0] = 0xdeadc0de;
    output_words.Store4(0,uint4(raw.Load(0),empty.Load(int2(0,0)),zero,empty_out[0]));
}
