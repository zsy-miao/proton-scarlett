/* Private x64 backend interface. Hold one reference until the GPU import is
 * destroyed after retirement. No allocation handles or GPU VAs are returned. */
#pragma once
#include <windows.h>

struct XsDisplayBacking {
    void *base;
    SIZE_T size, offset;
};
#ifdef __cplusplus
extern "C" {
#endif
HRESULT WINAPI ScarlettRetainDisplayBackingV1(const void *slice,SIZE_T bytes,struct XsDisplayBacking *out);
/* Pair each successful retain with one release of its returned base. */
void WINAPI ScarlettReleaseDisplayBackingV1(void *base);
#ifdef __cplusplus
}
#endif
