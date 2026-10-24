param(
    [ValidateSet('Prepare', 'Smoke', 'Measure')]
    [string]$Phase = 'Prepare',
    [string]$RunDirectory = 'evidence/benchmark-stencil-radius'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $repo
$llvm = if ($env:COURSEWORK_LLVM23) { $env:COURSEWORK_LLVM23 } else { 'C:/Users/steve/.codex/tools/coursework-llvm/clang+llvm-23.1.4-x86_64-pc-windows-msvc' }
$cases = @(
    @{ Name = 'R1'; Fixture = 'P20Stencil'; Mode = 'stencil' },
    @{ Name = 'R4'; Fixture = 'P20StencilR4'; Mode = 'stencil-r4' },
    @{ Name = 'R8'; Fixture = 'P20StencilR8'; Mode = 'stencil-r8' },
    @{ Name = 'R16'; Fixture = 'P20StencilR16'; Mode = 'stencil-r16' }
)
$manifest = Join-Path $RunDirectory 'manifest.json'
if ($Phase -eq 'Prepare') {
    if (Test-Path $manifest) { throw 'Prepared campaign already exists; choose a fresh RunDirectory to preserve its artifacts.' }
    New-Item -ItemType Directory -Force $RunDirectory | Out-Null
    $hostBuild = Get-Content build/bin/cuda-runner-build.txt -Raw
    $sourceHash = (Get-FileHash tools/CudaRunner.cpp -Algorithm SHA256).Hash
    if ($hostBuild -notmatch 'nvcc --std=c\+\+17 -O2' -or -not $hostBuild.Contains("CudaRunner.cpp SHA256=$sourceHash")) {
        throw 'Rebuild the current Release host runner with tools/verify.ps1 Launch first.'
    }
    & ./build/bin/cuda-runner.exe --self-test
    if ($LASTEXITCODE -ne 0) { throw 'CPU runner oracle self-check failed.' }
    foreach ($case in $cases) {
        $stem = Join-Path $RunDirectory $case.Name
        Copy-Item "test/Inputs/$($case.Fixture).ll" "$stem.original.ll"
        & ./build/bin/shared-memory-opt.exe "$stem.original.ll" -o "$stem.optimized.ll" 2>&1 | Set-Content "$stem.optimizer.log"
        if ($LASTEXITCODE -ne 0) { throw "Optimizer failed: $($case.Name)" }
        if ((Get-Content "$stem.optimized.ll" -Raw) -notmatch '@p20\.tile\.') { throw "Expected transform absent: $($case.Name)" }
        foreach ($side in @('original', 'optimized')) {
            & "$llvm/bin/opt.exe" -passes=verify -disable-output "$stem.$side.ll"
            if ($LASTEXITCODE -ne 0) { throw 'IR verifier failed.' }
            & "$llvm/bin/llc.exe" -O3 -march=nvptx64 -mcpu=sm_120 "$stem.$side.ll" -o "$stem.$side.ptx"
            if ($LASTEXITCODE -ne 0) { throw 'NVPTX lowering failed.' }
        }
    }
    Copy-Item build/bin/cuda-runner-build.txt (Join-Path $RunDirectory 'host-build.txt')
    git rev-parse HEAD | Set-Content (Join-Path $RunDirectory 'source-sha.txt')
    git diff HEAD | Set-Content (Join-Path $RunDirectory 'source.patch')
    $paths = @('tools/CudaRunner.cpp', 'tools/check-stencil.py', 'tools/benchmark-stencils.ps1', 'tools/summarize-stencils.py', 'lib/StencilSharedCache.cpp', 'tools/verify.ps1', 'build/bin/cuda-runner.exe', 'build/bin/shared-memory-opt.exe')
    $paths += @(Get-ChildItem $RunDirectory -File | ForEach-Object FullName)
    $hashes = @{}
    foreach ($path in $paths) { $hashes[[IO.Path]::GetRelativePath($repo, (Resolve-Path $path).Path)] = (Get-FileHash $path -Algorithm SHA256).Hash }
    @{
        scope = 'fixed radius 1/4/8/16, LLVM llc -O3 sm_120, nvcc -O2 host; independent uint32 oracle'
        elements = @(16384, 524288, 4194304, 16777216)
        repeats = 7
        launches_per_event_sample = 64
        transaction = 'warm context and module; allocate+H2D+poison+one launch+sync+D2H+free; CPU oracle outside timer'
        hashes = $hashes
    } | ConvertTo-Json -Depth 5 | Set-Content $manifest
    Write-Output "Prepared separate radius campaign: $RunDirectory"
} else {
    if (-not (Test-Path $manifest)) { throw 'Run Prepare first.' }
    $record = Get-Content $manifest -Raw | ConvertFrom-Json
    foreach ($entry in $record.hashes.PSObject.Properties) {
        if ((Get-FileHash $entry.Name -Algorithm SHA256).Hash -ne $entry.Value) { throw "Prepared artifact changed: $($entry.Name)" }
    }
    & ./tools/verify.ps1 Doctor
    & nvidia-smi --query-gpu=name,driver_version,memory.total,power.draw,clocks.sm,clocks.mem,temperature.gpu --format=csv | Set-Content "$RunDirectory/hardware-$Phase.csv"
    if ($LASTEXITCODE -ne 0) { throw 'Hardware inventory failed.' }
    $flag = if ($Phase -eq 'Smoke') { '--smoke' } else { '--benchmark' }
    foreach ($case in $cases) {
        $stem = Join-Path $RunDirectory $case.Name
        if (Test-Path "$stem.$Phase.log") { throw "Retain existing log and choose a fresh campaign: $stem.$Phase.log" }
        & ./build/bin/cuda-runner.exe "$stem.original.ptx" "$stem.optimized.ptx" $case.Mode $flag 2>&1 | Tee-Object "$stem.$Phase.log"
        if ($LASTEXITCODE -ne 0) { throw "CUDA $Phase failed; retained log: $stem.$Phase.log" }
    }
}
