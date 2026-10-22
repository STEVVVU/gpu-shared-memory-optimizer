# Static call counter

## Sub-features

Count direct static calls to each function in a textual or bitcode LLVM module.

## How to get to it (user POV)

Build `static` and run it with `test/Inputs/CallCounterInput.ll` as its module argument.

## Driving it with the project verification script

Run `tools/verify.ps1 Launch`, `Doctor`, and `Drive`. The CLI output must report `foo=3`, `bar=2`, and `fez=1` for the maintained fixture.

## Gotchas

This inherited tutorial feature only proves the base LLVM CLI and link setup. It does not prove the P20 GPU optimizer or any GPU behavior.
