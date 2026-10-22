---
name: verify-gpu-shared-memory-optimizer
description: Build and drive the LLVM GPU shared-memory optimizer CLI, check its IR/PTX and CUDA execution, and preserve verification evidence.
---

# Verify GPU Shared Memory Optimizer

**Status: implemented and locally proved on the RTX 5080 for the mapped narrow fixtures.** The skill covers an inherited LLVM CLI, a bounded LLVM module optimizer, NVPTX generation, legality fixtures, and a CUDA Driver API runner. It does not claim generic loop transformation or production workload coverage.

The primary user surface is the command-line LLVM IR optimizer. The repository also has inherited `llvm-tutor` command-line tools and lit tests. Use PowerShell 7 (`pwsh`) from the project root; the maintained driver is `tools/verify.ps1`.

## Launch

Run `tools/verify.ps1 Launch`. It builds the inherited `static` CLI, `shared-memory-opt` with the LLVM pass sources, and the CUDA Driver API runner using the installed MSVC, LLVM 23.1.4, and CUDA 13.0 toolchains. It starts no persistent service.

## Doctor

Run `tools/verify.ps1 Doctor` before driving. It checks the built CLIs, LLVM version, CUDA compiler, and visible NVIDIA GPU. A missing required device or mismatched SDK makes the instance not ready.

## Drive

Run `tools/verify.ps1 Drive`. It invokes the direct-call counter; verifies original and transformed LLVM IR; generates original/optimized sm_120 PTX; checks global/shared instructions and barriers; runs the padded stencil and private-slot kernels on the real GPU; compares results with CPU oracles; and confirms unsupported geometry, aliasing, volatile, and cross-thread cases remain unchanged.

## Evidence

Run `tools/verify.ps1 Evidence` after Drive. It writes SHA-256 hashes for every run artifact and prints the action, deterministic CLI output, and GPU/event measurements in `evidence/verification/<run-id>/`. The run stores input/output IR, PTX, negative cases, optimizer diagnostics, oracle results, and timings. Treat measurements as observations, including slowdowns; never assert a speedup in advance.

## Cleanup

Run `tools/verify.ps1 Cleanup`. It removes only the active-run pointer and preserves the build, maintained source, and verification evidence. Confirm the evidence directory remains readable after cleanup.

## Helpers

The PowerShell driver is executable as documented above. Its phase names correspond to this skill's Launch, Doctor, Drive, Evidence, and Cleanup sections. Both mapped GPU features completed this lifecycle locally. The inherited upstream CMake target build has not passed with this Windows LLVM SDK; use the verifier's direct MSVC link route while that remains unresolved.
