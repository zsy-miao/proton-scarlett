/* Synthetic test window. Run only through input-frame.sh's private Xvfb. */
#include <windows.h>
#include <stdio.h>

static int down, up;
static LRESULT CALLBACK window_proc(HWND w, UINT msg, WPARAM key, LPARAM data)
{
    if (msg == WM_KEYDOWN && key == VK_SPACE) down++;
    if (msg == WM_KEYUP && key == VK_SPACE && down == 1) {
        up++;
        InvalidateRect(w, NULL, FALSE);
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT p;
        HDC dc = BeginPaint(w, &p);
        FillRect(dc, &p.rcPaint, GetStockObject(up ? WHITE_BRUSH : DKGRAY_BRUSH));
        EndPaint(w, &p);
        return 0;
    }
    return DefWindowProcW(w, msg, key, data);
}

static int observe(POINT pt, COLORREF expected)
{
    DWORD start = GetTickCount();
    HDC screen = GetDC(NULL);
    COLORREF pixel;
    do {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        pixel = GetPixel(screen, pt.x, pt.y);
        if (pixel == expected) break;
        Sleep(10);
    } while (GetTickCount() - start < 3000);
    ReleaseDC(NULL, screen);
    printf("screen (%ld,%ld) = %#08lx; SPACE down=%d up=%d\n", pt.x, pt.y, pixel, down, up);
    return pixel == expected;
}

int main(void)
{
    WNDCLASSW cls = {.lpfnWndProc = window_proc, .lpszClassName = L"ScarlettInputProbe",
                     .hInstance = GetModuleHandleW(NULL)};
    POINT pt = {32, 32};
    INPUT keys[2] = {
        {.type = INPUT_KEYBOARD, .ki = {.wVk = VK_SPACE}},
        {.type = INPUT_KEYBOARD, .ki = {.wVk = VK_SPACE, .dwFlags = KEYEVENTF_KEYUP}}
    };
    if (!RegisterClassW(&cls)) return 1;
    HWND w = CreateWindowExW(0, cls.lpszClassName, L"Input fixture", WS_POPUP | WS_VISIBLE,
                              80, 80, 128, 128, NULL, NULL, cls.hInstance, NULL);
    if (!w || !ClientToScreen(w, &pt)) return 2;
    SetForegroundWindow(w);
    SetFocus(w);
    if (!observe(pt, RGB(64,64,64)) || GetForegroundWindow() != w || GetFocus() != w) return 3;
    if (SendInput(2, keys, sizeof(INPUT)) != 2) return 4;
    if (!observe(pt, RGB(255,255,255)) || down != 1 || up != 1) return 5;
    puts("PASS Scarlett private input/frame");
    return 0;
}
