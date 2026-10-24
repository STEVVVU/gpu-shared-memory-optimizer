# Structural and alignment rejection regressions

The fixtures are `test/Inputs/P20StencilMissingTid.ll` and `P20StencilWeakAlignment.ll`. LLVM 23.1.4 `opt -passes=verify -disable-output` accepts both source modules. The pre-change `build/bin/shared-memory-opt.exe` accepted and rebuilt both; the fixed binary rejects and preserves them. `*.old.ll` is the buggy transformed output; `*.new.ll` is the rejected unchanged output. Original inputs remain in the maintained fixtures.

Observed before/after:

```
MissingTid old_accepted=True
WeakAlignment old_accepted=True
MissingTid new_accepted=False
WeakAlignment new_accepted=False
```

MissingTid's source index is block*128+0, while the old transformed index adds tid.x. WeakAlignment's source accesses permit alignment 1, while the old reconstructed accesses require alignment 4. These artifacts prove acceptance and rewrite defects; they do not assert an observed GPU miscompile. No GPU was used in this reproduction.

The full local raw bundle at `evidence/shape-legality-red/` preserves old binaries, source snapshots, source/PTX/binary hashes, first-fix patch, old/new optimizer diagnostics, rebuild log and CPU validation log. The new CPU driver also checks missing/wrong tid and weak input/output alignment independently at every supported radius, alongside its other legality cases.
