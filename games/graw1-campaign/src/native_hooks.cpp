// The GRAW 1 campaign's Direct3D (title update 0.0.2.1; docs/RENDERER.md). The device layout is the
// unpatched game's, as graw360-recomp documents it: the functions below use the same offsets.
#include "generated/campaign/graw1_campaign_init.h"

#include "native_host.h"

#include <rex/ppc/hook.h>

const GrawboxTitle grawbox::kTitle = {
    .size = sizeof(GrawboxTitle),
    .push = 0,
    .dirty_masks = 16,
    .dirty_mask_count = 5,
    // VS constants, PS constants, fetch/bool/0x2000, 0x2100..0x2280, 0x2300/0x2380/shaders
    .dirty_bits = {~0ull, ~0ull, 0xFFFFFFFF00000000ull | 0x80000000ull | 0x3FFFC000ull,
                   0xFFFFF80000000000ull | 0x7C000000000ull | 0x3FFC000000ull | 0x3FFFFE0ull,
                   0xFFFFFFFFFC000000ull | 0x3FC0000ull | 0x400ull | 0x1E0ull},
    .shader_load_bits = 0x1E0,
    .fetch_constants = 1152,
    .vs_constants = 1920,
    .ps_constants = 6016,
    .bool_loop = 10112,
    .ring_read_ptr = 10384,
    .flags = 10432,
    .shadow_2000 = 11456,
    .shadow_2100 = 11532,
    .shadow_2180 = 11616,
    .shadow_2200 = 11636,
    .shadow_2280 = 11684,
    .shadow_2300 = 11768,
    .shadow_2380 = 11920,
    .scissor_tl = 11524,
    .scissor_br = 11528,
    .index_buffer = 12532,
    .color_surfaces = 12536,
    .depth_surface = 12552,
    .vertex_stride = 12688,
    .tile_count = 12996,
    .tile_rects = 13000,
    .tile_origins = 13240,
    .tile_origin_stride = 16,
    .pending_push = 13652,
    .texture_fetch = 16,
    .index_buffer_format = 0,
    .index_buffer_address = 12,
};

GRAWBOX_HOOK(824C45C0, 1);   // BeginVertices
GRAWBOX_HOOK(824C4C20, 2);   // EndVertices
GRAWBOX_HOOK(824C4860, 3);   // DrawVerticesUP
GRAWBOX_HOOK(824C48E8, 4);   // BeginIndexedVertices
GRAWBOX_HOOK(824C4C40, 5);   // DrawVertices
GRAWBOX_HOOK(824C4E80, 6);   // DrawIndexedVertices
GRAWBOX_HOOK(824CD5E0, 7);   // ClearF
GRAWBOX_HOOK(824CDD10, 8);   // Clear
GRAWBOX_HOOK(824D19A8, 9);   // Resolve
GRAWBOX_HOOK(824CCA10, 10);  // Swap
GRAWBOX_HOOK(824D28B0, 11);  // BeginTiling
GRAWBOX_HOOK(824D2CE8, 12);  // EndTiling
GRAWBOX_HOOK(824D7EC0, 13);  // MakeSpace
GRAWBOX_HOOK(824D7C48, 14);  // wraps the push buffer
GRAWBOX_HOOK(824D6FC8, 15);  // restores a push buffer segment
