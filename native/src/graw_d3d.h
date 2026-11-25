// The titles' Xbox 360 D3D: the functions the hooks report, and where each title's device and
// objects keep what the renderer reads (grawbox_native.h, GrawboxTitle; the host gives the values).
// How graw360-recomp found them for the unpatched GRAW 1 campaign is in its docs/NATIVE_RENDERER.md.
#pragma once

#include "host.h"

namespace graw::native {

namespace fn {
constexpr uint32_t kBeginVertices = 1;
constexpr uint32_t kEndVertices = 2;
constexpr uint32_t kDrawVerticesUP = 3;
constexpr uint32_t kBeginIndexedVertices = 4;
constexpr uint32_t kDrawVertices = 5;
constexpr uint32_t kDrawIndexedVertices = 6;
constexpr uint32_t kClearF = 7;
constexpr uint32_t kClear = 8;
constexpr uint32_t kResolve = 9;
constexpr uint32_t kSwap = 10;
constexpr uint32_t kBeginTiling = 11;
constexpr uint32_t kEndTiling = 12;
constexpr uint32_t kMakeSpace = 13;
constexpr uint32_t kWrapSegment = 14;
constexpr uint32_t kRestoreSegment = 15;
constexpr uint32_t kFlushState = 16;
}  // namespace fn

namespace dev {
inline const uint32_t& kPush = g_title.push;
inline const uint32_t& kDirtyMasks = g_title.dirty_masks;
inline const uint32_t& kFetchConstants = g_title.fetch_constants;
inline const uint32_t& kVsConstants = g_title.vs_constants;
inline const uint32_t& kPsConstants = g_title.ps_constants;
inline const uint32_t& kBoolLoop = g_title.bool_loop;
inline const uint32_t& kRingReadPtr = g_title.ring_read_ptr;
inline const uint32_t& kFlags = g_title.flags;
inline const uint32_t& kShadow2000 = g_title.shadow_2000;
inline const uint32_t& kShadow2100 = g_title.shadow_2100;
inline const uint32_t& kShadow2180 = g_title.shadow_2180;
inline const uint32_t& kShadow2200 = g_title.shadow_2200;
inline const uint32_t& kShadow2280 = g_title.shadow_2280;
inline const uint32_t& kShadow2300 = g_title.shadow_2300;
inline const uint32_t& kShadow2380 = g_title.shadow_2380;
inline const uint32_t& kScissorTL = g_title.scissor_tl;
inline const uint32_t& kScissorBR = g_title.scissor_br;
inline const uint32_t& kIndexBuffer = g_title.index_buffer;
inline const uint32_t& kColorSurfaces = g_title.color_surfaces;
inline const uint32_t& kDepthSurface = g_title.depth_surface;
inline const uint32_t& kVertexStride = g_title.vertex_stride;
inline const uint32_t& kTileCount = g_title.tile_count;
inline const uint32_t& kTileRects = g_title.tile_rects;
inline const uint32_t& kTileOrigins = g_title.tile_origins;
inline const uint32_t& kPendingPush = g_title.pending_push;
}  // namespace dev

inline const uint64_t (&kDirtyBits)[6] = g_title.dirty_bits;
inline const uint64_t& kShaderLoadBits = g_title.shader_load_bits;
inline const uint32_t& kTextureFetchOffset = g_title.texture_fetch;
inline const uint32_t& kIndexBufferFormat = g_title.index_buffer_format;
inline const uint32_t& kIndexBufferAddress = g_title.index_buffer_address;

}  // namespace graw::native
