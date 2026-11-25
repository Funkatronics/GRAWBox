// The GRAW 2 campaign's Direct3D (title update 0.0.4.1; docs/RENDERER.md). The device layout is read
// from the functions below. The register blocks, constants and masks sit where fear360-recomp's
// port documents them for F.E.A.R.; the push cursor, bound objects and tiles do not.
#include "generated/campaign/graw2_campaign_init.h"

#include "native_host.h"

#include <rex/ppc/hook.h>

const GrawboxTitle grawbox::kTitle = {
    .size = sizeof(GrawboxTitle),
    .push = 48,
    .dirty_masks = 0,
    .dirty_mask_count = 5,
    // VS constants; PS constants; shaders, 0x2180, 0x2200, 0x2000, 0x2100; fetch, 0x2280; 0x2300,
    // 0x2380, clip planes, bool/loop
    .dirty_bits = {~0ull, ~0ull,
                   0x1E0000ull | 0x1F000ull | 0xFFFull | 0x03FFFC0000000000ull | 0x3FFFFE00000ull,
                   0xFFFFFFFFull | 0x007FFFFC00000000ull,
                   0x3FFFFFFFFFull | 0x3FC000000000ull | 0x7E000000000000ull | 0x100000000000000ull},
    .shader_load_bits = 0x1E0000,
    .fetch_constants = 1024,
    .vs_constants = 1792,
    .ps_constants = 5888,
    .bool_loop = 9984,
    .ring_read_ptr = 10768,
    .flags = 10812,
    .shadow_2000 = 10240,
    .shadow_2100 = 10316,
    .shadow_2180 = 10400,
    .shadow_2200 = 10420,
    .shadow_2280 = 10468,
    .shadow_2300 = 10552,
    .shadow_2380 = 10704,
    .scissor_tl = 10308,
    .scissor_br = 10312,
    .index_buffer = 12300,
    .color_surfaces = 12304,
    .depth_surface = 12320,
    .vertex_stride = 12392,
    .tile_count = 12612,
    .tile_rects = 12616,
    .tile_origins = 12856,
    .tile_origin_stride = 8,
    .pending_push = 13300,
    .texture_fetch = 28,
    .index_buffer_format = 0,
    .index_buffer_address = 24,
};

GRAWBOX_HOOK(825A2398, 1);   // BeginVertices
GRAWBOX_HOOK(825A2DC8, 2);   // EndVertices
GRAWBOX_HOOK(825A2838, 3);   // DrawVerticesUP
GRAWBOX_HOOK(825A2898, 4);   // BeginIndexedVertices
GRAWBOX_HOOK(825A2DE8, 5);   // DrawVertices
GRAWBOX_HOOK(825A31D0, 6);   // DrawIndexedVertices
GRAWBOX_HOOK(825A0228, 7);   // ClearF
GRAWBOX_HOOK(825A09E0, 8);   // Clear
GRAWBOX_HOOK(825A3D08, 9);   // Resolve
GRAWBOX_HOOK(825A1250, 10);  // Swap
GRAWBOX_HOOK(825A1958, 11);  // BeginTiling
GRAWBOX_HOOK(825A1EE0, 12);  // EndTiling
GRAWBOX_HOOK(825A6AC0, 13);  // MakeSpace
GRAWBOX_HOOK(825A67C0, 14);  // wraps the push buffer
GRAWBOX_HOOK(825A5778, 15);  // restores a push buffer segment
