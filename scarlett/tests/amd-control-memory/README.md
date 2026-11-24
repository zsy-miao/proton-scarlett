# 64 KiB CPU WC + GPU UC backing investigation

Synthetic only, unprivileged, Linux `7.3.4-cachyos`, NVIDIA RTX 3070 /
595.112.07 and AMD 1002:164e (RADV RAPHAEL_MENDOCINO, GC 10.3.6).

## AMD same-adapter path with evidence across import

`gem-uc.c` uses the public amdgpu DRM render-node UAPI. It creates a 65536-byte
GTT GEM object with `AMDGPU_GEM_CREATE_CPU_GTT_USWC` and
`AMDGPU_GEM_CREATE_UNCACHED`, PRIME-exports it, closes the DRM file, and
initializes the first word via the dma-buf CPU mapping. A fresh process
image imports that fd on RADV with `VK_AMD_device_coherent_memory` enabled
and `deviceCoherentMemory=VK_TRUE`, selecting a compatible type with flags
`0xc6` (host visible/coherent; device coherent/uncached). GPU copies the
first word to the last word. The independent public CPU mapping observes it
after queue completion. DMA-BUF CPU access is bracketed by `DMA_BUF_IOCTL_SYNC`.

`gem.log`: the kernel's `GET_GEM_CREATE_INFO` returned `0x420c` on export and
`0x420d` on fresh import. Both include **USWC and UNCACHED**. GPU VA map
succeeded with default MTYPE (flags `0xe`). This is significant: on GFX10
`gmc_v10_0_get_vm_pte()` forces MTYPE_UC for a BO flagged UNCACHED even if
the mapping requested default. `amdgpu_ttm_tt_create()` selects
`ttm_write_combined` for a GTT BO flagged CPU_GTT_USWC. This is driver
allocation/query and GPU mapping evidence, not hardware PTE or PAT readout.
References: [amdgpu UAPI](https://github.com/torvalds/linux/blob/v7.0/include/uapi/drm/amdgpu_drm.h),
[GFX10 PTE choice](https://github.com/torvalds/linux/blob/v7.0/drivers/gpu/drm/amd/amdgpu/gmc_v10_0.c#L477-L522),
[TTM CPU cache choice](https://github.com/torvalds/linux/blob/v7.0/drivers/gpu/drm/amd/amdgpu/amdgpu_ttm.c),
[Vulkan device-uncached semantics](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryPropertyFlagBits.html).

Control (`fresh.log`): allocation directly via RADV type 8 gives CPU USWC
and explicit GPU VA MTYPE_UC (`0x80`) initially, but no persistent UNCACHED
BO flag. After release and fresh VM import, even selecting type 8 again,
VA map uses default MTYPE, without the persistent BO flag. Both paths pass
the copy test. Only the GEM-backed path has affirmative policy evidence on
both sides of a fresh import. Do not assume an exported fd carries the
exporter's GPU page attributes. vkd3d-proton at the pin already uses the AMD
device-coherent extension for its internal breadcrumbs/debug buffers; those
buffers are not an application-facing heap factory.

```sh
cc -O2 -Wall -Wextra amd-uc.c -lvulkan -o amd-uc
cc -O2 -Wall -Wextra -I"$DRM_INCLUDE" gem-uc.c -o gem-uc
cc -O2 -shared -fPIC -I"$DRM_INCLUDE" amd-trace.c -ldl -o amd-trace.so
VK_DRIVER_FILES="$RADV_ICD" LD_PRELOAD="$PWD/amd-trace.so" \
    ./gem-uc /dev/dri/renderD129 ./amd-uc

```
