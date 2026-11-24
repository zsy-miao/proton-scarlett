/* Private native-x64 bridge for the checked translator. Not a guest resource ABI. */
#pragma once
#include <d3d12.h>

struct XsPlacedCapture;
struct XsDataOperand {
    ID3D12Resource *resource; /* borrowed; retain the capture through queue retirement */
    UINT64 offset, bytes;    /* extent-relative, valid only for this range */
};
#ifdef __cplusplus
extern "C" {
#endif
HRESULT WINAPI ScarlettCapturePlacedV1(ID3D12Device3 *, const void *, UINT64 width, struct XsPlacedCapture **);
BOOL WINAPI ScarlettPlacedMatchesV1(const struct XsPlacedCapture *,const void *liveBase);
HRESULT WINAPI ScarlettPlacedDataV1(const struct XsPlacedCapture *, UINT64 offset, UINT64 bytes, struct XsDataOperand *);
/* On success release *retained with ScarlettReleasePlacedV1 after retirement (or list discard). */
HRESULT WINAPI ScarlettPlacedCopyToV1(struct XsPlacedCapture *, UINT64 sourceOffset, UINT64 bytes,
        ID3D12GraphicsCommandList *, ID3D12Resource *destination, UINT64 destinationOffset,
        struct XsPlacedCapture **retained);
void WINAPI ScarlettReleasePlacedV1(struct XsPlacedCapture *);
#ifdef __cplusplus
}
#endif
