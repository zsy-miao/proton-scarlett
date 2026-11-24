RWByteAddressBuffer result : register(u0);
[numthreads(1,1,1)] void first()  { result.Store(0, 0xc0de8404); }
[numthreads(1,1,1)] void second() { result.Store(4, 0x8404beef); }
