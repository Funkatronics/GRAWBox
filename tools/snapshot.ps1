# Makes test packages of both games from main as it is on GitHub: a manual snapshot, for testing
# what main holds (not run on every merge). Each package is the add-on folder package_test.ps1
# makes, with SNAPSHOT.txt naming the commit it was built from. The packages hold translated game
# code: they stay under private/ and are not for sharing.
#   tools/snapshot.ps1 -SdkSource <rexglue-sdk clone> -Runtime <folder with the C++ runtime DLLs>
#                      [-Out <folder>] [-Recipes1 <file>] [-Recipes2 <file>]
# -Out defaults to private\test-package, so GRAW1-NATIVE-ADDON and GRAW2-NATIVE-ADDON there are
# replaced (their bin-native\ and PLAY-*.bat; anything else in them is kept). Recipes come from
# tools/make_recipes.ps1.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SdkSource,
    [Parameter(Mandatory)][string]$Runtime,
    [string]$Out,
    [string]$Recipes1,
    [string]$Recipes2
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Out) { $Out = Join-Path $root 'private\test-package' }

# Only a clean checkout of main, level with GitHub's: a snapshot names exactly one commit.
Push-Location $root
try {
    git fetch -q origin main
    if ($LASTEXITCODE) { throw 'git fetch failed' }
    $branch = git rev-parse --abbrev-ref HEAD
    if ($branch -ne 'main') { throw "on ${branch}: check out main first" }
    if (git status --porcelain --untracked-files=no) { throw 'tracked files are changed: commit or stash them first' }
    $head = git rev-parse HEAD
    if ($head -ne (git rev-parse origin/main)) { throw 'main differs from origin/main: pull first' }
    $subject = git log -1 --format=%s
} finally {
    Pop-Location
}

$recipes = @{ graw1 = $Recipes1; graw2 = $Recipes2 }
foreach ($game in 'graw1', 'graw2') {
    & (Join-Path $PSScriptRoot 'build.ps1') -Game $game -SdkSource $SdkSource
    if ($LASTEXITCODE) { throw "$game build failed" }
    $folder = Join-Path $Out "$($game.ToUpper())-NATIVE-ADDON"
    New-Item -ItemType Directory -Force $folder | Out-Null
    $package = @{ Game = $game; Out = $folder; Runtime = $Runtime }
    if ($recipes[$game]) { $package.Recipes = $recipes[$game] }
    & (Join-Path $PSScriptRoot 'package_test.ps1') @package
    $stamp = @(
        "main $head",
        $subject,
        "built $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
    )
    [IO.File]::WriteAllLines((Join-Path $folder 'SNAPSHOT.txt'), $stamp)
}
"snapshot of main $($head.Substring(0, 7)) in $Out"
