param([string]$Python = 'python', [string]$Output = '')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$out = if ($Output) { [System.IO.Path]::GetFullPath($Output) } else { Join-Path $root 'dist/windows' }
New-Item -ItemType Directory -Force $out | Out-Null
& $Python (Join-Path $root 'tools/release_identity.py') (Join-Path $out 'release.json')
if ($LASTEXITCODE -ne 0) { throw 'Identity generation failed' }
$argsList = @('-m', 'PyInstaller', '--onefile', '--console', '--clean', '--noconfirm',
    '--paths', (Join-Path $root 'tools'), '--distpath', $out,
    '--workpath', (Join-Path $root 'build/windows-pyinstaller'),
    '--specpath', (Join-Path $root 'build/windows-spec'),
    '--add-data', "$(Join-Path $root 'targets');targets",
    '--add-data', "$(Join-Path $root 'templates');templates",
    '--add-data', "$(Join-Path $out 'release.json');.",
    '--exclude-module', 'build', '--name', 'aster', (Join-Path $root 'tools/aster.py'))
$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $Python
$start.Arguments = ($argsList | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' '
$start.UseShellExecute = $false
$process = [System.Diagnostics.Process]::Start($start)
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw 'Aster CLI packaging failed' }
