# ReXGlue SDK patches

Changes to [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) v0.11.0, under its BSD-3-Clause license.
Apply with `git apply` in the SDK root.

| Patch | Purpose |
|---|---|
| `rexglue-0.11.0-freed-page-resolve.patch` | GPU writes that land in memory the title has freed no longer hide what the title loads there next. Adapted from graw360-recomp's `freed-page-resolve` patch. Fixes the HUD noise in GRAW 1. |
| `rexglue-0.11.0-host-renderer.patch` | Lets the native renderer draw and present while packets still run. Follows pgr4-native's `gpu_skip_draws` patch (MIT). |

Build notes: Vulkan-Headers `vulkan-sdk-1.4.363.0` and VulkanMemoryAllocator `v3.5.0` are enough in place of the
Vulkan SDK. Set `Vulkan_INCLUDE_DIR`, `REXGLUE_VMA_INCLUDE_DIR`, and `Vulkan_LIBRARY` to any existing Vulkan loader.
