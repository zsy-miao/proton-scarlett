# Device +0x428: private backing capture

`ScarlettCapturePlacedV1` takes the exact code base and 174 MiB width. The
shared XMem page lock pins three complete views of one 2,960-page allocation
in VA order: code (27 MiB), WC data (47 MiB), WB data (100 MiB). The other
two extents need not be mapped. Use XMem to unmap before editing the title
reservations, as required by the existing page API. GPU imports use only the
pinned handles after unlocking; there is no subsequent title VA lookup.

`ScarlettPlacedDataV1` returns a borrowed data resource and checked
extent-relative byte range. A trusted recorder must use that range, check
its operation-specific constraints (e.g. alignment, state and device), and
keep the capture alive through the last queue fence. Empty ranges, crossings,
code and out-of-bounds requests return no operand. The original code handle
stays inside XMem. The production factory still fails: a whole-arena
descriptor has no checked consumer on this baseline.

Build `test-placed-capture.cpp` like `test-descriptor-pages.cpp`; it needs
no DXIL files. Launch the synthetic with `SCARLETT_TEST_GPU=radv`. It
rejects missing and mixed runs, then copies the last data words from two
generations after both runs were freed and the title VAs replaced.
