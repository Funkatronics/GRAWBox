# Translates and builds a game program from the owner's extracted files.
#   tools/build.ps1 [-Config Release|RelWithDebInfo|Debug]
# Needs Visual Studio C++ tools, the portable tools in .tools/, and the disc and title update
# extracted with tools/x360_extract.py to private/graw1/disc and private/graw1/update.
[CmdletBinding()]
param([ValidateSet('Release', 'RelWithDebInfo', 'Debug')][string]$Config = 'RelWithDebInfo')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$tools = Join-Path $root '.tools'
$project = Join-Path $root 'games\graw1-campaign'
$stage = Join-Path $root 'private\graw1\campaign'

# Codegen applies the update when it sits beside the executable.
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'private\graw1\disc\Yeti_Final_Xenon.exe') $stage -Force
Copy-Item -LiteralPath (Join-Path $root 'private\graw1\update\Yeti_Final_Xenon.exep') $stage -Force

$sdk = Join-Path $tools 'rexglue-0.11.0\win-amd64'
$llvm = Join-Path $tools 'llvm-22.1.8\bin'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -property installationPath
$ErrorActionPreference = 'Continue'
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$env:PATH = "$llvm;$tools\cmake\bin;$tools\ninja;$sdk\bin;$env:PATH"

# The generated sources must exist before CMake configures, or they are left out of the program.
& "$sdk\bin\rexglue.exe" codegen (Join-Path $project 'graw1_campaign_manifest.toml')
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$build = Join-Path $project 'out\build'
cmake -S $project -B $build -G 'Ninja Multi-Config' "-DCMAKE_CXX_COMPILER=$llvm\clang++.exe" `
    "-DCMAKE_PREFIX_PATH=$sdk"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --config $Config
exit $LASTEXITCODE
