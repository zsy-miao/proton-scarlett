/* Independent C client using only the Microsoft header and an ordinal import. */
#define COBJMACROS
#include <windows.h>
#include <stdbool.h>
#include <GameInput.h>
#include <assert.h>
#include <stdio.h>

static IGameInputDevice *device;
static void CALLBACK found(GameInputCallbackToken t, void *ctx, IGameInputDevice *d,
                           uint64_t when, GameInputDeviceStatus status, GameInputDeviceStatus prev)
{
    assert(ctx == &device && !device && t && when && !prev && (status & GameInputDeviceConnected));
    IGameInputDevice_AddRef(d);
    device = d;
}
static unsigned state(IGameInputReading *r)
{
    GameInputGamepadState s;
    assert(IGameInputReading_GetGamepadState(r, &s));
    return s.buttons;
}
static IGameInputReading *poll_pad(IGameInput *g)
{
    IGameInputReading *r = NULL;
    assert(IGameInput_GetCurrentReading(g, GameInputKindGamepad, device, &r) == S_OK && r);
    return r;
}
int main(void)
{
    IGameInput *g = NULL;
    GameInputCallbackToken token = 0;
    assert(GameInputCreate(&g) == S_OK && g);
    assert(IGameInput_RegisterDeviceCallback(g, NULL, GameInputKindGamepad, GameInputDeviceConnected,
           GameInputBlockingEnumeration, &device, found, &token) == S_OK && device);
    assert(IGameInput_UnregisterCallback(g, token, 5000));
    assert(!IGameInput_UnregisterCallback(g, token, 5000));
    const GameInputDeviceInfo *info = IGameInputDevice_GetDeviceInfo(device);
    IGameInputDevice *same = NULL;
    assert(IGameInput_FindDeviceFromId(g, &info->deviceId, &same) == S_OK && same == device);
    IGameInputDevice_Release(same);
    IGameInputReading *invalid = (void *)1;
    assert(IGameInput_GetCurrentReading(g, GameInputKindKeyboard, NULL, &invalid)
           == GAMEINPUT_E_READING_NOT_FOUND && !invalid);

    HWND w = CreateWindowW(L"STATIC", L"Original provider probe", WS_POPUP | WS_VISIBLE,
                           40, 40, 160, 100, NULL, NULL, NULL, NULL);
    assert(w);
    SetForegroundWindow(w);
    SetFocus(w);
    assert(GetForegroundWindow() == w);
    IGameInputReading *before = poll_pad(g), *held = NULL;
    assert(state(before) == 0);
    ULONG_PTR xid = (ULONG_PTR)GetPropA(w, "__wine_x11_whole_window");
    assert(xid);
    WCHAR cmd[128];
    swprintf(cmd, 128, L"python3 private-xtest.py space 400 %lu", (unsigned long)xid);
    STARTUPINFOW si = {.cb = sizeof(si)};
    PROCESS_INFORMATION pi;
    assert(CreateProcessW(L"Z:\\usr\\bin\\python3", cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi));
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DWORD start = GetTickCount(), first = 0, last = 0;
    unsigned count = 0;
    bool released = false;
    while (GetTickCount() - start < 5000) {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        IGameInputReading *r = poll_pad(g);
        unsigned b = state(r);
        assert(!b || b == GameInputGamepadA);
        if (b) {
            if (!count++) {
                first = GetTickCount();
                held = r;
                IGameInputReading_AddRef(r);
            }
            last = GetTickCount();
        } else if (held) released = true;
        IGameInputReading_Release(r);
        if (released) break;
        Sleep(16);
    }
    assert(released && last - first >= 100 && state(before) == 0 && state(held) == GameInputGamepadA);
    printf("PASS ordinal 1: before=0 held=0x4 (%u samples, %lu ms) after=0; snapshots retained\n",
           count, last - first);
    IGameInputReading_Release(held);
    IGameInputReading_Release(before);
    INPUT keys[2] = {
        {.type = INPUT_KEYBOARD, .ki = {.wVk = 'W'}},
        {.type = INPUT_KEYBOARD, .ki = {.wVk = VK_LEFT}}
    };
    for (int i = 0; i < 2; ++i) {
        assert(SendInput(2, keys, sizeof(INPUT)) == 2);
        GameInputGamepadState pad;
        DWORD limit = GetTickCount();
        do {
            IGameInputReading *r = poll_pad(g);
            assert(IGameInputReading_GetGamepadState(r, &pad));
            IGameInputReading_Release(r);
            if (pad.leftThumbstickY == (i ? 0 : 1) && pad.rightThumbstickX == (i ? 0 : -1)) break;
            assert(GetTickCount() - limit < 2000);
            Sleep(10);
        } while (true);
        assert(!pad.leftThumbstickX && !pad.rightThumbstickY && !pad.buttons);
        keys[0].ki.dwFlags = keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    puts("PASS virtual sticks: forward + look left, then neutral");
    assert(info->gamepadInfo->bButtonLabel==GameInputLabelXboxB &&
           info->gamepadInfo->menuButtonLabel==GameInputLabelXboxMenu);
    const WORD codes[]={VK_BACK,VK_RETURN,VK_BACK,VK_RETURN};
    const unsigned expected[]={GameInputGamepadB,GameInputGamepadB|GameInputGamepadMenu,GameInputGamepadMenu,0};
    for (unsigned i=0;i<ARRAYSIZE(codes);i++) {
        INPUT key={.type=INPUT_KEYBOARD,.ki={.wVk=codes[i],.dwFlags=i>=2?KEYEVENTF_KEYUP:0}};
        assert(SendInput(1,&key,sizeof(key))==1);
        DWORD limit=GetTickCount();
        for (;;) {
            IGameInputReading *r=poll_pad(g);
            unsigned b=state(r);
            IGameInputReading_Release(r);
            if (b==expected[i]) break;
            assert(GetTickCount()-limit<2000);
            Sleep(10);
        }
    }
    puts("PASS B and Menu: individually held, simultaneous press, independent release to neutral");
    IGameInputDevice_Release(device);
    IGameInput_Release(g);
    return 0;
}
