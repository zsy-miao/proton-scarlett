struct Element { uint words[STRIDE/4]; };
StructuredBuffer<Element> input : register(t0);
RWStructuredBuffer<uint> result : register(u0);
[numthreads(64,1,1)] void main(uint i : SV_DispatchThreadID) {
    if (i < STRIDE/4) {
        result[i]=input[0].words[i];
        result[STRIDE/4+i]=input[1].words[i];
    }
    if (!i) {uint count, stride; input.GetDimensions(count,stride); result[STRIDE/2]=count; result[STRIDE/2+1]=stride;}
}
