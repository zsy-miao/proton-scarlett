# xg_xs.dll — measured Windows x64 subset

`XGSuggestSwizzleMode` suggests layout **0x109** (64KB_S) for 4096×4096
2D BC1, BC3, BC4 or BC5 UNORM textures with one mip, one array element, one
sample, quality zero, unknown input layout, no resource flags, and default
placement alignment (0 or 65536). The second argument must be zero. It writes
one 32-bit layout on success, including when the output aliases `desc.Layout`.
For other requests it returns `E_NOTIMPL` without modifying the output.
Null required pointers return `E_POINTER`. These failure codes are local policy.

`XGCreateTextureComputer` clears the object output and returns `E_NOTIMPL` for
all descriptors (`E_POINTER` for null arguments). A valid swizzle suggestion
is not enough to implement subresource copies or produce a resource layout.
**Do not treat this DLL as a working texture computer:** two title paths use
the object without checking the result and can dereference null.

## Evidence for the suggestion

Microsoft's [DirectStorageTextureShuffling sample][sample] selects 64KB_S in
`ShuffleTextures/ShuffleTextures.cpp` and serializes `0x100 | tileMode`.
`DirectStorageTextureShuffling.cpp` uses that field as `D3D12_RESOURCE_DESC.Layout`
with 64 KiB placement alignment, no flags, sample count 1 and array size 1.
The shipped `Assets/Textures/BC{1,3,4,5}_shuffled_compressed.bin` headers at
offset `0x04` each give width 4096, height 4096 and mip count 1; offsets
`0x10` and `0x14` give format (respectively 71, 77, 80, 83) and layout
`0x109` in little-endian. The next word is a **different** DirectStorage
shuffle operation. These are demonstrated legal layouts, not a measurement
of Microsoft's suggestion heuristic. No texture payload is needed for tests.

[sample]: https://github.com/microsoft/Xbox-GDK-Samples/tree/b1e77b6ac10f95b07296025fda171ca2ddc42dbe/Samples/Graphics/DirectStorageTextureShuffling

## ABI evidence from the title (static inspection only)

`game_scarlett_master.exe` SHA-256:
`e95020b2fa4a721db3df59fdecf75621730ac559d8835252f6c3b658d5a08993`.
Addresses below are RVAs; preferred image base `0x140000000`.

| Import | IAT RVA | Jump thunk RVA | Direct call RVAs |
| --- | --- | --- | --- |
| XGCreateTextureComputer | `06793f60` | `05f3ff40` | `00344235`, `00979c75`, `016f7141`, `05121a5e` |
| XGSuggestSwizzleMode | `06793f68` | `05f3ff50` | `00c4b3ce`, `04eccca2` |

* Suggest: `RCX` points at a resource description. Both calls zero `EDX`,
  clearing the whole register; this does not distinguish a null pointer from
  an integer option. `R8` is `RCX+0x2c`, the same 32-bit field into which the
  alternate branch writes `0x100 | explicitMode`. `EAX` goes to the signed
  status helper at `04e9b660`. Descriptor stores at `04ecc9b0`–`04ecc9e4`
  match the Win64 `D3D12_RESOURCE_DESC` fields; `016f7113`–`016f713a` copies
  56 bytes for Create. The implementation reads only the common fields.
* Create: `RCX` is the description, `RDX` addresses a pointer-sized output.
  `00979c75` and `05121a5e` branch on negative status. `00344235` and
  `016f7141` do not. Observed object calls use vtable byte offsets `0x08`,
  `0x10`, `0x40`, `0x50`. Neither a complete interface nor the values to write
  into the large layout structure follow from these observations.

## Reproduce the synthetic import test

From this directory with x86-64 MinGW-w64, binutils and Wine:

```sh
mkdir -p build
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -shared xg.c \
  -Wl,--out-implib,build/libxg_xs.a -o build/xg_xs.dll
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror test.c \
  -Lbuild -lxg_xs -o build/test.exe
objdump -p build/xg_xs.dll | grep -E 'XGCreate|XGSuggest'
WINEPREFIX="$PWD/build/prefix" WINEDLLOVERRIDES=xg_xs=n wine build/test.exe
```

Expected: both names exported; `xg_xs synthetic import tests passed`. No
ordinal API, console SDK or external console compatibility layer is required.
