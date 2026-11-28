// GRAW native renderer: the cvars both backends and the renderer host read, compiled on every
// platform (the D3D12 renderer's own are in its files, the Vulkan backend's in
// vulkan_backend.cpp).
#include <rex/cvar.h>

#include <cstdint>
#include <string>

REXCVAR_DEFINE_BOOL(graw_native_async_pipelines, true, "GRAW native",
                    "Compile new pipeline states on worker threads, the frame's draws that need "
                    "them skipped, instead of compiling each at its draw");
REXCVAR_DEFINE_INT32(graw_native_pipeline_threads, 0, "GRAW native",
                     "Pipeline compile threads with --graw_native_async_pipelines (0: a quarter "
                     "of the hardware threads, 1 to 4)");
REXCVAR_DEFINE_BOOL(graw_native_pipeline_wait, true, "GRAW native",
                    "With --graw_native_async_pipelines, a frame whose draws need pipelines still "
                    "compiling is not shown and waits for them at its end, up to 0.5 s (false: "
                    "show it without those draws)");
REXCVAR_DEFINE_BOOL(graw_native_prepare_shaders, true, "GRAW native",
                    "At start, before the game's first frame: build every pipeline known from "
                    "earlier runs and the recipes, with all compile threads, showing a progress "
                    "bar (false: build them a slice per frame while playing)");
REXCVAR_DEFINE_BOOL(graw_native_pipeline_cache, true, "GRAW native",
                    "Keep the shaders and pipelines the native renderer creates under "
                    "<cache_root>\\pgr4-native and compile them at start-up in later runs "
                    "(prewarm, plus a D3D12 pipeline library)");
REXCVAR_DEFINE_STRING(graw_native_shader_file, "", "GRAW native",
                      "The games' compiled shaders (NewYetiShadersX360.sdf in the game folder): with "
                      "--graw_native_recipes, the pixel shaders recipes need are warmed from it");
REXCVAR_DEFINE_STRING(graw_native_recipes, "", "GRAW native",
                      "Recipe file (pipelines.bin format without shaders): pipelines recorded in "
                      "playtests, built or warmed at start-up as the store's are");
REXCVAR_DEFINE_STRING(graw_native_watch_trace, "", "GRAW native",
                      "Binary log of vertex write-watch events (arms and faults, one 64-bit "
                      "record each, see WatchTrace) for offline analysis of the watch policy");
REXCVAR_DEFINE_BOOL(graw_native_coherency_check, false, "GRAW native",
                    "Debug: keep a CPU copy of what shared memory holds and compare the vertex and "
                    "index ranges draws read with guest memory, per request and again at the Swap "
                    "(stale pages: a write the page watch missed; late pages: data rewritten after "
                    "a draw of the frame read it), and check copy segments against every read. "
                    "Slow (512 MB of host memory, a memcmp of every range read)");
REXCVAR_DEFINE_BOOL(graw_native_implicit_gradients, true, "GRAW native",
                    "Experiment (Steam Deck shimmer): translate computed-LOD texture fetches in "
                    "pixel shaders with sample_b and the GPU's own derivatives (pgr4-native 0021; "
                    "the Vulkan renderer's SPIR-V shaders too, with OpImageSampleImplicitLod); "
                    "false: explicit coarse gradients, as before 0021");
REXCVAR_DEFINE_BOOL(graw_native_luma_probe, false, "GRAW native",
                    "Read back three rows of every front buffer (at 10, 40 and 80 % of its "
                    "height) and log their mean brightness in the frame CSV, to find frames "
                    "drawn partly black");
REXCVAR_DEFINE_INT32(graw_native_resolution_scale, 1, "GRAW native",
                     "Draw resolution: 1 = 1280x720, 2 = 2560x1440, 3 = 3840x2160, 0 = the smallest "
                     "of them at least as tall as the screen");
REXCVAR_DEFINE_BOOL(graw_native_msaa, true, "GRAW native",
                    "Draw with the multisampling the game asks for");
REXCVAR_DEFINE_DOUBLE(graw_native_mip_bias, 0.0, "GRAW native",
                      "Mip level bias added to every sampler");
REXCVAR_DEFINE_INT32(graw_native_anisotropy, 1, "GRAW native",
                     "Anisotropic filtering of the game's linearly filtered textures with mips, "
                     "at least this many samples per pixel (2, 4, 8 or 16; 1: the game's own)");
REXCVAR_DEFINE_STRING(graw_native_haze, "fixed", "GRAW native",
                      "The games' distance haze: fixed (post.h), original (blurred and doubled as the "
                      "games draw it) or off");
REXCVAR_DEFINE_INT32(graw_native_haze_variant, 3, "GRAW native",
                     "Motion of the fixed haze: 1 as the games, or a variant (post.cpp)");
REXCVAR_DEFINE_BOOL(graw_native_haze_tuning, false, "GRAW native",
                    "F8 goes through the variants of the fixed haze");
REXCVAR_DEFINE_INT32(graw_native_ao, 0, "GRAW native",
                     "Ambient occlusion, its strength in percent (0: none)");
REXCVAR_DEFINE_STRING(graw_native_hud_layout, "", "GRAW native",
                      "With a wider --graw_native_aspect: edges spreads the HUD over the wide "
                      "picture, its elements at their own shape; centered keeps it 16:9 in the "
                      "middle (both as the game hook narrows it; anything else: stretched)");
REXCVAR_DEFINE_STRING(graw_native_hud_trace, "", "GRAW native",
                      "Debug, with --graw_native_hud_layout=edges: writes each frame's HUD quads to "
                      "this file, as the game placed them and as moved (tools/check_hud.py)");
REXCVAR_DEFINE_DOUBLE(graw_native_aspect, 0.0, "GRAW native",
                      "Width over height the picture is shown at (0: the image's own); a wider one "
                      "stretches it across, for a game drawing that aspect into 1280x720");
REXCVAR_DEFINE_DOUBLE(graw_native_sharpness, 0.0, "GRAW native",
                      "Sharpening of the scene, 0 to 1");
