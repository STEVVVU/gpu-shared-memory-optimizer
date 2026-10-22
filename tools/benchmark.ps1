param(
    [ValidateSet('Prepare', 'Smoke', 'Measure')]
    [string]$Phase = 'Prepare',
    [string]$RunDirectory = 'evidence/benchmark'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $repo
if ((Get-PSDrive C).Free -lt 10GB) { throw 'Benchmark requires 10 GiB free disk.' }
$llvm = if ($env:COURSEWORK_LLVM23) { $env:COURSEWORK_LLVM23 } else { 'C:/Users/steve/.codex/tools/coursework-llvm/clang+llvm-23.1.4-x86_64-pc-windows-msvc' }
New-Item -ItemType Directory -Force $RunDirectory | Out-Null
if ($Phase -eq 'Prepare') {
    foreach ($name in @('Stencil', 'PrivateSlot')) {
        $inputIR = "test/Inputs/P20$name.ll"
        $stem = Join-Path $RunDirectory $name
        & ./build/bin/shared-memory-opt.exe $inputIR -o "$stem.optimized.ll"
        if ($LASTEXITCODE -ne 0) { throw 'Optimizer failed.' }
        foreach ($side in @('original', 'optimized')) {
            $ir = if ($side -eq 'original') { $inputIR } else { "$stem.optimized.ll" }
            & "$llvm/bin/opt.exe" -passes=verify -disable-output $ir
            if ($LASTEXITCODE -ne 0) { throw 'IR verifier failed.' }
            & "$llvm/bin/llc.exe" -O3 -march=nvptx64 -mcpu=sm_120 $ir -o "$stem.$side.ptx"
            if ($LASTEXITCODE -ne 0) { throw 'NVPTX generation failed.' }
        }
    }
    git rev-parse HEAD | Set-Content "$RunDirectory/source-sha.txt"
    git diff HEAD | Set-Content "$RunDirectory/source.patch"
    Get-FileHash tools/CudaRunner.cpp, tools/verify.ps1, build/bin/cuda-runner.exe, "$RunDirectory/*.ptx" | Format-List | Out-File "$RunDirectory/hashes.txt"
    if (-not (Test-Path build/bin/cuda-runner-build.txt)) { throw 'Rebuild the Release host runner via tools/verify.ps1 Launch before Prepare.' }
    Copy-Item -LiteralPath build/bin/cuda-runner-build.txt -Destination "$RunDirectory/host-build.txt"
    'Prepared verifier-clean PTX. Build the current CudaRunner.cpp via tools/verify.ps1 Launch before Smoke.'
} else {
    & ./tools/verify.ps1 Doctor
    & nvidia-smi --query-gpu=name,driver_version,memory.total,power.draw,clocks.sm,clocks.mem,temperature.gpu --format=csv | Set-Content "$RunDirectory/hardware-$Phase.csv"
    if ($LASTEXITCODE -ne 0) { throw 'Hardware inventory failed.' }
    $flag = if ($Phase -eq 'Smoke') { '--smoke' } else { '--benchmark' }
    foreach ($case in @(@('Stencil', 'stencil'), @('PrivateSlot', 'copy'))) {
        $stem = Join-Path $RunDirectory $case[0]
        & ./build/bin/cuda-runner.exe "$stem.original.ptx" "$stem.optimized.ptx" $case[1] $flag 2>&1 | Tee-Object "$stem.$Phase.log"
        if ($LASTEXITCODE -ne 0) { throw "CUDA $Phase failed. Retain the failed log." }
    }
}
