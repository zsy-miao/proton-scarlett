/* Private trusted-host compute ABI. Root parameter 0: t0 RAW SRV + u0,u1 RAW UAVs.
 * Caller owns state transitions on the two data resources and must retain the
 * table through fence retirement. Bind only with the corresponding translated PSO. */
#pragma once
#include "placed_capture.h"
struct XsPlacedTable;
#ifdef __cplusplus
extern "C" {
#endif
HRESULT WINAPI ScarlettCreatePlacedTableV1(struct XsPlacedCapture *, struct XsPlacedTable **);
HRESULT WINAPI ScarlettBindPlacedTableV1(const struct XsPlacedTable *, ID3D12GraphicsCommandList *);
void WINAPI ScarlettReleasePlacedTableV1(struct XsPlacedTable *);
#ifdef __cplusplus
}
#endif
