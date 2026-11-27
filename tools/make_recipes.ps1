# Makes a recipe file from the native renderer's pipeline stores (cache-native\graw-native\pipelines.bin
# from playtests): the pipeline records only, without the shaders, merged and without duplicates. A
# recipe names its shaders by hash, so the file holds no game code; the shaders come from the
# player's own files at start-up (native/src/disc_shaders.h). Format: native/src/pipeline_store.cpp.
#
# With -Shaders (the game's NewYetiShadersX360.sdf), also vertex shader records ('V'): a drawn vertex
# shader is a disc one with its vertex fetch instructions patched for the vertex layout bound, so the
# record names the disc entry (its index in the file) and the dwords that differ (a few fetch fields
# each), and the game rebuilds the drawn shader, checked against its hash, for a complete pipeline.
#   tools/make_recipes.ps1 -Out <grawbox-recipes.bin> -Stores <pipelines.bin>... [-Shaders <.sdf>]
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Out,
    [Parameter(Mandatory)][string[]]$Stores,
    [string]$Shaders
)
$ErrorActionPreference = 'Stop'
# Relative to this shell's location, not the .NET working directory.
$Out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
public static class VertexPatches {
    static uint Be(byte[] d, int p) { return (uint)(d[p] << 24 | d[p + 1] << 16 | d[p + 2] << 8 | d[p + 3]); }
    // The .sdf's vertex shaders by entry index, as big-endian bytes (disc_shaders.cpp's layout).
    public static Dictionary<int, byte[]> DiscVertex(byte[] sdf) {
        var result = new Dictionary<int, byte[]>();
        if (sdf.Length < 24 || sdf[0] != 'S' || sdf[1] != 'D' || sdf[2] != 'F' || sdf[3] != 'X') return result;
        uint count = Be(sdf, 0x10);
        long pos = 0x18;
        for (int i = 0; i < count && pos + 12 <= sdf.Length; ++i) {
            uint size = Be(sdf, (int)pos + 4);
            int at = (int)pos + 12;
            pos = at + (long)size;
            if (pos > sdf.Length || size < 28) continue;
            uint magic = Be(sdf, at);
            if ((magic & 1) == 0) continue;  // pixel
            long offset = Be(sdf, at + 4);
            uint bytes = Be(sdf, at + 8);
            if ((magic & ~1u) == 0x102A1100u) {
                uint record = Be(sdf, at + 24);
                if (record > size - 8) continue;
                offset += Be(sdf, at + (int)record);
                bytes = Be(sdf, at + (int)record + 4);
            } else if ((magic & ~1u) != 0x102A0E00u) continue;
            if (bytes == 0 || bytes % 4 != 0 || offset + bytes > size) continue;
            var code = new byte[bytes];
            Array.Copy(sdf, at + offset, code, 0, bytes);
            result[i] = code;
        }
        return result;
    }
    // A V record per drawn vertex shader that pairs with a disc one of the same length (the fewest
    // differing dwords, at most maxDiff): tag, hash, entry index, count, then (dword index, value).
    public static byte[] Records(Dictionary<ulong, byte[]> drawn, Dictionary<int, byte[]> disc, int maxDiff,
                                 out int paired, out int dwords) {
        var output = new MemoryStream();
        var w = new BinaryWriter(output);
        paired = 0; dwords = 0;
        foreach (var kv in drawn) {
            byte[] code = kv.Value;
            int best = -1, bestDiff = int.MaxValue;
            foreach (var d in disc) {
                if (d.Value.Length != code.Length) continue;
                int diff = 0;
                for (int k = 0; k < code.Length && diff < bestDiff; k += 4)
                    if (code[k] != d.Value[k] || code[k + 1] != d.Value[k + 1] || code[k + 2] != d.Value[k + 2] || code[k + 3] != d.Value[k + 3]) ++diff;
                if (diff < bestDiff) { bestDiff = diff; best = d.Key; }
            }
            if (best < 0 || bestDiff > maxDiff) continue;
            byte[] src = disc[best];
            w.Write((uint)'V');
            w.Write(kv.Key);
            w.Write((uint)best);
            w.Write((uint)bestDiff);
            for (int k = 0; k < code.Length; k += 4) {
                if (code[k] == src[k] && code[k + 1] == src[k + 1] && code[k + 2] == src[k + 2] && code[k + 3] == src[k + 3]) continue;
                w.Write((uint)(k / 4));
                w.Write(code, k, 4);  // as in guest memory
            }
            ++paired; dwords += bestDiff;
        }
        return output.ToArray();
    }
}
'@

$header = $null
$seen = [System.Collections.Generic.HashSet[string]]::new()
$records = [System.IO.MemoryStream]::new()
$sha = [System.Security.Cryptography.SHA1]::Create()
$vertex = [System.Collections.Generic.Dictionary[uint64, byte[]]]::new()  # drawn vertex shaders by hash
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
            $type = [BitConverter]::ToUInt32($data, $pos + 4)
            $hash = [BitConverter]::ToUInt64($data, $pos + 8)
            $bytes = 4 * [BitConverter]::ToUInt32($data, $pos + 16)
            if ($pos + 20 + $bytes -gt $data.Length) { break }
            if ($type -eq 0 -and -not $vertex.ContainsKey($hash)) {  # xenos::ShaderType::kVertex
                $code = [byte[]]::new($bytes)
                [Array]::Copy($data, $pos + 20, $code, 0, $bytes)
                $vertex[$hash] = $code
            }
            $pos += 20 + $bytes
        } elseif ($tag -eq [uint32][char]'P') {
            $size = 4 + 32 + $keySize
            if ($pos + $size -gt $data.Length) { break }
            $hash = [BitConverter]::ToString($sha.ComputeHash($data, $pos, $size))
            if ($seen.Add($hash)) { $records.Write($data, $pos, $size); $count++ }
            $pos += $size
        } else { break }
    }
}
$note = ''
if ($Shaders) {
    $disc = [VertexPatches]::DiscVertex([IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Shaders)))
    $paired = 0; $dwords = 0
    $v = [VertexPatches]::Records($vertex, $disc, 32, [ref]$paired, [ref]$dwords)
    $records.Write($v, 0, $v.Length)
    $note = ", $paired of $($vertex.Count) vertex shaders paired with the game's ($dwords patched dwords)"
}
$file = [IO.File]::Create($Out)
try {
    $file.Write([byte[]]$header, 0, 16)
    $records.WriteTo($file)
} finally {
    $file.Close()
}
"make_recipes: $count recipes from $($Stores.Count) store(s)$note in $Out"
