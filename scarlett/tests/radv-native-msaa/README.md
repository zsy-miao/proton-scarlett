# Native same-owner MSAA alias on RADV

Tested on Raphael/Mendocino (`1002:164e`, RADV 26.3.0). Rebuild the three
shaders and launch a native Linux process with the RADV ICD selected:

```sh
for stage in vs ps cs; do
    dxc -spirv -fspv-target-env=vulkan1.1 -WX -T ${stage}_6_0 -E $stage -Fo $stage.spv draw.hlsl
done
cc -std=c11 -Wall -Wextra -Werror -O2 draw.c -lvulkan -o draw
./draw
RADV_DEBUG=nodcc ./draw  # diagnostic raw-storage comparison, not a runtime setting
```

The buffer and optimal 1×1 4× RGBA8 image are bound to *one unimported,
non-exportable* `VkDeviceMemory` on *one device*. The buffer covers the entire
`0x89700000` bytes. The image starts at `0xe800000` and uses sampled, color
attachment and transfer src/dst usage. Both report type bits `0x7ad`; the
allocation uses type 2 (`HOST_VISIBLE | HOST_COHERENT`). On the default
configuration the image requires `0x22010` bytes, `0x10000` alignment and
no dedicated allocation. A Linux DRM ioctl trace of that exact pool-sized
BO reports domain `0x2` (GTT), flags `0xc5`, `CPU_GTT_USWC=1`,
`UNCACHED=0`; checking the Vulkan flags alone would not establish WC.

A whole-pool `VK_EXT_map_memory_placed` mapping goes at a reserved 2 MiB
aligned address. The fixture fills the slot and both 64 KiB guards, unmaps
through Vulkan and reuses that VA as anonymous memory **before submission**.
It draws four `SV_SampleIndex` colors, samples them through `Texture2DMS`,
and writes the four words into a disjoint prefix of the original buffer.
After the fence, remapping the allocation elsewhere gives `f0800c04`,
`e08c1408`, `d0981c0c`, `c0a42410`. A disjoint GPU buffer marker, both
original guards and the replacement are checked. In the default run the
image changed 73,880 bytes (including metadata). In the `nodcc` diagnostic
all four raw sample words were also found at `+0,+0x4000,+0x8000,+0xc000`;
69,664 bytes changed. Neither raw layout is a portable ABI.

## Limits of the current provider

* An **exportable** allocation would be different. Even with no reimport,
  `VUID-vkBindImageMemory-memory-02728` requires at least one export handle
  type on the image. `VUID-VkMemoryGetFdInfoKHR-handleType-00671` forbids fd
  export unless the allocation requested that type. RADV rejects this 4×
  optimal image with DMA-BUF; opaque fd requires a dedicated image. A
  buffer-only export declaration does not exempt the aliased image. No fd
  is exported by this test.
* Measured with `ID3D12DeviceExt::GetVulkanHandles` at XMem allocation and
  guest `+428` in read-only retail: same process, normally **different**
  instance/device handles. In `xmem/graphics.cpp:validate_import` both the
  original and rebound buffer/heap are temporary. Wine retains an independently
  mapped tagged fd; it does not retain the original Vulkan owner. A
  diagnostic AddRef keeping the XMem device alive made instance, physical
  device and logical device identical at both sites. Both retail runs still
  stopped at `+cc2936`; this control did not substitute a resource.
* A direct mapping is therefore a candidate for a **new owner-backed memory
  contract**, not an fd for the existing one. Wine must own/register the VA,
  call Vulkan's matching unmap exactly once at free/reuse, prevent stale VA
  acquisition, retain the actual device and allocation until GPU retirement,
  and let all buffer consumers use the same owner. The current fd path also
  permits independent simultaneous CPU views. Vulkan permits only one host
  mapping at a time (`VUID-VkMemoryMapInfo-memory-07958`); this fixture tests
  sequential mappings. A pointer or a process-lifetime AddRef does not
  implement those semantics. No production change is included.
