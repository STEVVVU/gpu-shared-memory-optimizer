# Padded three-point stencil tile

## Sub-features

For a single-block LLVM kernel with a validated exact three-point i32 sum, replace three global reads per output lane with one cooperative center read per lane, two conditional halo reads per CTA, address-space-3 shared stores, one converged CTA barrier, and two shared neighbor reads per lane. The lane reuses its center SSA value. Every source and output GEP must use i32 as its source element type, matching the four-byte address arithmetic used by the rewrite.

## How to get to it (user POV)

Build `shared-memory-opt`, then run it on `test/Inputs/P20Stencil.ll`. The accepted contract is exact: PTX kernel with `nvvm.reqntid="128,1,1"`; input and output are separate `noalias` pointers; input is read-only and output write-only; the caller passes an interior input pointer with one valid i32 halo on either side; output length is a positive multiple of 128; and the largest global index fits in unsigned i32. Unsupported IR stays unchanged.

## Driving it with the project verification script

Run `tools/verify.ps1 Launch`, `Doctor`, `Drive`, `Evidence`, and `Cleanup`. Drive runs LLVM verification before and after the transform, emits NVPTX for sm_120, checks `ld.global`/`ld.shared`/`st.shared`/`bar.sync`, runs the original and transformed PTX on the RTX 5080, and compares each output with an independently computed CPU sum for 1, 3, and 17 blocks. Negative cases remove the required noalias contract, make a load volatile, change the required block size, add `nsw` to a sum operation, or use byte-stride source/output GEPs; each must remain untransformed and verifier-clean.

## Gotchas

For the frozen larger workload, build the current runner, then run `tools/benchmark.ps1 Prepare`, `Smoke`, and `Measure` in an exclusive GPU slot. Measure uses 128, 4096, 32768, and 131072 CTAs, seven alternating samples per side and 64 launches per sample. Every sample checks poisoned output against the independent CPU oracle. Retain the raw logs. CUDA events exclude allocation, JIT, input transfer and correctness readback. Host batch time includes event overhead. Effective bandwidth counts logical input plus output bytes and does not measure DRAM traffic. Occupancy is the CUDA theoretical occupancy API result, not achieved occupancy. Use a separate profiler run before attributing a measured gap to a hardware limiter.

The runner now enables host `nvcc -O2` and records its flags and binary hash alongside the separately `llc -O3` device PTX. Launch, Prepare, Smoke and the identical frozen-input Measure passed in the coordinated slot on 22 October 2026, followed by the matched old/new PTX check. `docs/evidence/p20/metrics-release-summary.json` retains all samples and build/source/raw hashes. Existing `metrics-summary.json` retains the earlier unoptimized host-runner limitation. Even Release-host event spans can include host submission gaps and do not establish isolated hardware throughput. The center-load cleanup improves only the matched 4M case beyond full range overlap (24.7775 to22.7785 µs); other cleanup sizes overlap. The tile transform still has no demonstrated advantage over untreated PTX.

The 128-thread and halo requirements are host-side launch/allocation preconditions; the pass does not generate a host launcher or add bounds checks. It does not prove arbitrary affine loops, multiple stencils, other element types, or boundary padding. Sum operations carrying `nsw` or `nuw` are rejected because the pass does not copy those poison-generating flags. PTX static load instruction count is 3 both before and after; under the accepted CTA contract, expected dynamic global loads are 384 before and 130 after (128 centers plus two halos). Report GPU timings as observations; synchronization and branch overhead can erase the memory-traffic benefit on small inputs.
