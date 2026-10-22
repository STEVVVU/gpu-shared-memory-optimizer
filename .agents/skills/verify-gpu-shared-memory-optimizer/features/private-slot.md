# Thread-private shared-slot promotion

## Sub-features

For an exactly 128-thread NVPTX kernel, replace one `[128 x i32]` address-space-3 store/load pair indexed by the same `tid.x`-derived value with its stored SSA value. The pass requires a single basic block and a nonvolatile, nonatomic store-before-load pair; it preserves allowed CTA barriers.

## How to get to it (user POV)

Build `shared-memory-opt`, then run it on `test/Inputs/P20PrivateSlot.ll`. The resulting module must pass LLVM verification. The host must launch exactly 128 threads; `nvvm.reqntid` encodes this fixed contract.

## Driving it with the project verification script

Run `tools/verify.ps1 Launch`, `Doctor`, `Drive`, `Evidence`, and `Cleanup`. Drive checks original PTX has shared load/store operations, transformed PTX removes them but retains `bar.sync`, then runs both versions on the GPU and compares each against a CPU identity oracle. `P20CrossThreadSlot.ll` must retain its shared operations because the read uses another lane's slot.

## Gotchas

Only one global slot, one shared GEP, one store and one load in a single-block kernel are accepted. The analysis rejects extra uses, escaping pointers, atomics, volatile accesses, and side effects between store and load other than a CTA barrier.
