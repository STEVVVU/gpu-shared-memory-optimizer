# GPU shared-memory optimizer

This project adds two bounded LLVM 23 module transformations to the full-history, MIT-licensed [`llvm-tutor`](https://github.com/banach-space/llvm-tutor) codebase. It is an educational, pattern-specific optimizer rather than a general GPU loop optimizer.

`StencilSharedCachePass` recognizes one exact padded three-point `i32` stencil in a 128-thread NVPTX kernel. It creates a cooperative 130-element address-space-3 tile, conditionally loads the two halos, synchronizes the CTA, and replaces each lane's three global reads with shared-memory reads. The matcher requires the input and output GEPs to use `i32` source element types and leaves unsupported IR unchanged.

`PrivateSharedSlotPromotionPass` removes one same-lane store/load pair for a `[128 x i32]` address-space-3 slot in an exact 128-thread single-block kernel. It verifies the GEP type matches the slot global and preserves CTA barriers. It rejects cross-thread accesses and unsupported uses.

## Build and verify

On the configured Windows development machine, run the maintained product verifier from PowerShell 7 (`pwsh`):

```powershell
.\tools\verify.ps1 Launch
.\tools\verify.ps1 Doctor
.\tools\verify.ps1 Drive
.\tools\verify.ps1 Evidence
.\tools\verify.ps1 Cleanup
```

This builds the LLVM IR CLI and CUDA Driver API runner, verifies original and transformed IR, emits NVPTX, checks unsupported cases, and runs both kernels on the locally installed GPU against CPU oracles. See the [verification driver instructions](.agents/skills/verify-gpu-shared-memory-optimizer/SKILL.md) for prerequisites and evidence handling.

The [P20 CPU-only CI workflow](.github/workflows/p20-cpu-llvm23.yml) installs a pinned LLVM 23.1.4 package from the [official apt.llvm.org repository](https://apt.llvm.org/), builds the optimizer, verifies the IR transformations and generates NVPTX. It does not run a GPU and provides no GPU-execution or performance claim.

## Contracts and evidence

The stencil caller must provide a positive output length divisible by 128, an input pointer to the first interior `i32` with one valid halo element on either side, a separate `noalias` output allocation, and indices within unsigned `i32` range. The pass does not add bounds checks or create a host launcher. Full scope, source provenance, verification results, and limitations are documented in the [P20 implementation record](reports/P20-implementation.md).

Curated LLVM IR, NVPTX, a local GPU result JSON file, and a valid `align 1` byte-GEP red reproduction are in [`docs/evidence/p20/`](docs/evidence/p20/). The GPU result is a local RTX 5080 Laptop GPU observation; timing results are measurements from one machine and are not general speedup claims. The CMU gallery confirms the project topic, but its linked final report was inaccessible; this implementation does not claim to reproduce that report.

The untouched upstream README is preserved as [`README.upstream.md`](README.upstream.md), and inherited workflows are retained outside the active workflow directory under [`docs/archive/upstream-workflows/`](docs/archive/upstream-workflows/).
