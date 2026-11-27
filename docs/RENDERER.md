# Native renderer

Goal: draw each game draw once, at any resolution, from the games' own Direct3D calls, instead of
emulating the console GPU's command stream and its strips. Both renderers stay selectable, with a key
to switch while playing.

## Design

- `native/` is graw360-recomp's renderer (MIT), with the 1x to 3x drawing merged from fear360-recomp's copy of it
  (MIT). No project's GPL folders are used. D3D12 only for now; the Vulkan backend is not brought over yet.
- It is `grawbox_native.dll`, built with the shader translator of ReXGlue `bd833a2` (`native/fetch.ps1`), so that SDK
  and 0.11 never share symbols. The C interface is `native/include/grawbox_native.h`; the host side is
  `games/common/`; each game has `src/native_hooks.cpp` with its addresses and device layout.
- The game's own function always runs first. While native draws, the emulated GPU still runs packets (fences,
  interrupts) without drawing or presenting (`patches/rexglue-0.11.0-host-renderer.patch`).
- It presents into a child window over the game window, because a window takes one flip swap chain.
- `native.renderer` = `native` (default), `emulated` or `off`. **F9** switches while playing.
  `native.resolution_scale` = 1 to 3. `native.vsync`. `native.anisotropy` = 1 to 16. `native.haze` = fixed (default), original, off. `native.shader_wait` (default true): wait up to 0.5 s for new shaders; false shows the frame without them.
  `native.haze_variant` = 1 (the games' motion) to 6 (default 3, a drift).
  `native.ambient_occlusion` = 0 to 100 (default 0). `native.sharpness` = 0 to 1. `native.mip_bias` = -2 to 2.
- `native/src/post.cpp`, written for this project: just before the games' own haze draw it replaces the scene with the same
  haze (same weights by depth and the same moving displacement), without the double image and with the blurred copy weighted
  by distance, so near objects do not bleed; plus ambient occlusion from the games' depth and sharpening. HUD untouched.
- Adds over the sources it came from: `k_CTX1` textures and resolves to `k_16_16`, which GRAW 2 uses.

## Limits known

- A scaled resolve is used only by a texture of exactly its size, or by one whose every mip level is a resolve (GRAW 2's
  shadow atlas); others get the 1x copy.
- No occlusion queries (a fixed count is reported).
- System dialogs need the SDK's presenter: the child window hides while one is open. Not tested.

## Guest Direct3D functions

`tools/match_functions.py` against graw360's addresses in the unpatched GRAW 1. GRAW 1 entries match
exactly, or by instruction shape (marked s); the s entries differ only in addresses of data, so GRAW 1's device layout
is the unpatched one. GRAW 2 entries are the closest match (x: exactly or by shape), read in its code and in use.

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
