/* Console GameInput v0 ABI: GetCurrentReading #4, GetGamepadState #22. */
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct { void **vt; } Object;
typedef struct { UINT32 buttons; float axes[6]; } State;
__declspec(dllimport) HRESULT WINAPI GameInputCreate(Object **);

static int buttons(Object *input, UINT32 *out)
{
    Object *r = NULL;
    State s = {0};
    HRESULT (WINAPI *read)(Object *, UINT32, void *, Object **) = input->vt[4];
    if (FAILED(read(input, 0x40000, NULL, &r)) || !r) return 0;
    bool (WINAPI *gamepad)(Object *, State *) = r->vt[22];
    ULONG (WINAPI *release)(Object *) = r->vt[2];
    bool valid = gamepad(r, &s);
    release(r);
    *out = s.buttons;
    return valid;
}

int main(void)
{
    HWND w = CreateWindowExW(0, L"STATIC", L"GameInput probe", WS_POPUP | WS_VISIBLE,
                             80, 80, 128, 128, NULL, NULL, GetModuleHandleW(NULL), NULL);
    Object *input = NULL;
    UINT32 b;
    if (!w || FAILED(GameInputCreate(&input)) || !input) return 1;
    SetForegroundWindow(w);
    SetFocus(w);
    if (GetForegroundWindow() != w || GetFocus() != w || !buttons(input, &b) || b) return 2;
    printf("GameInput before: buttons=%#lx\n", (unsigned long)b);
    fflush(stdout);
    STARTUPINFOW si = {.cb = sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    HANDLE xid = GetPropA(w, "__wine_x11_whole_window");
    if (!xid) return 3;
    WCHAR cmd[160];
    swprintf(cmd, 160, L"python3 /game/private-xtest.py space 400 %lu", (unsigned long)(UINT_PTR)xid);
    if (!CreateProcessW(L"Z:\\usr\\bin\\python3", cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("cannot launch private injector: %lu\n", GetLastError());
        return 3;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DWORD start = GetTickCount(), first = 0, last = 0;
    unsigned samples = 0;
    while (GetTickCount() - start < 5000) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        if (GetForegroundWindow() != w || GetFocus() != w || !buttons(input, &b)) return 4;
        DWORD elapsed = GetTickCount() - start;
        if (b == 0x4) {
            if (!samples++) first = elapsed;
            last = elapsed;
        } else if (b) return 5;
        else if (samples) {
            printf("GameInput held: buttons=0x4, %u samples, %lu..%lu ms\n", samples, first, last);
            printf("GameInput after: buttons=%#lx at %lu ms\n", (unsigned long)b, elapsed);
            if (last - first < 100) return 6;
            puts("PASS XCL GameInput Space -> A -> released");
            return 0;
        }
        Sleep(16);
    }
    return 7;
}
