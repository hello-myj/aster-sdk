param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$argsList = @('-m', 'PyInstaller', '--onefile', '--console', '--clean', '--noconfirm',
    '--paths', (Join-Path $root 'tools'), '--distpath', (Join-Path $root 'dist/windows'),
    '--workpath', (Join-Path $root 'build/windows-pyinstaller'),
    '--specpath', (Join-Path $root 'build/windows-spec'),
    '--add-data', "$(Join-Path $root 'targets');targets",
    '--add-data', "$(Join-Path $root 'templates');templates",
    '--exclude-module', 'build', '--name', 'aster', (Join-Path $root 'tools/aster.py'))
$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $Python
$start.Arguments = ($argsList | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' '
$start.UseShellExecute = $false
$process = [System.Diagnostics.Process]::Start($start)
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw 'Aster CLI packaging failed' }
