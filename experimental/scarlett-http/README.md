# Scarlett HTTP provider

Builds an x64 PE named `libHttpClient.GDK.dll` from the MIT-licensed
[Microsoft/libHttpClient](https://github.com/microsoft/libHttpClient/tree/c5007fd9cbdc785518ea6dccc5751b125d451cc1)
Win32 implementation. It uses WinHTTP, upstream XAsync and task queues, and
the upstream-pinned zlib. No GDK SDK or GDK redistributable is required.

```sh
./build.sh                         # prints Provider directory: /…/bin
python3 test.py /path/to/bin       # starts a loopback server and a private Wine prefix
```

Requires Bash, curl, sha256sum, tar, patch, Make, CMake, and a threaded
`x86_64-w64-mingw32` GCC/G++ toolchain with binutils. The test requires Wine
with x64 and WinHTTP support. Work goes under `/tmp/opencode/scarlett-http-provider`
unless a different scratch root is passed to `build.sh`. Place the generated DLL
beside the guest executable for an app-local trial. Keep the accompanying license
and notice files with the DLL; the selected MinGW runtime's license terms also apply.

## Verification and limits

* `required-exports.txt` is the 38 libHttpClient import names from
  `game_scarlett_master.exe` (SHA-256 `e95020b2fa4a721db3df59fdecf75621730ac559d8835252f6c3b658d5a08993`).
  The game uses names, not ordinals. All 38 function declarations match the
  x64 C signatures in `Microsoft.GDK.PC` NuGet `2510.5.6322`; one SAL
  input/output annotation differs. The public `XAsyncBlock` is 56 bytes.
* Wine 11.0, MinGW-w64 GCC 16.2: 154 named exports. The probe resolved all
  38 required exports, initialized the client, sent a POST with a header and
  body via a thread-pool queue, checked the network result, HTTP 201, response
  header and echoed body, closed the handle, and waited for async cleanup.
* The MinGW patch retains the ETW provider GUID but uses `EventWriteString`
  rather than the Windows SDK TraceLogging event format. The upstream Win32
  export set is not identical to the console GDK export set.
* This verifies standalone HTTP using a queue created by this DLL. Compatibility
  with handles created by a separate console task-queue runtime, title-specific
  networking, TLS, and game startup have not been demonstrated.
