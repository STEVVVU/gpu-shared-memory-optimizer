# Curated P20 evidence

These are compact review artifacts copied from the post-fix verification run `run-20261021-214751497`. They contain LLVM IR and generated NVPTX only; no executables, object files, build directories, or raw run bundle are included here. The local GPU measurements and exact source run are recorded in `gpu-results.json`.

The GPU result is local evidence from an RTX 5080 Laptop GPU. The separate GitHub Actions workflow performs CPU-side compilation, LLVM verification, and NVPTX code generation only; it does not execute on a GPU.

## Byte-GEP regression evidence

`red-byte-gep/` contains original and pre-fix transformed modules/PTX for two mismatches: `i8` GEPs feeding `i32` input loads, and an `i8` output GEP feeding an `i32` store. All relevant accesses use `align 1`, so the byte-stride source modules are valid. The PTX shows the pre-fix transformation changing their addressing to four-byte strides. The fixed pass now rejects these GEPs. This is a compiler/PTX comparison, not a GPU runtime result.
