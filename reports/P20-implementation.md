# P20 implementation and verification record

## Function-attribute legality correction, 24 October 2026

The radius-1/4/8/16 matcher previously ignored function-level `nosync` and memory-effect attributes while replacing the body with a barrier and shared-memory accesses. The valid red reproduction at `C:\Users\steve\.codex\orchestrate\resume-strength30-20261024\p20-function-attributes-red.ll` has both `nosync` and `memory(argmem: readwrite)`; the old pass transformed it while preserving those incompatible attributes. Its transformed output is `C:\Users\steve\.codex\orchestrate\resume-strength30-20261024\p20-function-attributes-transformed-red.ll`. The cause is confirmed in `isSupported`: the prior precondition checked argument attributes but neither `NoSync` nor function memory effects.

The matcher now rejects `nosync` and every function memory-effect summary other than unknown before rewriting. The maintained CPU checker generates both negative variants for all four radii and requires LLVM-canonicalized unchanged output. `memory(readwrite)` is explicitly tested as accepted unknown effects. The fresh green run at `evidence/verification/function-attributes-20261024/stencil-legality/` passed 132 legality, LLVM verifier and NVPTX PTX cases; the runner's CPU-only `--self-test` passed. This establishes the bounded function-attribute behavior and CPU/PTX checks only, not GPU execution or performance.

## Fixed-radius extension, 24 October 2026

The current stencil matcher supports only exact dense radii 1, 4, 8 and 16 with separate 3/9/17/33-point contract strings and fixtures. It matches the scalar i32 output GEP first, proves its index is `zext i32(ctaid.x*128+actual tid.x) to i64` (commuted add/multiply allowed), and requires exactly one aligned, ordinary i32 input load at every i64 offset [-R,R], an unflagged modulo-32-bit add tree using each load once, and one output store. A matched-instruction whitelist rejects extra operations and effects. All original function/launch/noalias/read/write preconditions remain required; input and output accesses below four-byte alignment are rejected.

The generalized builder uses `128+2R` shared elements. All lanes store center at t+R and retain its SSA value; the first R lanes fill slots [0,R) from g-R, the last R lanes fill [128+R,128+2R) from g+R, and all converge at one unconditional barrier. The result uses center plus 2R shared neighbor reads and ordinary i32 adds. The host promises a 1D full-CTA launch, positive N divisible by 128, `N-1 <= UINT32_MAX`, separate four-byte-aligned arrays, and R valid input halo elements on both sides within the same allocation. The pass does not infer or check those host facts.

Before rebuilding, the existing optimizer accepted both `P20StencilMissingTid.ll` (index omits tid.x) and `P20StencilWeakAlignment.ll` (align 1). The first exposed a structural acceptance bug: the old matcher found a block multiply but never required the actual lane operand. The second exposed an alignment-proof gap: it reconstructed align-4 accesses from accepted align-1 IR. These are confirmed acceptance/rewrite defects, not claims of a native GPU miscompile. Old and new raw IR, optimizer diagnostics, old binaries/source snapshots and hashes are retained in `evidence/shape-legality-red/`; compact review artifacts are in `docs/evidence/p20/red-shape-legality/`. The new binary rejects both fixtures unchanged. The earlier byte-GEP red evidence and frozen performance evidence remain intact.

In the granted CPU build slot, `pwsh -NoProfile -File tools/verify.ps1 Launch` succeeded using the maintained MSVC/LLVM 23.1.4 and CUDA 13 nvcc -O2 host route, with LLVM-header C4624 warnings. `python tools/check-stencil.py --optimizer build/bin/shared-memory-opt.exe --llvm-bin <LLVM>/bin --evidence evidence/verification/stencil-cpu-extension` passed 120 cases: eight supported/commuted shapes and 112 unsupported shapes rejected unchanged after canonicalizing both sides with opt. All accepted inputs/outputs passed LLVM verification and llc -O3 sm_120 lowering. The runner's CPU-only `--self-test`, both summary-parser self-checks, PowerShell parsing and diff whitespace checks passed. The first test-driver attempt compared opt's inferred target datalayout against the CLI's omitted layout; canonicalizing both sides fixed that harness mismatch without changing product behavior.

The owned CUDA runner retains copy/stencil modes and adds fixed stencil-r4/r8/r16 descriptors. Its independent uint32 oracle defines modulo-32-bit sums without C++ signed overflow. Native correctness covers poisoned outputs, 1/3/17 CTAs, deterministic ordinary values, full-bit random data and distinct overflow-heavy halos. These native extension checks are **pending an exclusive GPU grant**, so the extension is not yet claimed GPU-correct or faster.

The separate maintained `tools/benchmark-stencils.ps1` campaign uses radii 1/4/8/16 at the historical four sizes, seven alternating repeats and 64 resident-array launches per event sample, identical llc -O3 sm_120 device lowering, and nvcc -O2 host code. Separate transaction rows time warm-context allocation, H2D, output poisoning, one launch, synchronization, D2H and free; CPU oracle work, context/module/JIT and host-vector allocation are outside that timer. The old host batch metric excludes allocation/transfers and is not that transaction metric. `tools/summarize-stencils.py` requires all event and transaction dimensions, seven distinct alternating repeats, finite positive timings and successful work/oracles. Prepared inputs, sources, binaries, PTX and raw logs are hashed; existing campaign logs and historical summaries are preserved. No autotuner or broader performance claim is added; hardware profiling remains a separate prerequisite for attributing a limiter.

The default verifier and CPU workflow now drive all radii; the feature map and skill document the exact invocation and evidence lifecycle. The historical sections below describe the original radius-1 implementation and evidence, rather than new extension validation.

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

The corrected CPU workflow passed at `32bccb1a455f97c9fa860a206aa61cdc5aa84aa8` in [run 39716593312](https://github.com/STEVVVU/gpu-shared-memory-optimizer/actions/runs/39716593312). Main inspected its actual logs: both transformations applied, three unsupported shapes were rejected, LLVM IR verification and NVPTX generation passed. This hosted run used no GPU. The fresh main-owned local GPU verification remains `run-20261021-220403226` with retained CPU-oracle results and cleanup proof.

This implementation supports only the explicit fixtures and structural contracts above. It does not cover loop discovery, arbitrary dynamic dimensions, nonpadded edges, other dtypes, more general affine stencils, divergent source control flow, or broad alias analysis. The supplied kernel contracts are required preconditions, not runtime-checked properties. Hosted GPU CI is not configured; the real GPU evidence is local to this RTX 5080. The full inherited upstream tutorial build remains unverified/failing for its plugin DLL linkage on this static Windows LLVM SDK, although the retained upstream CLI targets and new P20 target compile as described.

## Main-agent repeat verification

Main rebuilt Launch and passed Doctor, then reran Drive/Evidence/Cleanup with PowerShell 7. All six native CUDA oracle cases and negative legality cases passed at `evidence/verification/run-20261021-220403226`. The compact GPU JSON records this later run. Windows PowerShell 5.1 treated the inherited CLI's normal stderr table as a terminating NativeCommandError before product checks; PowerShell 7 completed the driver. The supported shell is explicit.
