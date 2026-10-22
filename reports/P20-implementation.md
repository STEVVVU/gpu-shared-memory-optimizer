# P20 implementation and verification record

## Scope and source provenance

The CMU 15-745 Fall 2024 gallery confirms the project topic “Autocaching for GPU Shared Memory Using LLVM” and lists its presenters, but the linked final-report exports returned HTTP 401. This work implements the confirmed topic independently; it does not claim to reproduce or grade against the unavailable student report.

The repository began as the full-history MIT-licensed `banach-space/llvm-tutor` clone at commit `49e322c602fd64838964b77c5ea109d6667048f8` (2026-09-20, “Upgrade Docker files to LLVM 23”). The upstream `LICENSE`, sources, tests, and Git history remain. The original README is retained as `README.upstream.md` and its tutorial content remains after the P20 introduction in `README.md`.

Primary references: [CMU project gallery](https://www.cs.cmu.edu/afs/cs.cmu.edu/academic/class/15745-f24/www/project_info/actual_projects.html), [LLVM NVPTX usage](https://llvm.org/docs/NVPTXUsage.html), [LLVM New Pass Manager](https://llvm.org/docs/WritingAnLLVMNewPMPass.html), and Li et al., [Automatic Data Placement into GPU On-Chip Memory Resources, CGO 2015](https://hzhou.wordpress.ncsu.edu/files/2022/12/CGO-15.pdf). LLVM documents address space 3 as shared memory and maps its CTA barrier intrinsic to `bar.sync`; the paper motivates proof-based private shared-to-register promotion. The implementation below is an explicitly bounded subset, not a reproduction of the paper.

## Implemented transformations

`StencilSharedCachePass` accepts a single-block PTX kernel only when its IR is exactly a sum of three nonvolatile, nonatomic i32 global loads at `idx-1`, `idx`, and `idx+1`, plus one output store at `idx`. It requires `nvvm.reqntid="128,1,1"`, separate `noalias` input/output pointers with readonly/writeonly attributes, and the explicit host contract string `one-dimensional-padded-i32-3point;128-threads;multiple-of-128-elements;index-fits-u32`. It also requires the input and output GEP source element types to be i32, matching the four-byte arithmetic introduced by the rewrite. It replaces the body with one global center read per lane, two conditional halo reads per CTA, a `[130 x i32]` address-space-3 tile, a reconverged barrier, three shared reads per lane, and the output store. Unsupported shape/alias/volatile/geometry/type cases remain unchanged.

The host must launch a positive multiple of 128 outputs, pass an input pointer to the first interior element with valid left/right padding, use a distinct output allocation, and keep all linear indices in unsigned i32 range. The pass does not synthesize a host launcher, bounds checks, or infer these allocation/launch facts; they are caller preconditions.

`PrivateSharedSlotPromotionPass` accepts one initialized `[128 x i32]` address-space-3 global with exactly one GEP indexed by a `tid.x` call/zext and one nonvolatile, nonatomic store-before-load pair in a single-block 128-thread PTX kernel. It requires the GEP source element type to match the global's value type. It replaces the load with the stored SSA value and removes the shared store/GEP/global, while preserving a CTA barrier. Additional GEPs (including cross-thread reads), escaping uses, other shared-memory operations, volatile/atomic accesses, mismatched GEP types, or intervening side effects reject the slot.

## Environment and build

- Windows 11, Visual Studio 2022 Build Tools/MSVC 19.44.35228, Windows SDK 10.0.26100.0.
- Shared LLVM 23.1.4 SDK: `C:\Users\steve\.codex\tools\coursework-llvm\clang+llvm-23.1.4-x86_64-pc-windows-msvc`; NVPTX backend is present.
- Isolated CUDA toolkit: CUDA 13.0.88; `nvidia-smi -L` reports an NVIDIA GeForce RTX 5080 Laptop GPU.
- The upstream CMake visibility option check was guarded for MSVC and warning/RTTI switches made MSVC-specific. CMake package configuration succeeds using the authentic shared LLVM deps (zlib 1.3.2, zstd 1.5.7, libxml2 2.15.3) and the installed DIA SDK, with explicit `ZLIB_LIBRARY=.../deps/zlib/lib/zs.lib`, `ZLIB_INCLUDE_DIR=.../deps/zlib/include`, and `LT_LLVM_INSTALL_DIR=<LLVM SDK>`.
- `cmake --build build/cmake --config Release --target shared-memory-opt --parallel 4` succeeded. The CMake-built CLI transformed the stencil fixture and `opt -passes=verify` accepted the output. The inherited `static` target also builds. Building every upstream tutorial target still fails on Windows because upstream plugin DLL targets do not link the SDK's static LLVM component libraries; several existing tutorial warnings also surface. The P20 CLI uses the CMake target successfully.
- `tools/verify.ps1 Launch` independently builds both maintained CLIs and the CUDA runner with direct MSVC/LLVM component linking, avoiding a plugin DLL/host loading dependency. This is the supported local verification route.

## Verification results

The project driver and map live in `.agents/skills/verify-gpu-shared-memory-optimizer/`. The final complete sequence was:

```powershell
.\tools\verify.ps1 Launch
.\tools\verify.ps1 Doctor
.\tools\verify.ps1 Drive
.\tools\verify.ps1 Evidence
.\tools\verify.ps1 Cleanup
```

The local run directory was `evidence/verification/run-20261021-214751497/` (generated run bundles are excluded from the published source package). The curated tracked artifacts are in `docs/evidence/p20/`, with checksums in `SHA256SUMS`. The run was performed after tightening GEP checks and the byte-stride fixtures to valid `align 1` accesses. It included original/transformed LLVM IR and NVPTX, optimizer logs, byte-input-GEP, byte-output-GEP, and byte-private-slot negative fixtures and unchanged outputs, CUDA result logs, and a full-run `sha256.txt`. Cleanup removed the active-run pointer while preserving the local evidence. The inherited static CLI fixture reports `foo=3`, `bar=2`, `fez=1`.

LLVM's verifier accepted both original and transformed GPU modules. For the stencil, original and optimized PTX each contain three static `ld.global` instruction sites; optimized PTX also contains shared loads/stores and `bar.sync`. Dynamic load counts implied by the validated 128-lane control flow are 384 global reads per original CTA versus 130 after transformation (128 centers plus two conditional halos), with three shared reads per output lane. The private-slot PTX changes from a shared store/load to no shared memory operations and retains `bar.sync`.

The CUDA Driver API runner executed original and transformed PTX on the RTX 5080 Laptop GPU. It compared each output bit-for-bit to an independent CPU oracle for 1, 3, and 17 CTAs (128, 384, and 2,176 outputs). Both stencil and private-slot cases passed at all three sizes. The stencil runner supplied the required two padding values and separate output allocation. Negative checks passed for cross-thread shared access, wrong thread geometry, a volatile input load, missing noalias attributes, an `nsw` sum, i8 input/output GEPs on i32 loads/stores, and an `[128 x i8]` private-slot GEP on an `[128 x i32]` global; LLVM verification passed for all unchanged outputs.

The source-element-type bug was reproduced before the fix and is preserved in `docs/evidence/p20/red-byte-gep/`. With i8 input GEPs and i32 loads, the accepted buggy pass generated four-byte input offsets instead of the original byte offsets; the output variant likewise changed from byte-stride to four-byte addressing. The red artifacts contain original and buggy-transformed LLVM IR/PTX with all affected accesses set to `align 1`, so the byte-stride IR is valid. These are PTX address-arithmetic comparisons, not GPU runtime measurements. The fixed byte-input and byte-output fixtures now use `align 1` and are rejected unchanged in the post-fix run. The private-slot source-type requirement is a conservative typed-shape rule requested by review; its negative fixture confirms rejection, without asserting a separate observed miscompile.

Interleaved five-round CUDA-event means, each round timing 64 launches after warming both kernels, from that verification run (microseconds per launch):

| Case | CTAs | Original | Transformed |
|---|---:|---:|---:|
| Stencil | 1 | 6.5352 | 6.5888 |
| Stencil | 3 | 6.5370 | 6.5382 |
| Stencil | 17 | 6.2456 | 6.3178 |
| Private slot | 1 | 10.7163 | 10.8280 |
| Private slot | 3 | 10.4230 | 10.7841 |
| Private slot | 17 | 10.6177 | 10.8441 |

These are local observations from one run, not general speedup claims. This run shows parity or a small slowdown; synchronization, branch overhead, and retained barriers dominate the tiny kernels. Timing variation is expected at this scale.

## CPU CI bring-up note

The first published CPU-only workflow run [39715288737](https://github.com/STEVVVU/gpu-shared-memory-optimizer/actions/runs/39715288737) added the official apt repository, found the pinned package versions, and installed the LLVM packages successfully. It then failed in the same install step, before compilation. The job log shows the final check was `/usr/lib/llvm-23/bin/llc --version | grep -Fq NVPTX`. Version-matched local LLVM 23.1.4 `llc --version` output registers the target IDs as lowercase `nvptx` and `nvptx64`, and contains no uppercase `NVPTX`, confirming that this case-sensitive check does not match the reported target list. The captured output is about 1 KB locally, so pipe-buffer `SIGPIPE` is not indicated by that evidence, though the log cannot rule out an early-close pipe interaction; the workflow now avoids the pipe entirely by saving the output to a file and checking the registered `nvptx64` identifier case-insensitively. The first run did not reach the compile, IR, or PTX checks. A rerun is needed to validate the remainder of the Linux job.

## Limits and remaining work

This implementation supports only the explicit fixtures and structural contracts above. It does not cover loop discovery, arbitrary dynamic dimensions, nonpadded edges, other dtypes, more general affine stencils, divergent source control flow, or broad alias analysis. The supplied kernel contracts are required preconditions, not runtime-checked properties. Hosted GPU CI is not configured; the real GPU evidence is local to this RTX 5080. The full inherited upstream tutorial build remains unverified/failing for its plugin DLL linkage on this static Windows LLVM SDK, although the retained upstream CLI targets and new P20 target compile as described.

## Main-agent repeat verification

Main rebuilt Launch and passed Doctor, then reran Drive/Evidence/Cleanup with PowerShell 7. All six native CUDA oracle cases and negative legality cases passed at `evidence/verification/run-20261021-220403226`. The compact GPU JSON records this later run. Windows PowerShell 5.1 treated the inherited CLI's normal stderr table as a terminating NativeCommandError before product checks; PowerShell 7 completed the driver. The supported shell is explicit.
