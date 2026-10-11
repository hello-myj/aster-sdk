param([switch]$Headless, [int]$Frames = 120)
$appRoot = $PSScriptRoot
$sdkRoot = Split-Path (Split-Path $appRoot -Parent) -Parent
$asterParent = Split-Path $sdkRoot -Parent
$simEntry = Join-Path $asterParent 'aster-sim/src/cli.js'
$appPackage = Join-Path $appRoot 'dist/xtensa/com.example.neonduel.ast'
if (!(Test-Path -LiteralPath $simEntry)) { throw "aster-sim missing: $simEntry" }
if (!(Test-Path -LiteralPath $appPackage)) { throw 'Build the app first; see README.md.' }
if ($Headless) {
    & node $simEntry $appPackage --resolution 240x320 --headless --frames $Frames --screenshot (Join-Path $appRoot 'tests/title.png')
} else {
    & node $simEntry $appPackage --resolution 240x320
}
exit $LASTEXITCODE
