# Sized control release — scratch handoff

`release-only.patch` is based on patches 0001–0004. It never sets the
control mark. After verifying the backing and creating the public control
view, its creator must set `VPROT_SCARLETT_CONTROL` under the VM mutex
before publishing the view. Never infer this mark from size, address,
protection, WC metadata or generic section creation. Reserve information
class `0x53430009` for release (no output pointers, exact 64 KiB implicit).
The implementation handles current-process native x64 section views and
standalone ref-counted DMA views; placeholder subviews are excluded.

Build and stage **both** Wine artifacts: `dlls/ntdll/ntdll.so` at
`files/lib/wine/x86_64-unix/ntdll.so`, and
`dlls/kernelbase/x86_64-windows/kernelbase.dll` at
`files/lib/wine/x86_64-windows/kernelbase.dll`. With just the Unix change
the Win32 request is still refused. A patched kernelbase and older ntdll
fail closed on the unknown information class.

`synthetic-control-full.patch`, the two binaries here and `test-release.c`
are for proof only. They add a CPU-backed test mark at `0x5354ff01`;
do not ship this allocator or the test ntdll. The actual `VirtualFree`
import was tested with a scratch copy of Proton. Log:
`../scarlett-run.fNYvPGdF/logs/steam-999001.log`.

No GPU cache/coherency or GPU queue retirement claim follows from this CPU
lifetime test. Once a qualified creation path exists, run these same checks
with its real backing and independently hold a device binding through fence
retirement and exact-address reuse.
