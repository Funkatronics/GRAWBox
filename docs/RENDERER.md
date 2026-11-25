# Native renderer

Goal: draw each game draw once, at any resolution, from the games' own Direct3D calls, instead of
emulating the console GPU's command stream and its strips. Both renderers stay selectable, with a key
to switch while playing.

## Base

- fear360-recomp `port-nx/src/d3d12` (MIT, with ReXGlue and Xenia BSD parts): the pgr4-native and graw360
  lineage, D3D12 and Vulkan, with 1x to 3x resolution scale on D3D12. Front end: graw360-recomp `native/`
  (MIT). Neither project's GPL folders are used.
- Those sources are written for ReXGlue `bd833a2`; the games run on 0.11. Planned shape: the renderer as its own
  library with a small C interface (guest memory, window, hook arguments, log), holding the SDK's shader
  translator sources it needs, so the two SDK versions never share symbols.

## Guest Direct3D functions

`tools/match_functions.py` against graw360's addresses in the unpatched GRAW 1. GRAW 1 entries match
exactly, or by instruction shape (marked s). GRAW 2 entries are the closest match only and must be read
before use, except where marked x (matches exactly or by shape).

| Function | GRAW 1 0.0.2.1 | GRAW 2 0.0.4.1 |
|---|---|---|
| BeginVertices | 824C45C0 | 825A2398 |
| EndVertices | 824C4C20 | 825A2DC8 x |
| DrawVerticesUP | 824C4860 | 825A2838 |
| BeginIndexedVertices | 824C48E8 | 825A2898 |
| DrawVertices | 824C4C40 s | 825A2DE8 |
| DrawIndexedVertices | 824C4E80 s | 825A31D0 |
| ClearF | 824CD5E0 s | 825A0228 |
| Clear | 824CDD10 s | 825A09E0 |
| Resolve | 824D19A8 s | 825A3D08 |
| Swap | 824CCA10 | 825A1250 |
| BeginTiling | 824D28B0 | 825A1958 |
| EndTiling | 824D2CE8 s | 825A1EE0 |
| MakeSpace | 824D7EC0 | 825A6AC0 x |
| WrapSegment | 824D7C48 | 825A67C0 |
| RestoreSegment | 824D6FC8 | 825A5778 x |
| FlushState | 824D9ED8 | inline in the draws, probably |
| SetScissorRect | 824D0408 | 8259E7A8 |
| SetDepthStencilSurface | 824D1000 | 8259F490 |
