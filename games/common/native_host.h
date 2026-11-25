// Hosts the native renderer (native/): loads its library, gives it the SDK's services and reports
// the title's Direct3D calls to it.
#pragma once

#include <rex/app/app.h>
#include <rex/ppc/context.h>
#include <rex/setting.h>

#include <grawbox_native.h>

namespace grawbox {

class NativeSettings : public rex::SettingOwner {
 public:
  NativeSettings() : rex::SettingOwner("native") {}
  REX_SETTING(std::string, renderer, "emulated",
              "Renderer at start, F9 switches. off leaves the native renderer unloaded",
              rex::Allowed("native", "emulated", "off"), rex::kRestart);
  REX_SETTING(rex::i32, resolution_scale, 1, "Resolution scale of the native renderer",
              rex::Range{1, 3}, rex::kRestart);
  REX_SETTING(bool, vsync, true, "Wait for the display when the native renderer presents",
              rex::kRestart);
};

NativeSettings& Settings();
void SetApp(rex::app::App* app);
void Call(uint32_t function, PPCContext& ctx, uint8_t* base,
          void (*original)(PPCContext&, uint8_t*));

// Each title defines its own.
extern const GrawboxTitle kTitle;

}  // namespace grawbox

#define GRAWBOX_HOOK(address, function)                                                     \
  REX_EXTERN(__imp__sub_##address);                                                         \
  REX_HOOK_RAW(sub_##address) { grawbox::Call(function, ctx, base, __imp__sub_##address); } \
  static_assert(true)
