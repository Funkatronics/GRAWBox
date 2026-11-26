# Builds a GRAWBox release from a clean checkout and, with -Publish, tags it and uploads it to GitHub.
#   tools/release.ps1 -Version 0.1.0 -SdkSource <rexglue-sdk clone> [-Notes <file>] [-SkipBuild] [-Publish] [-Prerelease]
# Needs everything tools/build.ps1 needs (Visual Studio C++ tools, .tools/, the owner's extracted files in
# private/<game>/), Rust (cargo), and for -Publish the GitHub CLI (gh), logged in. See docs/RELEASING.md.
# The zip holds GRAWBox.exe and the built programs (with their recompiled code) and never any game file.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$SdkSource,
    [string]$Notes,
    [switch]$SkipBuild,
    [switch]$Publish,
    [switch]$Prerelease
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$tag = "v$Version"
$name = "GRAWBox-$tag-windows-x64"
$out = Join-Path $root "private\release\$tag"
$sdk = Join-Path $root '.tools\rexglue-grawbox'
Set-Location $root

function Fail($message) { Write-Host "release: $message" -ForegroundColor Red; exit 1 }
function Step($message) { Write-Host "release: $message" -ForegroundColor Cyan }

# 1. Clean tree: the release is what the tag names.
if (git status --porcelain --untracked-files=no) { Fail 'the working tree has changes; commit them first' }
if ($Publish) {
    if (git tag --list $tag) { Fail "tag $tag exists" }
    git fetch -q origin
    $branch = git rev-parse --abbrev-ref HEAD
    if ((git rev-parse HEAD) -ne (git rev-parse "origin/$branch")) { Fail "push $branch first" }
}

# 2. Build: both programs with the native renderer, and the launcher (tested first).
if (-not $SkipBuild) {
    if (-not $SdkSource) { Fail '-SdkSource is needed to build the native renderer' }
    foreach ($game in 'graw1', 'graw2') {
        Step "building $game"
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\build.ps1') -Game $game -SdkSource $SdkSource
        if ($LASTEXITCODE) { Fail "$game build failed" }
    }
    Step 'building the launcher'
    & (Join-Path $root 'launcher\fetch_fonts.ps1')
    if (-not $env:CARGO_TARGET_DIR) { $env:CARGO_TARGET_DIR = Join-Path $root '.tools\launcher-target' }
    Push-Location (Join-Path $root 'launcher')
    cargo test --release --locked
    if ($LASTEXITCODE) { Pop-Location; Fail 'launcher tests failed' }
    cargo build --release --locked
    if ($LASTEXITCODE) { Pop-Location; Fail 'launcher build failed' }
    Pop-Location
}
$target = if ($env:CARGO_TARGET_DIR) { $env:CARGO_TARGET_DIR } else { Join-Path $root '.tools\launcher-target' }

# 3. Stage the portable layout: GRAWBox.exe beside programs\<game>\.
Step "staging $name"
if (Test-Path $out) { Remove-Item $out -Recurse -Force -Confirm:$false }
$stage = Join-Path $out 'GRAWBox'
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item (Join-Path $target 'release\GRAWBox.exe') $stage
Copy-Item LICENSE, README.md $stage
Copy-Item launcher\fonts\OFL.txt (Join-Path $stage 'OFL-Rajdhani.txt')
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$crt = Get-ChildItem "$(& $vswhere -latest -property installationPath)\VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT" -Directory |
    Sort-Object FullName | Select-Object -Last 1
if (-not $crt) { Fail 'no Visual C++ redistributable files found' }
foreach ($game in 'graw1', 'graw2') {
    $dir = Join-Path $stage "programs\$game"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $build = Join-Path $root "games\$game-campaign\out\build\Release"
    # The program and its own modules from the build; the SDK's runtime straight from the SDK, so a
    # library copied into the build folder by hand never ships.
    Get-ChildItem $build -File | Where-Object {
        $_.Name -eq "${game}_campaign.exe" -or $_.Name -like "${game}_campaign_*.dll" -or $_.Name -eq 'grawbox_native.dll'
    } | Copy-Item -Destination $dir
    Get-ChildItem (Join-Path $sdk 'bin') -File -Filter *.dll | Where-Object { $_.Name -notlike 'rexhid-autopilot*' } |
        Copy-Item -Destination $dir
    Get-ChildItem $crt.FullName -File -Filter *.dll | Copy-Item -Destination $dir
    Copy-Item input\grawbox-keys*.txt $dir
    foreach ($needed in "${game}_campaign.exe", 'grawbox_native.dll', 'rexkernel.dll', 'vcruntime140.dll') {
        if (-not (Test-Path (Join-Path $dir $needed))) { Fail "$needed is missing from programs\$game" }
    }
}

# 4. Nothing of the games' own: no game executables, updates, archives or disc images, and only the
# kinds of files the layout above makes.
$bad = Get-ChildItem $stage -Recurse -File | Where-Object {
    $_.Name -match '^(Yeti|AI_X360)' -or $_.Extension -notin '.exe', '.dll', '.txt', '.md' -and $_.Name -ne 'LICENSE'
}
if ($bad) { Fail "unexpected files: $($bad.FullName -join ', ')" }
$exes = Get-ChildItem $stage -Recurse -File -Filter *.exe | ForEach-Object Name
if (Compare-Object $exes @('GRAWBox.exe', 'graw1_campaign.exe', 'graw2_campaign.exe')) { Fail "unexpected programs: $exes" }

# 5. Zip and checksum.
$zip = Join-Path $out "$name.zip"
Compress-Archive $stage $zip
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
Set-Content (Join-Path $out "$name.zip.sha256") "$hash  $name.zip" -Encoding ascii
Step "$zip ($([math]::Round((Get-Item $zip).Length / 1MB)) MB), sha256 $hash"

# 6. Publish: tag the commit and upload the zip to a GitHub release.
if (-not $Publish) { Step 'not published (-Publish to tag and upload)'; exit 0 }
if (-not $Notes) { Fail '-Notes <file> is needed to publish' }
git tag -a $tag -m "GRAWBox $tag"
if ($LASTEXITCODE) { Fail 'tagging failed' }
git push -q origin $tag
if ($LASTEXITCODE) { Fail 'pushing the tag failed' }
$flags = @('--title', "GRAWBox $tag", '--notes-file', $Notes, '--verify-tag')
if ($Prerelease) { $flags += '--prerelease' }
gh release create $tag $zip (Join-Path $out "$name.zip.sha256") @flags
if ($LASTEXITCODE) { Fail 'gh release create failed' }
Step "published $tag"
