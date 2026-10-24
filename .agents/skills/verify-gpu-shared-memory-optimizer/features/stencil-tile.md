# Padded fixed-radius stencil tile

## Sub-features

The pass accepts only dense i32 sums at radii R = 1, 4, 8, 16 (3, 9, 17, 33 taps), each supplied as a separate single-basic-block LLVM fixture with the same kernel name. The output is one ordinary scalar i32 store at index `zext i32(add(mul(ctaid.x,128),tid.x)) to i64`; add and multiply operands may commute, and the actual unique tid.x operand must be present. Input has exactly one ordinary nonvolatile, nonatomic i32 load at each offset [-R,R] from that output index. Nonzero offsets are unflagged i64 add/sub operations after zext, so left halos never wrap through 32-bit arithmetic. The stored value is an unflagged modulo-32-bit add tree using every load exactly once.

All input and output GEP source types are scalar i32 with exactly one index. Input/output accesses require alignment at least four bytes. Function-level `nosync` and any restricted `memory(...)` effect reject the function unchanged because the replacement adds a barrier, shared writes and shared reads. Explicit `memory(readwrite)` is equivalent to unknown effects and remains accepted. Only the matched operations, unique tid.x/ctaid.x calls, one store and void return are accepted: additional loads, calls, arithmetic, effects, flags or divergent control flow reject the function unchanged.

The replacement has a `128+2R` element address-space-3 tile. Lane t stores input[g] at tile[t+R] and retains its center SSA value. Lanes t<R also store input[g-R] at tile[t]; lanes t>=128-R store input[g+R] at tile[t+2R]. Those three ownership ranges are disjoint and cover every tile element. All lanes converge at one unconditional CTA barrier, then sum the retained center and tile[t+R+d] for every nonzero d in [-R,R]. No shared center reload is introduced.

## How to get to it (user POV)

Build `shared-memory-opt`, then use `test/Inputs/P20Stencil.ll`, `P20StencilR4.ll`, `P20StencilR8.ll`, or `P20StencilR16.ll`. The exact attribute strings are:

| R | `p20.stencil-contract` |
| --- | --- |
| 1 | `one-dimensional-padded-i32-3point;128-threads;multiple-of-128-elements;index-fits-u32` |
| 4 | `one-dimensional-padded-i32-9point;128-threads;multiple-of-128-elements;index-fits-u32` |
| 8 | `one-dimensional-padded-i32-17point;128-threads;multiple-of-128-elements;index-fits-u32` |
| 16 | `one-dimensional-padded-i32-33point;128-threads;multiple-of-128-elements;index-fits-u32` |

The host must launch a 1D grid of unconditional full `(128,1,1)` CTAs for N outputs, N positive and divisible by 128, with `N-1 <= UINT32_MAX`. It must pass separate four-byte-aligned input/output arrays using noalias, readonly input and writeonly output attributes. Input points to the first interior element and has R valid i32 elements in each halo within the same allocation; output has N elements. These are host preconditions, not facts inferred or runtime-checked by the pass. Unsupported IR is left unchanged. The radius-1 contract string and historical negative cases remain supported.

## Driving it with the project verification script

Run `tools/verify.ps1 Launch`, `Doctor`, `Drive`, `Evidence`, `Cleanup` from PowerShell 7. The maintained `tools/check-stencil.py` CPU subset checks positive and commuted shapes plus wrong contracts, geometry, missing/wrong tid, weak input/output alignment, alias/read/write attributes, function `nosync`, restricted memory effects, explicit unknown memory effects, volatile and atomic accesses, flagged sums/index, missing/duplicate/extra offsets and loads, shifted output, byte GEPs, additional arithmetic/store/call/fence and offsets before zext. Rejected output must match the LLVM-canonicalized original body. Accepted IR passes the verifier and LLVM 23.1.4 NVPTX sm_120 lowering, with exactly three global load sites, 2R shared neighbor reads, four stores, and one barrier.

Drive uses the fixed-mode native runner for all radii at 1, 3 and 17 CTAs, comparing both sides against the independent uint32 oracle `sum(input[i..i+2R]) mod 2^32`. It poisons output and includes ordinary values, seeded full-bit random data, distinct halos and overflow-heavy values. Cleanup preserves every run artifact and its recursive hash manifest.

Native validation for radii 1, 4, 8 and 16 passed on the RTX 5080 Laptop GPU at 1, 3 and 17 CTAs, for ordinary, full-bit random and distinct-halo/overflow patterns with poisoned outputs. The extension's 132 CPU legality cases, LLVM verification, NVPTX lowering and GPU oracle receipts are retained under `docs/evidence/p20/stencil-radius-20261024/`. CPU-only verifier/PTX success alone does not establish GPU correctness.

## Performance and historical evidence

Keep the frozen `tools/benchmark.ps1` campaign and historical summaries untouched. A separate `tools/benchmark-stencils.ps1` campaign covers radii 1/4/8/16 at the same 16,384 / 524,288 / 4,194,304 / 16,777,216 output sizes, seven alternating repeats per side and 64 launches per CUDA-event sample. Both sides use `llc -O3 -mcpu=sm_120` PTX and the same `nvcc -O2` Release host. Source, binary and PTX hashes are retained; Prepare rejects an existing campaign and Measure preserves existing logs.

CUDA-event batches use resident arrays, exclude allocation, transfer and readback, and may include host submission gaps. `host_batch_us_per_call` includes event overhead and is not an end-to-end transaction. Separate `transaction` rows time warm-context allocation + H2D + output poisoning + one kernel + synchronization + D2H + free; their independent CPU oracle comparison is outside the host timer. Context creation, module load/JIT and host-vector allocation are outside that warm application scope. Every timed trial checks the same input and CPU oracle. The new summary parser requires all 224 event samples and 224 transaction samples, finite positive times, seven distinct alternating repeats and no failures.

`docs/evidence/p20/metrics-release-summary.json` retains the old radius-1 samples and hashes. The center-SSA cleanup improved the matched 4M old/new case (24.7775 to22.7785 µs), while other cleanup sizes overlap. The shared-tile transform still has no demonstrated overall advantage over untreated PTX. Separate B and C campaigns publish every radius/size result and both timer scopes in `docs/evidence/p20/stencil-radius-20261024/README.md`. At 16,777,216 elements and radius 16, the resident CUDA-event median was 372.206 to 341.648 µs in B and 372.703 to 337.615 µs in C, with seven alternating samples and nonoverlapping sample ranges. The transaction ranges overlap in both campaigns, and many other event cases overlap or are slower. No full-transaction performance win is established. Nsight Compute could not read hardware counters (`ERR_NVGPUCTRPERM`), so the hardware limiter remains unknown; logical effective GB/s is not a DRAM bandwidth measurement. There is no autotuner.

The pass does not support arbitrary affine loops, dynamic radii, multiple stencils, other element types, nonpadded boundaries, inferred allocation bounds, or divergent source kernels.
