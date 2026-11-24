#ifndef ELEMENTS
#define ELEMENTS 1
#endif
struct Element { uint words[STRIDE/4]; };
StructuredBuffer<Element> input : register(t0);
RWStructuredBuffer<uint> result : register(u0);
[numthreads(64,1,1)] void main(uint i : SV_DispatchThreadID) {
    uint count,stride; input.GetDimensions(count,stride);
    const uint words=STRIDE/4;
    if (i < (ELEMENTS+1)*words) result[i]=input[i/words].words[i%words];
    if (!i) {result[(ELEMENTS+1)*words]=count; result[(ELEMENTS+1)*words+1]=stride;}
}
