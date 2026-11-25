# Translates and builds a game program from the owner's extracted files.
#   tools/build.ps1 -Game graw1|graw2 [-Config Release|RelWithDebInfo|Debug] [-Sdk <folder>]
# Needs Visual Studio C++ tools, the portable tools in .tools/, and the disc and title update
# extracted with tools/x360_extract.py to private/<game>/disc and private/<game>/update.
# The default SDK is .tools/rexglue-grawbox: v0.11.0 with patches/ applied, Release only.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('graw1', 'graw2')][string]$Game,
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')][string]$Config = 'Release',
    [string]$Sdk
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$tools = Join-Path $root '.tools'
$name = "${Game}_campaign"
$project = Join-Path $root "games\$Game-campaign"
$stage = Join-Path $root "private\$Game\campaign"
if (-not $Sdk) { $Sdk = Join-Path $tools 'rexglue-grawbox' }
$files = @{
    graw1 = @('Yeti_Final_Xenon.exe')
    graw2 = @('Yeti_Final_Xenon.xex', 'AI_X360_F.xex')
}[$Game]
$modules = @{ graw1 = @(); graw2 = @('--target', 'AI_X360_F') }[$Game]

# Codegen applies the update when it sits beside the executable.
New-Item -ItemType Directory -Force $stage | Out-Null
foreach ($file in $files) {
    Copy-Item -LiteralPath (Join-Path $root "private\$Game\disc\$file") $stage -Force
    Copy-Item -LiteralPath (Join-Path $root "private\$Game\update\${file}p") $stage -Force
}

$llvm = Join-Path $tools 'llvm-22.1.8\bin'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -property installationPath
$ErrorActionPreference = 'Continue'
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$env:PATH = "$llvm;$tools\cmake\bin;$tools\ninja;$Sdk\bin;$env:PATH"

# The generated sources must exist before CMake configures, or they are left out of the program.
& "$Sdk\bin\rexglue.exe" codegen (Join-Path $project "${name}_manifest.toml") @modules
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$build = Join-Path $project 'out\build'
cmake -S $project -B $build -G 'Ninja Multi-Config' "-DCMAKE_CXX_COMPILER=$llvm\clang++.exe" `
    "-DCMAKE_PREFIX_PATH=$Sdk"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --config $Config
exit $LASTEXITCODE
