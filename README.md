# GPU shared-memory optimizer

This project adds two bounded LLVM 23 module transformations to the full-history, MIT-licensed [`llvm-tutor`](https://github.com/banach-space/llvm-tutor) codebase. It is an educational, pattern-specific optimizer rather than a general GPU loop optimizer.

`StencilSharedCachePass` recognizes exact padded `i32` sums with fixed radii 1, 4, 8, or 16 (3, 9, 17, or 33 taps) in a 128-thread NVPTX kernel. It creates a cooperative address-space-3 tile of `128+2R` elements, loads R halos on each side, synchronizes the CTA, and reuses the lane's center SSA value plus shared neighbors. The matcher anchors scalar `i32` GEPs on the output index `zext i32(block*128+tid.x) to i64`, requires every offset exactly once and accesses aligned to at least four bytes, and leaves unsupported IR unchanged.

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

The [P20 CPU-only CI workflow](.github/workflows/p20-cpu-llvm23.yml) uses the SHA-256-pinned [official LLVM 23.1.4 release SDK](https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.4), builds the optimizer, verifies the IR transformations and generates NVPTX. It does not run a GPU and provides no GPU-execution or performance claim. The earlier apt snapshot pin disappeared when that repository advanced to 23.1.5; the release archive preserves the required version.

The Linux build, IR checks, unsupported byte-GEP cases and NVPTX lowering passed in [run 39716593312](https://github.com/STEVVVU/gpu-shared-memory-optimizer/actions/runs/39716593312). GPU execution was checked separately on the local RTX 5080 Laptop GPU.

## Contracts and evidence

The coordinated Release-host RTX 5080 Laptop campaign ran on 22 October 2026 with CUDA 13.0.88, driver 592.01 and LLVM 23.1.4. The host runner uses `nvcc -O2` and device PTX uses `llc -O3`. Seven alternating samples of 64 launches per side use resident arrays and independently checked outputs. Stencil event times below are per-launch microseconds, with the median and full sample range.

| Output elements | Untreated | Shared tile with center reuse |
| --- | --- | --- |
| 16,384 | 6.44 [6.342,7.4655] | 6.488 [6.1915,8.1865] |
| 524,288 | 8.1215 [6.9365,14.484] | 8.2985 [7.686,12.536] |
| 4,194,304 | 19.9 [18.7135,23.522] | 22.9005 [22.7395,24.647] |
| 16,777,216 | 173.248 [172.861,257.307] | 182.23 [174.097,257.228] |

The shared-tile median is slower at every size, and its full ranges overlap untreated PTX. Inspection found a redundant reload of the lane's center from shared memory. Reusing its dominating SSA value removes one of three shared reads while preserving the center store, neighbor stores and CTA barrier. In a separate, same-slot old-versus-new PTX comparison through the same Release host, the 4M case improves from 24.7775 [24.7365,24.854] to 22.7785 [22.724,22.842] µs, about 8.1%, with nonoverlapping ranges. The other three cleanup comparisons overlap. This scoped cleanup gain does not overcome the transform's cost relative to untreated PTX; the tile transform has no demonstrated overall performance advantage. It remains opt-in through its explicit IR contract.

The [Release-host measurements](docs/evidence/p20/metrics-release-summary.json) retain all seven samples and raw-log SHA256 hashes for both stencil and private-slot paths, the runner/source hashes and build flags, plus the matched old/new experiment. Earlier unconfirmed-isolation runs are diagnostic. The [earlier clean campaign](docs/evidence/p20/metrics-summary.json) remains preserved with its original host-build limitation. Maximum input/output GPU payload is 128 MiB. Both stencil variants report 100% theoretical occupancy, rather than achieved occupancy. Timings exclude JIT, allocation, input copy and oracle readback. Effective bandwidth counts logical input and output bytes rather than DRAM traffic. Hardware counter profiling was unavailable; instruction counts do not establish a hardware bottleneck.

Event intervals around batches of launches can include gaps from host submission even with Release host code. They characterize this launch path and do not isolate hardware throughput. Rebuilding the host with `-O2` and rerunning the identical frozen inputs did not establish an optimizer win; no gain is claimed from a cross-campaign timing change.

Reproduce with `tools/benchmark.ps1 Prepare`, `Smoke`, and `Measure` after building the current tools. Retain the first PTX and logs before rebuilding a candidate, then compare the two optimized PTX files with `build/bin/cuda-runner.exe old.ptx new.ptx stencil --benchmark`. Run `python tools/summarize-benchmark.py` to validate and curate both campaigns. Run only in an exclusive GPU slot.

The stencil caller must launch a one-dimensional grid of full `(128,1,1)` CTAs for positive output length N divisible by 128 and `N-1 <= UINT32_MAX`. Input and output must be separate four-byte-aligned arrays; the interior input pointer has R valid halo elements in both directions within the same allocation. The exact contracts and bounded IR shapes are in the [stencil feature map](.agents/skills/verify-gpu-shared-memory-optimizer/features/stencil-tile.md). The pass does not add bounds checks or create a host launcher. Full scope, source provenance, verification results, and limitations are documented in the [P20 implementation record](reports/P20-implementation.md).

The fixed-radius 1/4/8/16 extension passed 132 CPU legality cases, LLVM verification, sm_120 PTX generation and native RTX 5080 oracle checks at 1, 3 and 17 CTAs. Two separate seven-repeat campaigns measured resident CUDA-event batches and full warm transactions at all 16 radius/size combinations. Radius 16 at 16,777,216 elements had transformed event medians of 341.648 and 337.615 microseconds, but full transaction ranges overlap the baseline and several other event cases are slower; no overall win is established. Hardware profiling remains blocked by a profiler driver-load failure after counter access was enabled. See the [full campaign report and receipts](docs/evidence/p20/stencil-radius-20261024/README.md). The CPU workflow checks the extension separately. No autotuner is included.

Curated LLVM IR, NVPTX, a local GPU result JSON file, and a valid `align 1` byte-GEP red reproduction are in [`docs/evidence/p20/`](docs/evidence/p20/). The GPU result is a local RTX 5080 Laptop GPU observation; timing results are measurements from one machine and are not general speedup claims. The CMU gallery confirms the project topic, but its linked final report was inaccessible; this implementation does not claim to reproduce that report.

The untouched upstream README is preserved as [`README.upstream.md`](README.upstream.md), and inherited workflows are retained outside the active workflow directory under [`docs/archive/upstream-workflows/`](docs/archive/upstream-workflows/).
