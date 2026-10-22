# Feature map

- [Static call counter](static-call-counter.md): inherited LLVM command-line feature used to prove the LLVM SDK and project sources can build and run together.
- [Padded three-point stencil tile](stencil-tile.md): validates a narrow affine global-load pattern, creates a cooperative address-space-3 tile with two halo lanes, and checks real RTX 5080 execution against a CPU oracle.
- [Thread-private shared-slot promotion](private-slot.md): removes a same-lane shared store/load pair while retaining the CTA barrier, with cross-thread access as a negative case.
