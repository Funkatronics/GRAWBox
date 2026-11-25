// The native renderer's interface to the program that hosts it. Plain C types only: the renderer is
// a library of its own, built against its own copy of the SDK's shader translator.
#pragma once

#include <cstdint>

#if defined(GRAWBOX_NATIVE_BUILD)
#define GRAWBOX_NATIVE_API extern "C" __declspec(dllexport)
#else
#define GRAWBOX_NATIVE_API extern "C" __declspec(dllimport)
#endif

// A physical range the guest is about to write: returns the range that no longer needs a watch,
// start in the low half and length in the high half.
using GrawboxWriteCallback = uint64_t (*)(void* context, uint32_t start, uint32_t length, bool exact);

struct GrawboxHost {
  uint32_t size;  // sizeof(GrawboxHost)
  uint8_t* virtual_base;
  uint8_t* physical_base;
  void (*log)(int level, const char* text);  // 0 info, 1 warning, 2 error
  void* (*window)();                         // the game's HWND, null while there is none
  const uint32_t* (*gamma_pwl)();            // 128 x 3 words, or null
  const uint32_t* (*gamma_table)();          // 256 words, or null
  void* (*watch_register)(GrawboxWriteCallback callback, void* context);
  void (*watch_unregister)(void* handle);
  void (*watch)(uint32_t start, uint32_t length);
  void (*skip_emulated_draws)(bool skip);
  void (*connect_presenter)(bool connect);
  bool (*system_ui_active)();
  const char* cache_dir;  // UTF-8, empty for none
  // An option by its name without the "graw_" prefix, null when not set.
  const char* (*option)(const char* name);
};

// Where a title's D3D keeps what the renderer reads (byte offsets from the device).
struct GrawboxTitle {
  uint32_t size;  // sizeof(GrawboxTitle)
  uint32_t push, dirty_masks, dirty_mask_count;
  uint64_t dirty_bits[6];
  uint64_t shader_load_bits;
  uint32_t fetch_constants, vs_constants, ps_constants, bool_loop, ring_read_ptr, flags;
  uint32_t shadow_2000, shadow_2100, shadow_2180, shadow_2200, shadow_2280, shadow_2300, shadow_2380;
  uint32_t scissor_tl, scissor_br, index_buffer, color_surfaces, depth_surface, vertex_stride;
  uint32_t tile_count, tile_rects, tile_origins, pending_push;
  uint32_t texture_fetch, index_buffer_format, index_buffer_address;
};

// A guest D3D call. `function` is one of graw_d3d.h's fn:: numbers.
struct GrawboxCall {
  uint32_t function;
  uint32_t r[8];  // r3..r10 at entry
  uint32_t sp;
  double f[4];  // f1..f4 at entry
  // Runs the game's own function, returns r3.
  uint64_t (*original)(void* context);
  void* context;
};

GRAWBOX_NATIVE_API void grawbox_native_call(uint8_t* base, const GrawboxCall* call);
// True while this renderer draws the game (false: the emulated one does, or it failed).
GRAWBOX_NATIVE_API bool grawbox_native_drawing();
GRAWBOX_NATIVE_API bool grawbox_native_start(const GrawboxHost* host, const GrawboxTitle* title);
GRAWBOX_NATIVE_API void grawbox_native_request_switch();
