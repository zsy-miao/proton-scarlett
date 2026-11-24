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
HRESULT WINAPI ScarlettPlacedDataV1(const struct XsPlacedCapture *, UINT64 offset, UINT64 bytes, struct XsDataOperand *);
void WINAPI ScarlettReleasePlacedV1(struct XsPlacedCapture *);
#ifdef __cplusplus
}
#endif
