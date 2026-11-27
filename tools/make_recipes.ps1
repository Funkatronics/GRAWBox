# Makes a recipe file from the native renderer's pipeline stores (cache-native\graw-native\pipelines.bin
# from playtests): the pipeline records only, without the shaders, merged and without duplicates. A
# recipe names its shaders by hash, so the file holds no game code; the shaders come from the
# player's own files at start-up (native/src/disc_shaders.h). Format: native/src/pipeline_store.cpp.
#   tools/make_recipes.ps1 -Out <grawbox-recipes.bin> -Stores <pipelines.bin>...
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Out,
    [Parameter(Mandatory)][string[]]$Stores
)
$ErrorActionPreference = 'Stop'
$header = $null
$seen = [System.Collections.Generic.HashSet[string]]::new()
$records = [System.IO.MemoryStream]::new()
$sha = [System.Security.Cryptography.SHA1]::Create()
$count = 0
foreach ($store in $Stores) {
    $data = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $store))
    if ($data.Length -lt 16 -or [Text.Encoding]::ASCII.GetString($data, 0, 4) -ne 'P4PS') { throw "$store is not a pipeline store" }
    $keySize = [BitConverter]::ToUInt32($data, 8)
    if ($null -eq $header) { $header = $data[0..15] }
    elseif ([BitConverter]::ToString($data, 4, 12) -ne [BitConverter]::ToString([byte[]]$header, 4, 12)) {
        throw "$store has another format, key size or key version than the first store"
    }
    $pos = 16
    while ($pos + 4 -le $data.Length) {
        $tag = [BitConverter]::ToUInt32($data, $pos)
        if ($tag -eq [uint32][char]'S') {
            if ($pos + 20 -gt $data.Length) { break }
            $pos += 20 + 4 * [BitConverter]::ToUInt32($data, $pos + 16)
        } elseif ($tag -eq [uint32][char]'P') {
            $size = 4 + 32 + $keySize
            if ($pos + $size -gt $data.Length) { break }
            $hash = [BitConverter]::ToString($sha.ComputeHash($data, $pos, $size))
            if ($seen.Add($hash)) { $records.Write($data, $pos, $size); $count++ }
            $pos += $size
        } else { break }
    }
}
$file = [IO.File]::Create($Out)
$file.Write([byte[]]$header, 0, 16)
$records.WriteTo($file)
$file.Close()
"make_recipes: $count recipes from $($Stores.Count) store(s) in $Out"
