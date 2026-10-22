[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Launch', 'Doctor', 'Drive', 'Evidence', 'Cleanup')]
    [string]$Phase
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$llvm = if ($env:COURSEWORK_LLVM23) { $env:COURSEWORK_LLVM23 } else { 'C:\Users\steve\.codex\tools\coursework-llvm\clang+llvm-23.1.4-x86_64-pc-windows-msvc' }
$deps = Join-Path (Split-Path $llvm -Parent) 'deps'
$cuda = if ($env:COURSEWORK_CUDA13) { $env:COURSEWORK_CUDA13 } else { 'C:\Users\steve\.codex\tools\coursework-cuda\toolkit' }
$build = Join-Path $repo 'build'
$bin = Join-Path $build 'bin'
$evidence = Join-Path $repo 'evidence\verification'
$activeRun = Join-Path $evidence '.active-run'
$vsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'

function Set-VSEnvironment {
    $lines = & $env:ComSpec /d /c "call `"$vsDevCmd`" -arch=amd64 -host_arch=amd64 >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'VsDevCmd failed.' }
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2]) }
    }
}

function Assert-Ready {
    foreach ($path in @("$llvm\bin\llvm-config.exe", "$llvm\lib\LLVMSupport.lib", "$deps\zlib\lib\zs.lib", "$deps\zstd\lib\zstd_static.lib", "$cuda\bin\nvcc.exe")) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Required local toolchain file is missing: $path" }
    }
}

switch ($Phase) {
    'Launch' {
        Assert-Ready
        Set-VSEnvironment
        New-Item -ItemType Directory -Force -Path $bin | Out-Null
        $mainObj = Join-Path $build 'StaticMain.obj'
        $passObj = Join-Path $build 'StaticCallCounter.obj'
        $gpuMainObj = Join-Path $build 'SharedMemoryMain.obj'
        $slotObj = Join-Path $build 'PrivateSharedSlotPromotion.obj'
        $stencilObj = Join-Path $build 'StencilSharedCache.obj'
        $exe = Join-Path $bin 'static.exe'
        $gpuExe = Join-Path $bin 'shared-memory-opt.exe'
        $runner = Join-Path $bin 'cuda-runner.exe'
        $includeArgs = @("/I$llvm\include", "/I$repo\include")
        & cl.exe /nologo /EHs-c- /GR- /std:c++17 @includeArgs /c (Join-Path $repo 'tools\StaticMain.cpp') "/Fo$mainObj"
        if ($LASTEXITCODE -ne 0) { throw 'StaticMain compilation failed.' }
        & cl.exe /nologo /EHs-c- /GR- /std:c++17 @includeArgs /c (Join-Path $repo 'lib\StaticCallCounter.cpp') "/Fo$passObj"
        if ($LASTEXITCODE -ne 0) { throw 'StaticCallCounter compilation failed.' }
        $libs = (& "$llvm\bin\llvm-config.exe" --libnames core irreader passes support).Split(' ', [StringSplitOptions]::RemoveEmptyEntries)
        if ($LASTEXITCODE -ne 0) { throw 'llvm-config failed.' }
        $systemLibs = @('psapi.lib', 'shell32.lib', 'ole32.lib', 'uuid.lib', 'advapi32.lib', 'ws2_32.lib', 'ntdll.lib')
        & cl.exe /nologo "/Fe$exe" $mainObj $passObj /link "/LIBPATH:$llvm\lib" "/LIBPATH:$deps\zlib\lib" "/LIBPATH:$deps\zstd\lib" $libs 'zs.lib' 'zstd_static.lib' $systemLibs
        if ($LASTEXITCODE -ne 0) { throw 'Static call counter link failed.' }
        if (-not (Test-Path -LiteralPath $exe)) { throw 'Static call counter executable was not produced.' }
        & cl.exe /nologo /EHs-c- /GR- /std:c++17 @includeArgs /c (Join-Path $repo 'tools\SharedMemoryMain.cpp') "/Fo$gpuMainObj"
        if ($LASTEXITCODE -ne 0) { throw 'Shared-memory CLI compilation failed.' }
        & cl.exe /nologo /EHs-c- /GR- /std:c++17 @includeArgs /c (Join-Path $repo 'lib\PrivateSharedSlotPromotion.cpp') "/Fo$slotObj"
        if ($LASTEXITCODE -ne 0) { throw 'Private-slot pass compilation failed.' }
        & cl.exe /nologo /EHs-c- /GR- /std:c++17 @includeArgs /c (Join-Path $repo 'lib\StencilSharedCache.cpp') "/Fo$stencilObj"
        if ($LASTEXITCODE -ne 0) { throw 'Stencil shared-cache pass compilation failed.' }
        & cl.exe /nologo "/Fe$gpuExe" $gpuMainObj $slotObj $stencilObj /link "/LIBPATH:$llvm\lib" "/LIBPATH:$deps\zlib\lib" "/LIBPATH:$deps\zstd\lib" $libs 'zs.lib' 'zstd_static.lib' $systemLibs 'bcrypt.lib'
        if ($LASTEXITCODE -ne 0) { throw 'Shared-memory optimizer link failed.' }
        & "$cuda\bin\nvcc.exe" --std=c++17 (Join-Path $repo 'tools\CudaRunner.cpp') "$cuda\lib\x64\cuda.lib" -o $runner
        if ($LASTEXITCODE -ne 0) { throw 'CUDA Driver API runner compilation failed.' }
        if (-not (Test-Path -LiteralPath $gpuExe) -or -not (Test-Path -LiteralPath $runner)) { throw 'GPU executables were not produced.' }
        Write-Output "Built $exe, $gpuExe, and $runner"
    }
    'Doctor' {
        $exe = Join-Path $bin 'static.exe'
        $gpuExe = Join-Path $bin 'shared-memory-opt.exe'
        $runner = Join-Path $bin 'cuda-runner.exe'
        if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $gpuExe) -or -not (Test-Path -LiteralPath $runner)) { throw 'Run Launch first; one or more executables are missing.' }
        $version = (& "$llvm\bin\llvm-config.exe" --version).Trim()
        if ($LASTEXITCODE -ne 0 -or $version -ne '23.1.4') { throw "Expected LLVM 23.1.4; found $version" }
        $nvccOutput = & "$cuda\bin\nvcc.exe" --version 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0 -or $nvccOutput -notmatch 'release 13\.0') { throw 'CUDA 13.0 compiler is unavailable.' }
        $nvidiaSmi = 'C:\Windows\System32\nvidia-smi.exe'
        if (-not (Test-Path -LiteralPath $nvidiaSmi)) { throw 'nvidia-smi is unavailable.' }
        $gpuOutput = & $nvidiaSmi -L 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0 -or $gpuOutput -notmatch 'RTX 5080 Laptop GPU') { throw 'Expected RTX 5080 Laptop GPU is not visible.' }
        Write-Output "Ready: LLVM $version; CUDA 13.0; RTX 5080 Laptop GPU; $exe"
    }
    'Drive' {
        $exe = Join-Path $bin 'static.exe'
        $gpuExe = Join-Path $bin 'shared-memory-opt.exe'
        $runner = Join-Path $bin 'cuda-runner.exe'
        $llvmBin = Join-Path $llvm 'bin'
        if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $gpuExe) -or -not (Test-Path -LiteralPath $runner)) { throw 'Run Launch first; one or more executables are missing.' }
        New-Item -ItemType Directory -Force -Path $evidence | Out-Null
        $runId = Get-Date -Format 'yyyyMMdd-HHmmssfff'
        $runDir = Join-Path $evidence "run-$runId"
        New-Item -ItemType Directory -Path $runDir | Out-Null
        $fixture = Join-Path $repo 'test\Inputs\CallCounterInput.ll'
        $output = & $exe $fixture 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
        $output | Set-Content -LiteralPath (Join-Path $runDir 'output.log') -Encoding utf8
        "Command: `"$exe`" `"$fixture`"`nExit code: $exitCode`n" | Set-Content -LiteralPath (Join-Path $runDir 'action.txt') -Encoding utf8
        if ($exitCode -ne 0) { throw "CLI exited $exitCode; see $runDir" }
        foreach ($pattern in @('foo\s+3\b', 'bar\s+2\b', 'fez\s+1\b')) {
            if ($output -notmatch $pattern) { throw "Expected direct-call count missing ($pattern); see $runDir" }
        }
        $gpuInput = Join-Path $repo 'test\Inputs\P20Stencil.ll'
        $gpuOutput = Join-Path $runDir 'stencil.optimized.ll'
        $passLog = & $gpuExe $gpuInput -o $gpuOutput 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) { throw "Stencil optimizer failed; $passLog" }
        $passLog | Set-Content -LiteralPath (Join-Path $runDir 'optimizer.log') -Encoding utf8
        foreach ($path in @($gpuInput, $gpuOutput)) {
            & "$llvmBin\opt.exe" -passes=verify -disable-output $path
            if ($LASTEXITCODE -ne 0) { throw "LLVM verifier rejected $path" }
        }
        $originalPtx = Join-Path $runDir 'stencil.original.ptx'
        $optimizedPtx = Join-Path $runDir 'stencil.optimized.ptx'
        & "$llvmBin\llc.exe" -march=nvptx64 -mcpu=sm_120 $gpuInput -o $originalPtx
        if ($LASTEXITCODE -ne 0) { throw 'NVPTX codegen failed for original stencil.' }
        & "$llvmBin\llc.exe" -march=nvptx64 -mcpu=sm_120 $gpuOutput -o $optimizedPtx
        if ($LASTEXITCODE -ne 0) { throw 'NVPTX codegen failed for optimized stencil.' }
        $originalText = Get-Content -LiteralPath $originalPtx -Raw
        $optimizedText = Get-Content -LiteralPath $optimizedPtx -Raw
        $originalGlobalLoads = ([regex]::Matches($originalText, 'ld\.global(?:\.nc)?\.b32')).Count
        $optimizedGlobalLoads = ([regex]::Matches($optimizedText, 'ld\.global(?:\.nc)?\.b32')).Count
        if ($originalGlobalLoads -ne 3 -or $optimizedGlobalLoads -ne 3 -or
            $optimizedText -notmatch 'ld\.shared\.b32' -or
            $optimizedText -notmatch 'st\.shared\.b32' -or
            $optimizedText -notmatch 'bar\.sync') {
            throw "Unexpected PTX memory mapping: original global=$originalGlobalLoads optimized global=$optimizedGlobalLoads"
        }
        $negativeInput = Join-Path $repo 'test\Inputs\P20CrossThreadSlot.ll'
        $negativeOutput = Join-Path $runDir 'negative.optimized.ll'
        $negativeLog = & $gpuExe $negativeInput -o $negativeOutput 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) { throw 'Negative legality fixture did not complete.' }
        $negativeLog | Set-Content -LiteralPath (Join-Path $runDir 'negative.log') -Encoding utf8
        & "$llvmBin\opt.exe" -passes=verify -disable-output $negativeOutput
        if ($LASTEXITCODE -ne 0) { throw 'LLVM verifier rejected negative fixture output.' }
        $negativeText = Get-Content -LiteralPath $negativeOutput -Raw
        if ($negativeText -notmatch 'addrspace\(3\)' -or
            $negativeText -notmatch 'load i32, ptr addrspace\(3\)' -or
            $negativeText -notmatch 'store i32 .*ptr addrspace\(3\)') {
            throw 'Cross-thread shared access was unexpectedly promoted.'
        }
        $slotByteInput = Join-Path $repo 'test\Inputs\P20PrivateSlotByteGep.ll'
        $slotByteOutput = Join-Path $runDir 'private-byte-gep.optimized.ll'
        $slotByteLog = & $gpuExe $slotByteInput -o $slotByteOutput 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) { throw 'Mismatched private-slot GEP case did not complete.' }
        $slotByteLog | Set-Content -LiteralPath (Join-Path $runDir 'private-byte-gep.log') -Encoding utf8
        & "$llvmBin\opt.exe" -passes=verify -disable-output $slotByteOutput
        if ($LASTEXITCODE -ne 0) { throw 'LLVM verifier rejected mismatched private-slot GEP output.' }
        $slotByteText = Get-Content -LiteralPath $slotByteOutput -Raw
        if ($slotByteText -notmatch 'getelementptr inbounds \[128 x i8\], ptr addrspace\(3\) @slot' -or
            $slotByteText -notmatch 'load i32, ptr addrspace\(3\)' -or
            $slotByteText -notmatch 'store i32 .*ptr addrspace\(3\)') {
            throw 'Mismatched private-slot GEP source type was unexpectedly promoted.'
        }

        $privateInput = Join-Path $repo 'test\Inputs\P20PrivateSlot.ll'
        $privateOutput = Join-Path $runDir 'private.optimized.ll'
        $privateLog = & $gpuExe $privateInput -o $privateOutput 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) { throw "Private-slot optimizer failed: $privateLog" }
        $privateLog | Set-Content -LiteralPath (Join-Path $runDir 'private.log') -Encoding utf8
        foreach ($path in @($privateInput, $privateOutput)) {
            & "$llvmBin\opt.exe" -passes=verify -disable-output $path
            if ($LASTEXITCODE -ne 0) { throw "LLVM verifier rejected $path" }
        }
        $privateOriginalPtx = Join-Path $runDir 'private.original.ptx'
        $privateOptimizedPtx = Join-Path $runDir 'private.optimized.ptx'
        & "$llvmBin\llc.exe" -march=nvptx64 -mcpu=sm_120 $privateInput -o $privateOriginalPtx
        if ($LASTEXITCODE -ne 0) { throw 'NVPTX codegen failed for private-slot input.' }
        & "$llvmBin\llc.exe" -march=nvptx64 -mcpu=sm_120 $privateOutput -o $privateOptimizedPtx
        if ($LASTEXITCODE -ne 0) { throw 'NVPTX codegen failed for private-slot output.' }
        $privateOriginalText = Get-Content -LiteralPath $privateOriginalPtx -Raw
        $privateOptimizedText = Get-Content -LiteralPath $privateOptimizedPtx -Raw
        if ($privateOriginalText -notmatch 'st\.shared\.b32' -or
            $privateOriginalText -notmatch 'ld\.shared\.b32' -or
            $privateOptimizedText -match '(?:st|ld)\.shared\.b32' -or
            $privateOriginalText -notmatch 'bar\.sync' -or
            $privateOptimizedText -notmatch 'bar\.sync') {
            throw 'Private shared-slot promotion did not remove only its memory operations or preserve the barrier.'
        }
        $privateCuda = & $runner $privateOriginalPtx $privateOptimizedPtx copy 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) { throw "Private-slot CUDA oracle failed: $privateCuda" }
        $privateCuda | Set-Content -LiteralPath (Join-Path $runDir 'private-cuda-results.log') -Encoding utf8

        $stencilText = Get-Content -LiteralPath $gpuInput -Raw
        $negativeVariants = @(
            @{ Name = 'wrong-geometry'; Text = $stencilText.Replace('"nvvm.reqntid"="128,1,1"', '"nvvm.reqntid"="64,1,1"') },
            @{ Name = 'volatile-load'; Text = $stencilText.Replace('%left = load i32,', '%left = load volatile i32,') },
            @{ Name = 'possibly-aliasing'; Text = $stencilText.Replace('noalias readonly', 'readonly').Replace('noalias writeonly', 'writeonly') },
            @{ Name = 'overflow-flags'; Text = $stencilText.Replace('%partial = add i32', '%partial = add nsw i32') },
            @{ Name = 'byte-input-gep'; Text = Get-Content -LiteralPath (Join-Path $repo 'test\Inputs\P20StencilByteInputGep.ll') -Raw },
            @{ Name = 'byte-output-gep'; Text = Get-Content -LiteralPath (Join-Path $repo 'test\Inputs\P20StencilByteOutputGep.ll') -Raw }
        )
        foreach ($variant in $negativeVariants) {
            $variantInput = Join-Path $runDir "$($variant.Name).ll"
            $variantOutput = Join-Path $runDir "$($variant.Name).optimized.ll"
            Set-Content -LiteralPath $variantInput -Value $variant.Text -Encoding utf8
            $variantLog = & $gpuExe $variantInput -o $variantOutput 2>&1 | Out-String
            if ($LASTEXITCODE -ne 0) { throw "Negative stencil case failed to process: $($variant.Name)" }
            $variantLog | Set-Content -LiteralPath (Join-Path $runDir "$($variant.Name).log") -Encoding utf8
            & "$llvmBin\opt.exe" -passes=verify -disable-output $variantOutput
            if ($LASTEXITCODE -ne 0) { throw "LLVM verifier rejected negative stencil output: $($variant.Name)" }
            $variantOutputText = Get-Content -LiteralPath $variantOutput -Raw
            if ($variantOutputText -match '@p20\.tile\.' -or
                ([regex]::Matches($variantOutputText, 'load(?: volatile)? i32, ptr addrspace\(1\)')).Count -ne 3) {
                throw "Unsupported stencil case was transformed: $($variant.Name)"
            }
        }
        $cudaOutput = & $runner $originalPtx $optimizedPtx stencil 2>&1 | Out-String
        $cudaExit = $LASTEXITCODE
        $cudaOutput | Set-Content -LiteralPath (Join-Path $runDir 'cuda-results.log') -Encoding utf8
        if ($cudaExit -ne 0) { throw "CUDA correctness/measurement run failed: $cudaOutput" }
        "Command: `"$exe`" `"$fixture`"`nExit code: 0`nGPU command: `"$gpuExe`" `"$gpuInput`" -o `"$gpuOutput`"; llc --march=nvptx64 --mcpu=sm_120; `"$runner`" original.ptx optimized.ptx stencil`nStatic global load instructions: original=$originalGlobalLoads optimized=$optimizedGlobalLoads`nExpected dynamic loads per CTA from the validated contract: original=384 optimized=130 (128 center + 2 halo)`n" | Set-Content -LiteralPath (Join-Path $runDir 'action.txt') -Encoding utf8
        Set-Content -LiteralPath $activeRun -Value $runDir -Encoding utf8
        Write-Output "Passed inherited CLI, transformed LLVM verifier, PTX memory checks, conservative negative case, and GPU oracle: $runDir"
    }
    'Evidence' {
        if (-not (Test-Path -LiteralPath $activeRun)) { throw 'Run Drive first; no active evidence run is recorded.' }
        $runDir = (Get-Content -LiteralPath $activeRun -Raw).Trim()
        foreach ($name in @('action.txt', 'output.log', 'optimizer.log', 'stencil.original.ptx', 'stencil.optimized.ptx', 'cuda-results.log', 'private.optimized.ptx', 'private-cuda-results.log', 'negative.log', 'private-byte-gep.log', 'wrong-geometry.log', 'volatile-load.log', 'possibly-aliasing.log', 'overflow-flags.log', 'byte-input-gep.log', 'byte-output-gep.log')) {
            if (-not (Test-Path -LiteralPath (Join-Path $runDir $name))) { throw "Evidence is incomplete: $name" }
        }
        $manifest = Join-Path $runDir 'sha256.txt'
        Get-ChildItem -LiteralPath $runDir -File | Where-Object Name -ne 'sha256.txt' | Get-FileHash -Algorithm SHA256 | ForEach-Object { "$($_.Hash)  $([IO.Path]::GetFileName($_.Path))" } | Set-Content -LiteralPath $manifest -Encoding ascii
        Get-Content -LiteralPath $manifest
        Get-Content -LiteralPath (Join-Path $runDir 'action.txt')
        Get-Content -LiteralPath (Join-Path $runDir 'output.log')
    }
    'Cleanup' {
        if (Test-Path -LiteralPath $activeRun) {
            $runDir = (Get-Content -LiteralPath $activeRun -Raw).Trim()
            Remove-Item -LiteralPath $activeRun
            if (-not (Test-Path -LiteralPath $runDir)) { throw 'Cleanup removed the recorded evidence.' }
            Write-Output "Preserved evidence: $runDir"
        } else {
            Write-Output 'No active run scratch state.'
        }
    }
}
