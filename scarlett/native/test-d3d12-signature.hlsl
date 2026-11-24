RWByteAddressBuffer args : register(u0);
RWByteAddressBuffer counter : register(u1);
[numthreads(1,1,1)] void produce() { args.Store3(12, uint3(3,2,1)); counter.Store(0, 0); }
[numthreads(4,1,1)] void consume() { uint old; counter.InterlockedAdd(0, 1, old); }
