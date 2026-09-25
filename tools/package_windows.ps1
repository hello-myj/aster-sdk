param(
    [string]$Python = "python",
    [string]$WasiSdk = "",
    [string]$Wamrc = "",
    [string]$Cache = ""
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Cache) { $Cache = Join-Path $root "build\windows-downloads" }
New-Item -ItemType Directory -Force $Cache | Out-Null
$out = Join-Path $root "dist\windows"
New-Item -ItemType Directory -Force $out | Out-Null

if (-not $WasiSdk) {
    $archive = Join-Path $Cache "wasi-sdk-24.0-x86_64-windows.tar.gz"
    if (-not (Test-Path $archive)) {
        Invoke-WebRequest -Uri "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-24/wasi-sdk-24.0-x86_64-windows.tar.gz" -OutFile $archive
    }
    $digest = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($digest -ne "0f934c6e7e171b5627c81b697a9e57e96d65cd889f56ce6077350de48978afd3") {
        Remove-Item $archive -Force
        throw "wasi-sdk archive SHA-256 mismatch; retry the command"
    }
    $extract = Join-Path $Cache "wasi-sdk"
    if (-not (Test-Path (Join-Path $extract ".complete"))) {
        New-Item -ItemType Directory -Force $extract | Out-Null
        & tar -xzf $archive -C $extract
        if ($LASTEXITCODE -ne 0) { throw "wasi-sdk extraction failed" }
        New-Item -ItemType File (Join-Path $extract ".complete") | Out-Null
    }
    $clang = Get-ChildItem $extract -Filter clang.exe -Recurse -File | Select-Object -First 1
    if (-not $clang) { throw "wasi-sdk archive has no clang.exe" }
    $WasiSdk = $clang.Directory.Parent.FullName
}
if (-not (Test-Path (Join-Path $WasiSdk "bin\clang.exe"))) {
    throw "expected wasi-sdk bin\clang.exe under $WasiSdk"
}
# APPs are freestanding wasm32-unknown-unknown. Keep the compiler, linker,
# archive/symbol tools and Clang resource directory; omit WASI sysroots and
# unrelated host tools from the single-file EXE.
$slim = Join-Path $Cache "wasi-sdk-app"
if (Test-Path $slim) { Remove-Item $slim -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $slim "bin") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $slim "lib") | Out-Null
foreach ($name in @("clang.exe", "wasm-ld.exe", "lld.exe", "llvm-ar.exe", "llvm-nm.exe")) {
    $file = Join-Path $WasiSdk "bin\$name"
    if (-not (Test-Path $file)) { throw "wasi-sdk missing $name" }
    Copy-Item $file (Join-Path $slim "bin\$name")
}
Copy-Item (Join-Path $WasiSdk "lib\clang") (Join-Path $slim "lib") -Recurse
$runtimeDlls = @()
foreach ($name in @("MSVCP140.dll", "VCRUNTIME140.dll", "VCRUNTIME140_1.dll")) {
    $file = Join-Path $env:WINDIR "System32\$name"
    if (-not (Test-Path $file)) { throw "Microsoft runtime DLL missing: $file" }
    Copy-Item $file (Join-Path $slim "bin\$name")
    $runtimeDlls += $file
}
$WasiSdk = $slim

if (-not $Wamrc) {
    $archive = Join-Path $Cache "wamrc-2.4.3-x86_64-windows-2022.zip"
    if (-not (Test-Path $archive)) {
        Invoke-WebRequest -Uri "https://github.com/wasm-micro-runtime/wasm-micro-runtime/releases/download/WAMR-2.4.3/wamrc-2.4.3-x86_64-windows-2022.zip" -OutFile $archive
    }
    $digest = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($digest -ne "ca6dfb01cf6b40a4afa7afcac07a020f8aca0385616fd1e6d6a5f67f7e30f638") {
        Remove-Item $archive -Force
        throw "WAMR archive SHA-256 mismatch; retry the command"
    }
    $extract = Join-Path $Cache "wamrc"
    if (-not (Test-Path (Join-Path $extract "wamrc.exe"))) {
        Expand-Archive -Path $archive -DestinationPath $extract -Force
    }
    $Wamrc = Join-Path $extract "wamrc.exe"
}
if (-not (Test-Path $Wamrc)) { throw "wamrc.exe missing: $Wamrc" }

& $Python -m pip install -r (Join-Path $root "requirements-windows.txt")
if ($LASTEXITCODE -ne 0) { throw "Windows Python dependencies failed to install" }

$common = @(
    "--noconfirm", "--clean", "--onefile", "--console",
    "--paths", (Join-Path $root "tools"),
    "--distpath", $out,
    "--workpath", (Join-Path $root "build\windows-pyinstaller"),
    "--specpath", (Join-Path $root "build\windows-spec")
)
$sdkData = @(
    "--add-data", "$(Join-Path $root 'contract');contract",
    "--add-data", "$(Join-Path $root 'targets');targets",
    "--add-data", "$(Join-Path $root 'include');include"
)
$toolData = @(
    "--add-data", "$(Join-Path $root 'lib');lib",
    "--add-data", "$(Join-Path $root 'toolchain\LICENSE');toolchain",
    "--add-data", "$(Join-Path $root 'NOTICE.md');.",
    "--add-data", "$WasiSdk;toolchain/wasi-sdk-24.0-x86_64-windows",
    "--add-data", "$Wamrc;toolchain"
)
foreach ($file in $runtimeDlls) {
    $toolData += @("--add-data", "$file;toolchain")
}

& $Python -m PyInstaller @common @sdkData @toolData `
    --hidden-import pack_ast --hidden-import verify_ast `
    --collect-all cryptography --name aster-toolchain `
    (Join-Path $root "tools\windows_toolchain.py")
if ($LASTEXITCODE -ne 0) { throw "aster-toolchain.exe packaging failed" }

& $Python -m PyInstaller @common @sdkData `
    --hidden-import esptool --hidden-import serial.tools.list_ports `
    --collect-all esptool --collect-all serial --collect-all cryptography `
    --name aster-device (Join-Path $root "tools\windows_device.py")
if ($LASTEXITCODE -ne 0) { throw "aster-device.exe packaging failed" }

Write-Host "Windows tools: $out\aster-toolchain.exe and $out\aster-device.exe"
