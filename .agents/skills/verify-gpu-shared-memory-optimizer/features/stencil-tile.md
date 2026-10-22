# Padded three-point stencil tile

## Sub-features

For a single-block LLVM kernel with a validated exact three-point i32 sum, replace three global reads per output lane with one cooperative center read per lane, two conditional halo reads per CTA, address-space-3 shared stores, one converged CTA barrier, and three shared reads per lane. Every source and output GEP must use i32 as its source element type, matching the four-byte address arithmetic used by the rewrite.

## How to get to it (user POV)

Build `shared-memory-opt`, then run it on `test/Inputs/P20Stencil.ll`. The accepted contract is exact: PTX kernel with `nvvm.reqntid="128,1,1"`; input and output are separate `noalias` pointers; input is read-only and output write-only; the caller passes an interior input pointer with one valid i32 halo on either side; output length is a positive multiple of 128; and the largest global index fits in unsigned i32. Unsupported IR stays unchanged.

## Driving it with the project verification script

Run `tools/verify.ps1 Launch`, `Doctor`, `Drive`, `Evidence`, and `Cleanup`. Drive runs LLVM verification before and after the transform, emits NVPTX for sm_120, checks `ld.global`/`ld.shared`/`st.shared`/`bar.sync`, runs the original and transformed PTX on the RTX 5080, and compares each output with an independently computed CPU sum for 1, 3, and 17 blocks. Negative cases remove the required noalias contract, make a load volatile, change the required block size, add `nsw` to a sum operation, or use byte-stride source/output GEPs; each must remain untransformed and verifier-clean.

## Gotchas

The 128-thread and halo requirements are host-side launch/allocation preconditions; the pass does not generate a host launcher or add bounds checks. It does not prove arbitrary affine loops, multiple stencils, other element types, or boundary padding. Sum operations carrying `nsw` or `nuw` are rejected because the pass does not copy those poison-generating flags. PTX static load instruction count is 3 both before and after; under the accepted CTA contract, expected dynamic global loads are 384 before and 130 after (128 centers plus two halos). Report GPU timings as observations; synchronization and branch overhead can erase the memory-traffic benefit on small inputs.
