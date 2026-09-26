param(
    [string]$Cache = "",
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"
$env:MSBUILDDISABLENODEREUSE = "1"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Cache) { $Cache = Join-Path $root "build\windows-downloads" }
if (-not $Output) { $Output = Join-Path $Cache "wamrc-xtensa\wamrc.exe" }
New-Item -ItemType Directory -Force $Cache | Out-Null

# Match the firmware's WAMR 2.4.3 and its Xtensa-capable LLVM 18 source.
# The firmware vendor tree uses AOT format version 6 (upstream 2.4.3 is 5).
$wamrCommit = "0e47872fb4e57833dbeccf7c312a406fa2dac2bc"
$llvmCommit = "344b928e67bfc1d07cb150609c55548e86a9bf19"
$wamrSource = Join-Path $Cache "wamr-2.4.3-source"
$llvmSource = Join-Path $Cache "llvm-xtensa-18-source"
$llvmBuild = Join-Path $Cache "llvm-xtensa-18-build"
$wamrBuild = Join-Path $Cache "wamrc-xtensa-build"

function Invoke-Native([string]$Command, [string[]]$Arguments) {
    $quoted = @($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') { '"' + $_.Replace('"', '\"') + '"' } else { $_ }
    })
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $Command
    $start.Arguments = $quoted -join " "
    $start.UseShellExecute = $false
    $process = [System.Diagnostics.Process]::Start($start)
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    $process.Dispose()
    if ($exitCode -ne 0) { throw "$Command failed with exit code $exitCode" }
}

function Get-PinnedSource([string]$Url, [string]$Commit, [string]$Destination) {
    if (-not (Test-Path (Join-Path $Destination ".git"))) {
        New-Item -ItemType Directory -Force $Destination | Out-Null
        Invoke-Native $gitExe @("-C", $Destination, "init")
        Invoke-Native $gitExe @("-C", $Destination, "remote", "add", "origin", $Url)
    }
    $marker = Join-Path $Destination ".aster-pinned-commit"
    if ((Test-Path $marker) -and (Get-Content $marker -Raw).Trim() -eq $Commit) { return }
    Invoke-Native $gitExe @("-C", $Destination, "fetch", "--depth", "1", "origin", $Commit)
    Invoke-Native $gitExe @("-C", $Destination, "checkout", "--detach", "FETCH_HEAD")
    Set-Content -Path $marker -Value $Commit -Encoding Ascii
}

$gitExe = (Get-Command git.exe -ErrorAction SilentlyContinue).Source
$cmakeExe = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if (-not $gitExe) { throw "git.exe is required on PATH" }
if (-not $cmakeExe) { throw "cmake.exe is required on PATH" }
Get-PinnedSource "https://github.com/wasm-micro-runtime/wasm-micro-runtime.git" `
    $wamrCommit $wamrSource
Get-PinnedSource "https://github.com/espressif/llvm-project.git" `
    $llvmCommit $llvmSource

$config = Join-Path $wamrSource "core\config.h"
$configText = [System.IO.File]::ReadAllText($config)
if ($configText -notmatch '#define AOT_CURRENT_VERSION [56]') {
    throw "unexpected WAMR AOT version in $config"
}
$configText = $configText -replace '#define AOT_CURRENT_VERSION [56]', '#define AOT_CURRENT_VERSION 6'
[System.IO.File]::WriteAllText($config, $configText)

Invoke-Native $cmakeExe @(
    "-S", (Join-Path $llvmSource "llvm"), "-B", $llvmBuild,
    "-G", "Visual Studio 17 2022", "-A", "x64",
    "-DLLVM_TARGETS_TO_BUILD=X86",
    "-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=Xtensa",
    "-DLLVM_ENABLE_PROJECTS=",
    "-DLLVM_BUILD_LLVM_C_DYLIB=OFF", "-DLLVM_BUILD_LLVM_DYLIB=OFF",
    "-DLLVM_INCLUDE_TESTS=OFF", "-DLLVM_INCLUDE_TOOLS=ON", "-DLLVM_INCLUDE_EXAMPLES=OFF",
    "-DLLVM_INCLUDE_BENCHMARKS=OFF", "-DLLVM_INCLUDE_DOCS=OFF",
    "-DLLVM_ENABLE_BINDINGS=OFF", "-DLLVM_ENABLE_LIBXML2=OFF",
    "-DLLVM_ENABLE_ZLIB=OFF", "-DLLVM_ENABLE_ZSTD=OFF",
    "-DLLVM_ENABLE_TERMINFO=OFF", "-DLLVM_ENABLE_LIBEDIT=OFF",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"
)
Invoke-Native $cmakeExe @("--build", $llvmBuild, "--config", "Release", "--parallel", "4")

$llvmConfig = Join-Path $llvmBuild "lib\cmake\llvm"
if (-not (Test-Path (Join-Path $llvmConfig "LLVMConfig.cmake"))) {
    throw "LLVMConfig.cmake missing after LLVM build"
}
Invoke-Native $cmakeExe @(
    "-S", (Join-Path $wamrSource "wamr-compiler"), "-B", $wamrBuild,
    "-G", "Visual Studio 17 2022", "-A", "x64",
    "-DWAMR_BUILD_PLATFORM=windows", "-DWAMR_BUILD_TARGET=X86_64",
    "-DWAMR_BUILD_WITH_CUSTOM_LLVM=1", "-DLLVM_DIR=$llvmConfig",
    "-DWAMR_BUILD_LIBC_WASI=0", "-DWAMR_BUILD_LIB_PTHREAD=0",
    "-DWAMR_BUILD_LIB_WASI_THREADS=0"
)
Invoke-Native $cmakeExe @("--build", $wamrBuild, "--config", "Release", "--target", "wamrc")
$built = Join-Path $wamrBuild "Release\wamrc.exe"
if (-not (Test-Path $built)) { throw "wamrc.exe missing after build" }
New-Item -ItemType Directory -Force (Split-Path $Output -Parent) | Out-Null
Copy-Item $built $Output -Force
Write-Host "Xtensa-capable Windows wamrc: $Output"
