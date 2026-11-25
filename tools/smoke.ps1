# Runs a game build unattended on the software GPU (WARP), with scripted controller input and
# window captures, then stops it. Captures and logs hold game content: use an untracked folder.
#   tools/smoke.ps1 -Exe <program> -GameRoot <folder> -Entry <executable> -RunDir <folder>
#                   [-PadScript <file>] [-Shots 20,60] [-Extra '--setting=value'] [-Adapter -2]
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$GameRoot,
    [Parameter(Mandatory)][string]$Entry,
    [Parameter(Mandatory)][string]$RunDir,
    [string]$PadScript,
    [int[]]$Shots = @(20, 60),
    [string[]]$Extra = @(),
    [int]$Adapter = -2,
    [string]$UserRoot
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $RunDir | Out-Null
$RunDir = (Resolve-Path -LiteralPath $RunDir).Path
if (-not $UserRoot) { $UserRoot = Join-Path $RunDir 'user' }

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Drawing; using System.Runtime.InteropServices;
public static class WindowCapture {
    struct Rect { public int L, T, R, B; }
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    public static string Save(IntPtr h, string file) {
        Rect r;
        if (!GetClientRect(h, out r) || r.R <= 0 || r.B <= 0) return "no window";
        using (var b = new Bitmap(r.R, r.B)) {
            using (var g = Graphics.FromImage(b)) {
                IntPtr dc = g.GetHdc(); PrintWindow(h, dc, 3); g.ReleaseHdc(dc);
            }
            b.Save(file);
        }
        return r.R + "x" + r.B;
    }
}
'@

function Format-Argument([string]$Text) { '"' + $Text + '"' }
$arguments = @(
    "--title.game_root=$GameRoot", "--title.entry=$Entry", "--app.user_root=$UserRoot",
    '--gpu.driver=d3d12', "--gpu.d3d12.adapter=$Adapter", '--ui.fullscreen=false',
    '--ui.resolution=1280x720', "--log.file_name=$RunDir\run.log"
) + $Extra
if ($PadScript) {
    $env:GRAWBOX_PAD_SCRIPT = (Resolve-Path -LiteralPath $PadScript).Path
    $arguments += '--hid.drivers=autopilot'
}
$process = Start-Process $Exe -ArgumentList ($arguments | ForEach-Object { Format-Argument $_ }) `
    -PassThru -WorkingDirectory $RunDir
$null = $process.Handle
$clock = [Diagnostics.Stopwatch]::StartNew()
try {
    foreach ($at in $Shots) {
        while ($clock.Elapsed.TotalSeconds -lt $at -and -not $process.HasExited) {
            Start-Sleep -Milliseconds 200
        }
        if ($process.HasExited) { break }
        $process.Refresh()
        $size = [WindowCapture]::Save($process.MainWindowHandle, "$RunDir\shot$at.png")
        "t=$at capture=$size responding=$($process.Responding)"
    }
    if ($process.HasExited) {
        'exited early, code 0x{0:X8}, after {1} s' -f $process.ExitCode, [int]$clock.Elapsed.TotalSeconds
    } else {
        "still running after $([int]$clock.Elapsed.TotalSeconds) s"
    }
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -Confirm:$false }
    Remove-Item Env:\GRAWBOX_PAD_SCRIPT -ErrorAction SilentlyContinue
}
