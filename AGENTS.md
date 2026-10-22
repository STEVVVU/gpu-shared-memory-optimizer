# Project instructions

This project retains the full MIT-licensed `llvm-tutor` source history and examples as its LLVM API base. Keep inherited tutorial material and `README.upstream.md`; put the GPU optimizer in the existing `include/`, `lib/`, `tools/`, and `test/` layout.

Use the shared LLVM 23.1.4 SDK at `C:\Users\steve\.codex\tools\coursework-llvm\clang+llvm-23.1.4-x86_64-pc-windows-msvc` and the isolated CUDA 13 toolkit at `C:\Users\steve\.codex\tools\coursework-cuda\toolkit`. Do not install packages globally or modify shared language environments. Keep build products under ignored `build*/` directories and verification evidence under `evidence/verification/`.

The supported source transformation must be an LLVM pass over LLVM IR. Keep every legality condition explicit and conservative; unsupported IR must remain unchanged. Validate the output with LLVM's verifier and NVPTX code generation before running the owned CUDA runner against its independent CPU oracle. See `.agents/skills/verify-gpu-shared-memory-optimizer/SKILL.md` for the project driver.
