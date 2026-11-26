# Downloads the launcher's font, Rajdhani (SIL Open Font License 1.1, Indian Type Foundry), into fonts\, checking each
# file against its SHA-256. The files are not in the repository; the build embeds them.
$ErrorActionPreference = 'Stop'
$dir = Join-Path $PSScriptRoot 'fonts'
New-Item -ItemType Directory -Force $dir | Out-Null
$files = @{
    'Rajdhani-SemiBold.ttf' = '94bbd25a18ca665999feb05a537de9fd2b860dcfb78bbe9ca00270825bf235da'
    'Rajdhani-Bold.ttf'     = '691470dd3286a14e9677940d0bf75796179841ba5215cbda1a2c8910a3226afd'
    'OFL.txt'               = 'f62ef357d3a1c3d27edd35a6e1ba350e8a8d13499797964eeadefbf0b3b15d1f'
}
foreach ($name in $files.Keys) {
    $path = Join-Path $dir $name
    if (-not (Test-Path $path) -or (Get-FileHash $path -Algorithm SHA256).Hash -ne $files[$name]) {
        Invoke-WebRequest "https://github.com/google/fonts/raw/main/ofl/rajdhani/$name" -OutFile $path -UseBasicParsing
    }
    if ((Get-FileHash $path -Algorithm SHA256).Hash -ne $files[$name]) {
        Remove-Item $path
        throw "$name does not match its checksum"
    }
}
