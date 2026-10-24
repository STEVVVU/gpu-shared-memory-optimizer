# Feature map

- [Static call counter](static-call-counter.md): inherited LLVM command-line feature used to prove the LLVM SDK and project sources can build and run together.
- [Padded fixed-radius stencil tile](stencil-tile.md): validates exact 3/9/17/33-tap i32 sums, creates cooperative address-space-3 tiles, and drives the native runner against uint32 CPU oracles. All four radii have native correctness evidence; the separate performance campaigns show no full-transaction win.
- [Thread-private shared-slot promotion](private-slot.md): removes a same-lane shared store/load pair while retaining the CTA barrier, with cross-thread access as a negative case.
