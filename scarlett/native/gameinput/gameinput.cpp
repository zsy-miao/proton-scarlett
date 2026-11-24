// x64, Windows SDK v0 GameInput ABI. One foreground-keyboard virtual gamepad.
#define CINTERFACE
#include <windows.h>
#include <GameInput.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static_assert(sizeof(void *) == 8 && sizeof(GameInputGamepadState) == 28);
static_assert(offsetof(IGameInputVtbl, GetCurrentReading) == 0x20);
static_assert(offsetof(IGameInputVtbl, RegisterDeviceCallback) == 0x48);
static_assert(offsetof(IGameInputVtbl, FindDeviceFromId) == 0x80);
static_assert(offsetof(IGameInputReadingVtbl, GetGamepadState) == 0xb0);
static_assert(offsetof(GameInputDeviceInfo, deviceId) == 0x20 && sizeof(APP_LOCAL_DEVICE_ID) == 32);

static const GUID input_iid = {0x11be2a7e,0x4254,0x445a,{0x9c,0x09,0xff,0xc4,0x0f,0x00,0x69,0x18}};
static const GUID reading_iid = {0x2156947a,0xe1fa,0x4de0,{0xa3,0x0b,0xd8,0x12,0x93,0x1d,0xbd,0x8d}};
static const GUID device_iid = {0x31dd86fb,0x4c1b,0x408a,{0x86,0x8f,0x43,0x9b,0x3c,0xd4,0x71,0x25}};

template<class T> static HRESULT query(T *self, REFIID iid, void **out, REFIID supported)
{
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid != IID_IUnknown && iid != supported) return E_NOINTERFACE;
    self->lpVtbl->AddRef(self);
    *out = self;
    return S_OK;
}

// Full SDK-declared tables, including typed failures for unsupported features.
static constexpr auto noop = [](auto...) {};
static constexpr auto zero = [](auto...) -> uint32_t { return 0; };
static constexpr auto absent = [](auto...) { return false; };
static constexpr auto unsupported = [](auto...) { return E_NOTIMPL; };
template<class T> static HRESULT fail_out(T *out, HRESULT error = E_NOTIMPL)
{
    if (!out) return E_POINTER;
    *out = {};
    return error;
}
static uint64_t now()
{
    LARGE_INTEGER q, f;
    QueryPerformanceCounter(&q);
    QueryPerformanceFrequency(&f);
    return q.QuadPart / f.QuadPart * 1000000 + q.QuadPart % f.QuadPart * 1000000 / f.QuadPart;
}

static const GameInputGamepadInfo pad_info = [] {
    GameInputGamepadInfo p{};
    p.aButtonLabel = GameInputLabelXboxA;
    return p;
}();
static const GameInputDeviceInfo info = [] {
    GameInputDeviceInfo i{};
    i.infoSize = sizeof(i);
    memcpy(&i.deviceId, "scarlett.virtual.pad.1", 22);
    i.deviceRootId = i.deviceId;
    i.deviceFamily = GameInputFamilyVirtual;
    i.supportedInput = GameInputKindGamepad;
    i.gamepadInfo = &pad_info;
    return i;
}();
// Process-lifetime input/device singletons each retain one internal reference.
static LONG dev_refs = 1, input_refs = 1;
static IGameInputDeviceVtbl dev_vt = {
    .QueryInterface = [](IGameInputDevice *s, REFIID i, void **o){return query(s,i,o,device_iid);},
    .AddRef = [](IGameInputDevice *) -> ULONG {return InterlockedIncrement(&dev_refs);},
    .Release = [](IGameInputDevice *) -> ULONG {return InterlockedDecrement(&dev_refs);},
    .GetDeviceInfo = [](IGameInputDevice *) {return &info;},
    .GetDeviceStatus = [](IGameInputDevice *) {return GameInputDeviceConnected | GameInputDeviceInputEnabled;},
    .GetBatteryState = [](auto *, auto *s){if (s) *s = {};},
    .CreateForceFeedbackEffect = [](auto *, auto, auto *, auto **o){return fail_out(o);},
    .IsForceFeedbackMotorPoweredOn = absent, .SetForceFeedbackMotorGain = noop,
    .SetHapticMotorState = unsupported, .SetRumbleState = noop,
    .SetInputSynchronizationState = noop, .SendInputSynchronizationHint = noop, .PowerOff = noop,
    .CreateRawDeviceReport = [](auto *, auto, auto, auto **o){return fail_out(o);},
    .GetRawDeviceFeature = [](auto *, auto, auto **o){return fail_out(o);},
    .SetRawDeviceFeature = unsupported, .SendRawDeviceOutput = unsupported,
    .SendRawDeviceOutputWithResponse = [](auto *, auto *, auto **o){return fail_out(o);},
    .ExecuteRawDeviceIoControl = [](auto *, auto, auto, auto *, auto, auto *, auto *n){
        if (n) *n = 0;
        return E_NOTIMPL;
    },
    .AcquireExclusiveRawDeviceAccess = absent, .ReleaseExclusiveRawDeviceAccess = noop
};
static IGameInputDevice device{&dev_vt};

struct Reading : IGameInputReading {
    LONG refs;
    uint64_t timestamp, sequence;
    GameInputGamepadState state;
};
static IGameInputReadingVtbl read_vt = {
    .QueryInterface = [](IGameInputReading *s, REFIID i, void **o){return query(s,i,o,reading_iid);},
    .AddRef = [](IGameInputReading *s) -> ULONG {return InterlockedIncrement(&static_cast<Reading *>(s)->refs);},
    .Release = [](IGameInputReading *s) -> ULONG {
        auto *r = static_cast<Reading *>(s);
        LONG n = InterlockedDecrement(&r->refs);
        if (!n) free(r);
        return n;
    },
    .GetInputKind = [](IGameInputReading *) {return GameInputKindGamepad;},
    .GetSequenceNumber = [](IGameInputReading *s, GameInputKind k) -> uint64_t {
        return (k & GameInputKindGamepad) ? static_cast<Reading *>(s)->sequence : 0;
    },
    .GetTimestamp = [](IGameInputReading *s){return static_cast<Reading *>(s)->timestamp;},
    .GetDevice = [](auto *, IGameInputDevice **o){if (o) { device.lpVtbl->AddRef(&device); *o = &device; }},
    .GetRawReport = [](auto *, auto **o){if (o) *o = nullptr; return false;},
    .GetControllerAxisCount = zero, .GetControllerAxisState = zero,
    .GetControllerButtonCount = zero, .GetControllerButtonState = zero,
    .GetControllerSwitchCount = zero, .GetControllerSwitchState = zero,
    .GetKeyCount = zero, .GetKeyState = zero, .GetMouseState = absent,
    .GetTouchCount = zero, .GetTouchState = zero, .GetMotionState = absent,
    .GetArcadeStickState = absent, .GetFlightStickState = absent,
    .GetGamepadState = [](IGameInputReading *s, GameInputGamepadState *o){
        if (!o) return false;
        *o = static_cast<Reading *>(s)->state;
        return true;
    },
    .GetRacingWheelState = absent, .GetUiNavigationState = absent
};
static bool pressed(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }
static HRESULT current(IGameInput *, GameInputKind kind, IGameInputDevice *dev, IGameInputReading **out)
{
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!(kind & GameInputKindGamepad) || (dev && dev != &device)) return GAMEINPUT_E_READING_NOT_FOUND;
    auto *r = static_cast<Reading *>(calloc(1, sizeof(Reading)));
    if (!r) return E_OUTOFMEMORY;
    r->lpVtbl = &read_vt;
    r->refs = 1;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid == GetCurrentProcessId()) {
        if (pressed(VK_SPACE)) r->state.buttons = GameInputGamepadA;
        r->state.leftThumbstickX = pressed('D') - pressed('A');
        r->state.leftThumbstickY = pressed('W') - pressed('S');
        r->state.rightThumbstickX = pressed(VK_RIGHT) - pressed(VK_LEFT);
        r->state.rightThumbstickY = pressed(VK_UP) - pressed(VK_DOWN);
    }
    static LONG64 sequence;
    r->sequence = InterlockedIncrement64(&sequence);
    r->timestamp = now();
    *out = r;
    return S_OK;
}
static constexpr auto no_history = [](auto *, auto *, auto, auto *, auto **o){
    return fail_out(o, GAMEINPUT_E_READING_NOT_FOUND);
};
// The virtual device never disconnects. The only notification is enumeration,
// delivered before registration returns. No asynchronous callbacks are retained.
struct Registration {
    GameInputCallbackToken token;
    bool running;
    Registration *next;
};
static Registration *registrations;
static SRWLOCK reg_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE finished = CONDITION_VARIABLE_INIT;
static HRESULT register_device(IGameInput *, IGameInputDevice *dev, GameInputKind kind,
    GameInputDeviceStatus filter, GameInputEnumerationKind enumeration, void *ctx,
    GameInputDeviceCallback callback, GameInputCallbackToken *out)
{
    if (out) *out = 0;
    if (!callback) return E_INVALIDARG;
    if (dev && dev != &device) return GAMEINPUT_E_DEVICE_NOT_FOUND;
    if (enumeration != GameInputBlockingEnumeration) return E_NOTIMPL;
    static LONG64 counter;
    auto token = static_cast<GameInputCallbackToken>(InterlockedIncrement64(&counter));
    Registration *entry = nullptr;
    if (out) {
        entry = static_cast<Registration *>(malloc(sizeof(*entry)));
        if (!entry) return E_OUTOFMEMORY;
        AcquireSRWLockExclusive(&reg_lock);
        *entry = {token, true, registrations};
        registrations = entry;
        ReleaseSRWLockExclusive(&reg_lock);
    }
    auto status = device.lpVtbl->GetDeviceStatus(&device);
    if ((kind & GameInputKindGamepad) && (filter & status))
        callback(token, ctx, &device, now(), status, GameInputDeviceNoStatus);
    if (entry) {
        AcquireSRWLockExclusive(&reg_lock);
        entry->running = false;
        *out = token;
        WakeAllConditionVariable(&finished);
        ReleaseSRWLockExclusive(&reg_lock);
    }
    return S_OK;
}
static bool unregister_cb(IGameInput *, GameInputCallbackToken token, uint64_t timeout)
{
    auto start = now();
    AcquireSRWLockExclusive(&reg_lock);
    for (;;) {
        auto **p = &registrations;
        while (*p && (*p)->token != token) p = &(*p)->next;
        if (!*p) break;
        if (!(*p)->running) {
            auto *entry = *p;
            *p = entry->next;
            free(entry);
            ReleaseSRWLockExclusive(&reg_lock);
            return true;
        }
        auto elapsed = now() - start;
        if (elapsed >= timeout) break;
        auto ms = (timeout - elapsed - 1) / 1000 + 1;
        SleepConditionVariableSRW(&finished, &reg_lock, ms < INFINITE ? (DWORD)ms : INFINITE - 1, 0);
    }
    ReleaseSRWLockExclusive(&reg_lock);
    return false;
}
static IGameInputVtbl input_vt = {
    .QueryInterface = [](IGameInput *s, REFIID i, void **o){return query(s,i,o,input_iid);},
    .AddRef = [](IGameInput *) -> ULONG {return InterlockedIncrement(&input_refs);},
    .Release = [](IGameInput *) -> ULONG {return InterlockedDecrement(&input_refs);},
    .GetCurrentTimestamp = [](IGameInput *) {return now();},
    .GetCurrentReading = current, .GetNextReading = no_history, .GetPreviousReading = no_history,
    .GetTemporalReading = [](auto *, auto, auto *, auto **o){return fail_out(o, GAMEINPUT_E_READING_NOT_FOUND);},
    .RegisterReadingCallback = [](auto *, auto *, auto, auto, auto *, auto, auto *t){if (t) *t = 0; return E_NOTIMPL;},
    .RegisterDeviceCallback = register_device,
    .RegisterSystemButtonCallback = [](auto *, auto *, auto, auto *, auto, auto *t){if (t) *t = 0; return E_NOTIMPL;},
    .RegisterKeyboardLayoutCallback = [](auto *, auto *, auto *, auto, auto *t){if (t) *t = 0; return E_NOTIMPL;},
    .StopCallback = noop, .UnregisterCallback = unregister_cb,
    .CreateDispatcher = [](auto *, auto **o){return fail_out(o);},
    .CreateAggregateDevice = [](auto *, auto, auto **o){return fail_out(o);},
    .FindDeviceFromId = [](auto *, const APP_LOCAL_DEVICE_ID *id, IGameInputDevice **o){
        if (!o) return E_POINTER;
        if (!id || memcmp(id, &info.deviceId, sizeof(*id))) return fail_out(o, GAMEINPUT_E_DEVICE_NOT_FOUND);
        device.lpVtbl->AddRef(&device);
        *o = &device;
        return S_OK;
    },
    .FindDeviceFromObject = [](auto *, auto *, auto **o){return fail_out(o);},
    .FindDeviceFromPlatformHandle = [](auto *, auto, auto **o){return fail_out(o);},
    .FindDeviceFromPlatformString = [](auto *, auto *, auto **o){return fail_out(o);},
    .EnableOemDeviceSupport = unsupported, .SetFocusPolicy = noop
};
HRESULT WINAPI GameInputCreate(IGameInput **out)
{
    static IGameInput input{&input_vt};
    if (!out) return E_POINTER;
    input.lpVtbl->AddRef(&input);
    *out = &input;
    return S_OK;
}
