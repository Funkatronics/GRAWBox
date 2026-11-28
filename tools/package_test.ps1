# Makes a private add-on folder for real-GPU tests of the native renderer. Copied over an existing test
# folder it only adds files: bin-native\ and PLAY-NATIVE-*.bat. Programs hold translated game code:
# the output folder must stay untracked and is not for sharing.
#   tools/package_test.ps1 -Game graw1|graw2 -Out <folder> -Runtime <folder with the C++ runtime DLLs>
#                          [-Recipes <recipe file from tools/make_recipes.ps1>]
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('graw1', 'graw2')][string]$Game,
    [Parameter(Mandatory)][string]$Out,
    [string]$Runtime,
    [string]$Recipes
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$release = Join-Path $root "games\$Game-campaign\out\build\Release"
$entry = @{ graw1 = 'Yeti_Final_Xenon.exe'; graw2 = 'Yeti_Final_Xenon.xex' }[$Game]
$update = @{ graw1 = 'Yeti_Final_Xenon.exep'; graw2 = 'AI_X360_F.xexp' }[$Game]

$bin = Join-Path $Out 'bin-native'
if (Test-Path $bin) { Remove-Item $bin -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force $bin | Out-Null
Get-ChildItem $release -File | Where-Object {
    $_.Extension -in '.exe', '.dll' -and $_.Name -ne 'rexhid-autopilot.dll'
} | Copy-Item -Destination $bin
if ($Runtime) {
    Get-ChildItem $Runtime -File | Where-Object { $_.Name -match '^(msvcp|vcruntime|concrt|vccorlib)' } |
        Copy-Item -Destination $bin
}
Copy-Item (Join-Path $root 'input\grawbox-keys*.txt') $bin
if ($Recipes) { Copy-Item -LiteralPath $Recipes (Join-Path $bin 'grawbox-recipes.bin') }
foreach ($needed in "${Game}_campaign.exe", 'grawbox_native.dll', 'rexgpu-xenos-d3d12.dll') {
    if (-not (Test-Path (Join-Path $bin $needed))) { throw "$needed is missing from $release" }
}

# name, renderer at start, native scale, extra
$scripts = @(
    @('NATIVE-720P', 'native', 1, ''),
    @('NATIVE-1440P', 'native', 2, ''),
    @('NATIVE-4K', 'native', 3, ''),
    @('NATIVE-4K-AO', 'native', 3, '--native.ambient_occlusion=80'),
    @('NATIVE-4K-AO-SHARP', 'native', 3, '--native.ambient_occlusion=80 --native.sharpness=0.5'),
    @('NATIVE-4K-HAZE-TUNING', 'native', 3, '--native.haze_tuning=true --native.ambient_occlusion=80 --native.sharpness=0.5'),
    @('CAPTURE-F12', 'native', 3, '', 'set GRAWBOX_NATIVE_CAPTURE_DIR=%~dp0captures& set GRAWBOX_NATIVE_CAPTURE_XENOS=false& set GRAWBOX_NATIVE_CAPTURE_BURST=6& set GRAWBOX_NATIVE_MARK_AUDIT=3& if not exist captures mkdir captures'),
    @('NATIVE-4K-ORIGINAL-HAZE', 'native', 3, '--native.haze=original'),
    @('EMULATED-THEN-F9', 'emulated', 3, ''),
    @('NATIVE-4K-KEYBOARD', 'native', 3, '--hid.drivers=keyboard,sdl3 --hid.shared_pads=true'),
    # Stutter: a row per frame (frame time, pipelines created and waited for, uploads) in
    # frames.csv beside native.log (frames.1.csv and so on in later runs), to tell shader compiles
    # from other hitches.
    @('NATIVE-4K-STUTTER-LOG', 'native', 3, '', 'set GRAWBOX_NATIVE_CSV=%~dp0frames.csv'),
    # The same, not waiting for new shaders (--native.shader_wait=false).
    @('NATIVE-4K-STUTTER-LOG-NO-SHADER-WAIT', 'native', 3, '--native.shader_wait=false', 'set GRAWBOX_NATIVE_CSV=%~dp0frames-nowait.csv')
)
# Field of view and aspect ratio (games/*/src/camera.cpp).
$scripts += @(
    @('NATIVE-4K-FOV-130', 'native', 3, '--native.field_of_view=130'),
    @('NATIVE-4K-FOV-85', 'native', 3, '--native.field_of_view=85'),
    @('NATIVE-4K-WIDE-DISPLAY', 'native', 3, '--native.aspect_ratio=display'),
    @('NATIVE-4K-WIDE-21x9', 'native', 3, '--native.aspect_ratio=21:9'),
    @('NATIVE-4K-WIDE-DISPLAY-HUD-STRETCHED', 'native', 3, '--native.aspect_ratio=display --native.hud_layout=stretched'),
    @('NATIVE-4K-WIDE-DISPLAY-HUD-CENTERED', 'native', 3, '--native.aspect_ratio=display --native.hud_layout=centered'),
    @('NATIVE-4K-WIDE-DISPLAY-FOV-115', 'native', 3, '--native.aspect_ratio=display --native.field_of_view=115')
)
foreach ($s in $scripts) {
    $lines = @(
        '@echo off', 'cd /d "%~dp0"',
        "if not exist `"game\$update`" (", "  echo Game files with the title update are missing from the `"game`" folder.",
        '  pause', '  exit /b 1', ')',
        "$($s[4])",
        ("start `"`" `"bin-native\${Game}_campaign.exe`" `"--title.game_root=%~dp0game`" " +
            "--title.entry=$entry `"--app.user_root=%~dp0user`" `"--app.cache_root=%~dp0cache-native`" " +
            "`"--log.file_name=%~dp0native.log`" --gpu.driver=d3d12 $(if ($s[3] -notmatch 'hid.drivers') { '--hid.drivers=sdl3,xinput ' })--kernel.headless=true " +
            '--ui.video_mode_width=1280 --ui.video_mode_height=720 --ui.fullscreen=true ' +
            "--native.renderer=$($s[1]) --native.resolution_scale=$($s[2]) " +
            "--gpu.resolution_scale_x=$($s[2]) --gpu.resolution_scale_y=$($s[2]) $($s[3])").TrimEnd()
    )
    [IO.File]::WriteAllLines((Join-Path $Out "PLAY-$($s[0]).bat"), $lines)
}
"$Game`: $((Get-ChildItem $bin).Count) files in bin-native, $($scripts.Count) scripts"
