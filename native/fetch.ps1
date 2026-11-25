# Gets the parts of ReXGlue SDK bd833a2 the native renderer is built with.
#   native/fetch.ps1 -Sdk <rexglue-sdk clone>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Sdk)
$ErrorActionPreference = 'Stop'
$out = Join-Path (Split-Path $PSScriptRoot -Parent) '.tools\rexglue-bd833a2'
New-Item -ItemType Directory -Force $out | Out-Null
$tar = "$out.tar"
git -C $Sdk archive --format=tar -o $tar bd833a2 include src/graphics src/core thirdparty/dxbc LICENSE
if ($LASTEXITCODE) { exit 1 }
& "$env:SystemRoot\System32\tar.exe" -xf $tar -C $out
Remove-Item $tar
# git apply needs a repository of its own here, or it uses the outer one's paths.
git init -q $out
foreach ($name in '0021*', '0022*', '*switch*') {
    $patch = Get-ChildItem "$PSScriptRoot\patches" -Filter $name
    git -C $out apply --ignore-whitespace $patch.FullName
    if ($LASTEXITCODE) { exit 1 }
}
