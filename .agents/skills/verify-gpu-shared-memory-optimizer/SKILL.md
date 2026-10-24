---
name: verify-gpu-shared-memory-optimizer
description: Build and drive the LLVM GPU shared-memory optimizer CLI, check its IR/PTX and CUDA execution, and preserve verification evidence.
---

# Verify GPU Shared Memory Optimizer

**Status: radius 1/4/8/16 stencil and private-slot paths were locally proved on the RTX 5080.** The extension passed 132 CPU legality cases, LLVM verification, sm_120 PTX checks, and native CUDA oracle runs at 1, 3, and 17 CTAs. Two separate seven-repeat campaigns are published; they do not establish an overall performance win, and hardware profiling remains blocked by counter permissions. See [the radius campaign report](../../../docs/evidence/p20/stencil-radius-20261024/README.md). The skill covers an inherited LLVM CLI, a bounded LLVM module optimizer, NVPTX generation, legality fixtures, and a CUDA Driver API runner. It does not claim generic loop transformation or production workload coverage.

The primary user surface is the command-line LLVM IR optimizer. The repository also has inherited `llvm-tutor` command-line tools and lit tests. Use PowerShell 7 (`pwsh`) from the project root; the maintained driver is `tools/verify.ps1`.

## Launch

Run `tools/verify.ps1 Launch`. It builds the inherited `static` CLI, `shared-memory-opt` with the LLVM pass sources, and the CUDA Driver API runner using the installed MSVC, LLVM 23.1.4, and CUDA 13.0 toolchains. It starts no persistent service.

## Doctor

Run `tools/verify.ps1 Doctor` before driving. It checks the built CLIs, LLVM version, CUDA compiler, visible NVIDIA GPU and the runner's CPU-only modulo-i32 oracle self-check. A missing required device or mismatched SDK makes the instance not ready.

## Drive

Run `tools/verify.ps1 Drive`. It invokes the direct-call counter; runs `tools/check-stencil.py` for all four radii with positive, commuted and conservative rejection cases; verifies original/transformed LLVM IR and sm_120 PTX; checks global/shared instructions and barriers; and runs the stencil and private-slot kernels against CPU oracles. Each radius uses 1, 3 and 17 CTAs with deterministic ordinary values, full-bit random values and distinct halos plus wrapping values; every output is poisoned before correctness launches. Unsupported geometry, aliasing, volatile/atomic, weak alignment, missing lane, byte GEPs, malformed offsets/sums and extra instructions remain unchanged.

The reusable CPU-only subset is `python tools/check-stencil.py --optimizer build/bin/shared-memory-opt.exe --llvm-bin <LLVM>/bin --evidence evidence/verification/<run>/stencil-legality`. `--fixtures-only` verifies generated inputs without invoking the optimizer or NVPTX compiler. The Linux CPU workflow runs the full subset against its newly built binary. This subset provides no GPU-correctness or performance evidence.

## Evidence

Run `tools/verify.ps1 Evidence` after Drive. It writes SHA-256 hashes for every run artifact and prints the action, deterministic CLI output, and GPU/event measurements in `evidence/verification/<run-id>/`. The run stores input/output IR, PTX, negative cases, optimizer diagnostics, oracle results, and timings. Treat measurements as observations, including slowdowns; never assert a speedup in advance.

## Cleanup

Run `tools/verify.ps1 Cleanup`. It removes only the active-run pointer and preserves the build, maintained source, and verification evidence. Confirm the evidence directory remains readable after cleanup.

## Helpers

The PowerShell driver is executable as documented above. Its phase names correspond to this skill's Launch, Doctor, Drive, Evidence, and Cleanup sections. Historical radius-1 and private-slot features completed this lifecycle locally; re-drive the extension before claiming it proved. Use the verifier's direct MSVC link route; the separate P20 CMake target has also built, while the full inherited plugin suite remains unsupported with this static Windows SDK.

Run the separate fixed-radius campaign with `tools/benchmark-stencils.ps1 Prepare`, `Smoke`, `Measure` after Launch, in an exclusive GPU slot. Use a fresh `-RunDirectory` for another campaign; prepared binaries, sources and PTX are hashed, and existing logs are preserved. `python tools/summarize-stencils.py --run <directory> --output <new-summary.json> --accepted-window <confirmed-slot>` validates exact dimensions, alternating repeats, completed work and both timer scopes. `python tools/summarize-stencils.py --self-test` checks its failure handling. Keep historical `benchmark.ps1`, its logs and `metrics-release-summary.json` unchanged.
