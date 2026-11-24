# XDXR SplitInstancesPass

## Identity and ABI

* Verified local builder SHA-256 `99f803ac22c01ef329dddb1d40be67238d2a812d01e8d040b3cb5135bc23707e`. Root creation site `+dbfd -> +c2d0`, shader creation `+dc43 -> +c470`, wrapper member `+158` (root) and `+160` (pipeline), shared UTF-16 literal `+cdb68`. DXBC RVA `+a6400`, file offset `+a5800`, bytes `0x88f4`, SHA-256 `5c3d3c4a41f320fb7350d3bf23849550786fb89b8709d62512f73eadf2088eee`, vkd3d FNV `520024d8d479f209`.
* Exact v1.1 guest signature: four root UAVs `u0..3`, then 8 DWORDs b0, all space 0, flags `0x03000000`, length 188. Separate root kind/name. Root and pipeline validate full signature bytes, device ownership, full shader digest, size, tail, and expected name in their respective entry points. Private host signature: table of four **bounded** UAVs; b0[8]; private constants[6], root SRV and root UAV in space `0x5358`. Six DWORDs = read base lo/hi/bytes, atomic base lo/hi/bytes. The SRV and UAV point at authorized real host suballocations, never at raw guest addresses.

## What the compiled shader reads and changes

* Group (32,1,1). b0[0..1] = vaHeader; [2..3] descriptor/pointer-array VA, **unused here**; [4] split count; [5] split-into-16 count; [6] allocation capacity; [7] flags, unused here. X must be below both the split count and `u1[0]` (NumElements). `u3[X]` is the input element index. Neither u1 nor u3 is written.
* u0 = raw 128-byte instance records. Record +56 contains tagged node ID (retain high two bits for output); +60 is the **compressed BLAS base**: zero-extend DWORD and shift left 8. This is *not* a full descriptor pointer, and is unrelated to b0[2..3]. Node VA = BLAS + 64*(untagged ID >> 3). Cached x4 reads fetch four child IDs. For eligible internal box lists, first-level count is the number of positive children; a positive child with `(id & 6) != 4` prevents that split. X below [5] can also count eligible grandchildren. Take the larger count, exit for <2. For box tag 5, each bounds pair is two x3 reads starting at node+16+24*k; compact boxes use one x3 at node+16+12*k (six halves). Record +120 metadata controls optional scale/rotation and BLAS header x3 reads at +16/+28 for the center correction. It is not an atomic target.
* One active lane per wave reserves `sum(n-1)` at **b0.vaHeader + 12** using fetch-add(355); unsigned `old+sum > cap` triggers fetch-sub(367) of the same amount and suppresses all emission. Success uses broadcast old + exclusive prefix of (n-1). There are no float atomics here. The first output replaces the indexed source slot; the others append. One-level code uses the child *ordinal* for the slot, whereas the two-level loop increments its emitted-grandchild count. Dense child prefixes matter. Output u0 copies the saved instance fields, patches tagged child ID, zeros +116, preserves +120, does not write +124. u2 = transformed min/max xyz, 24-byte stride. Med3_F32 clamps the transformed box and compact unpacked coordinates.
* Implementation reuses LoadInstancesPass option 57 and its range/atomic helpers unchanged. Reads require the whole aligned 12 or 16 bytes; otherwise zero. Counter requires 4 bytes; invalid target is suppressed and returns `~amount` to force rejection for finite cap < UINT_MAX. Split-specific compiler changes are only the exact shader selector and a distinct marker. No new intrinsic or shader port.

## Fenced coverage

Each case initializes six guarded buffers, dispatches two workgroups, fences, reads back **every word including guards, inputs and untouched tails**, compares to a CPU oracle. Permuted input indices (2,0); reflected, scaled and translated boxes; nonzero old count 4; both BLAS and header arithmetic cross the low-32-bit carry. Poison descriptor-array VA and flag constants.

| Case | Check |
|---|---|
|0|Two lanes, two FP32 children each; indexed replacements plus contiguous appends|
|1|Four grandchildren from lane 0 and two children from lane 1; mixed reservation and prefix offsets|
|2|Capacity 5 fails for reservation end 6; wave rollback, no output|
|3|Atomic extent 15 ends one byte before full DWORD; no writes, no false slot zero|
|4|BLAS x4 child table short by one byte; no split|
|5|Second child’s x3 max short by one byte; all three components zero, min retained|
|6|Compact six-half boxes|
|7|Header min/max center correction (nonzero on two axes, straddling zero on third)|
|8,9,10|Unsplittable leaf, zero `u1` count, split limit one|
|11|Shortened **u2 view only**, guarded backing still physically present; appends update u0/counter but do not escape u2 view|

Exact guest root without private windows fails host compilation (diagnostics in logs are expected); LoadPass-only core fails factory marker check. Additional negatives include bad schema with same byte length, byte mutation, wrong root, device, tail, size, digest and name.

**Dispatch boundary:** No guest recorder qualified. Establish view capacities, live owners/permissions and contiguous extents; unique in-range source indices disjoint from appended slots; packed child prefixes and 32-bit raw offsets; no counter wrap with outstanding reservations; cap < UINT_MAX. A bounded partial output is useful for checking confinement but not a valid completed BVH. Disjoint BLAS allocations cannot be represented by one over-wide read window. Multi-wave contention, arbitrary rotation/scale metadata and exceptional float behavior were not qualified.
