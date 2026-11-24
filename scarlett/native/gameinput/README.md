# GameInput ordinal 1: x64 polling subset

Original provider for one virtual gamepad. Holding Space while a window of the
calling process has foreground focus sets `GameInputGamepadA`. A reading is an
immutable sample; it stays valid until its final `Release`.

Build against Microsoft's **Windows SDK 10.0.26100 `um/GameInput.h`** (v0,
SHA-256 `c77b8621d3d503f5ddcb2a45f76f024f3e6f881e568133e7bf1a4fc05985d539`).
The header is a build input, not redistributed. The three COM tables are
declared by that header and initialized by method name. Do not substitute the
newer versioned GameInput interfaces. No other provider or console code is used.

## Call-site checks

RVAs in build 1.0.2.1 (`e95020b2fa4a721db3df59fdecf75621730ac559d8835252f6c3b658d5a08993`),
obtained by static disassembly, image base `0x140000000`:

| RVA | Observed x64 call / data use | Windows SDK v0 |
| --- | --- | --- |
| `59258c3` | ordinal 1, RCX = output address, signed HRESULT test | `GameInputCreate` |
| `592586a` | `+48h`, kind `40000h`, status `1`, enum `2`, context/callback/token | blocking `RegisterDeviceCallback` |
| `2211d72` | device `+18h`, copies 32 bytes from returned pointer `+20h` | `GetDeviceInfo`, `deviceId` |
| `22127b6` | input `+80h`, ID and device out | `FindDeviceFromId` |
| `2211b9f` | input `+20h`, `40000h`, device, reading out | `GetCurrentReading(Gamepad)` |
| `7a4255` | reading `+b0h`, bool in AL; consumes DWORD at 0, floats at +4..+18h | `GetGamepadState` |
| `2211030` | input `+68h`, token, 5000 | `UnregisterCallback` |
| `2211c4c`, `43a1e6b` | input `+30h`/`+28h`, reference reading | previous/next reading |
| `43a1d0e`, `43a1d40` | kind `10h`, reading `+70h` | keyboard reading, key count |

The last two rows are **not implemented**: no reading history and no native
keyboard-kind readings are advertised. Only blocking device enumeration is
supported; no hotplug, async event stream, physical controller, output,
background input, or console-only interface extensions. Other SDK entries
return unsupported/empty results or perform no action as their signatures
allow. Stock Proton 11.0-2e's builtin ordinal 1 returns `E_NOTIMPL`.

## Synthetic test (NixOS)

```bash
scarlett/native/gameinput/test.sh /path/to/10.0.26100/um/GameInput.h
```

The script builds the DLL and a separate C client importing ordinal 1 (NONAME).
It runs stock Proton's Wine in bubblewrap, on a new private Xvfb, with no
network or desktop socket. No retail package is mounted. It also checks
blocking enumeration, lookup by device ID, unregister, failed keyboard polling,
and retained snapshots across a 400 ms XTEST Space hold. The printed scratch
directory contains the DLL, EXE, and `test.log`. Packaging/activation is separate.
