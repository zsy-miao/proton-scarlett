/* Fixture DLL only: link with -Wl,--wrap=xmem_wc_slot; build its EXE with SCATTER_TEST_ROLLBACK. */
#include <windows.h>
BOOL __real_xmem_wc_slot(void *,SIZE_T,HANDLE,SIZE_T,DWORD);
BOOL __wrap_xmem_wc_slot(void *at,SIZE_T bytes,HANDLE section,SIZE_T offset,DWORD policy)
{
    static BOOL failed_bind,failed_unbind;
    if ((section && bytes==4136u*65536 && !failed_bind) ||
        (!section && bytes==6720u*65536 && !failed_unbind)) {
        if (section) failed_bind=TRUE; else failed_unbind=TRUE;
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }
    return __real_xmem_wc_slot(at,bytes,section,offset,policy);
}
