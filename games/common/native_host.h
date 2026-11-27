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
  REX_SETTING(std::string, renderer, "native",
              "Renderer at start, F9 switches. off leaves the native renderer unloaded",
              rex::Allowed("native", "emulated", "off"), rex::kRestart);
  REX_SETTING(rex::i32, resolution_scale, 1, "Resolution scale of the native renderer",
              rex::Range{1, 3}, rex::kRestart);
  REX_SETTING(std::string, haze, "fixed",
              "The games' distance haze: fixed (no double image, no bleeding), original or off",
              rex::Allowed("fixed", "original", "off"), rex::kRestart);
  REX_SETTING(rex::i32, haze_variant, 3, "Motion of the fixed haze: 1 as the games, 3 to 6 a random drift",
              rex::Range{1, 6}, rex::kRestart);
  REX_SETTING(bool, haze_tuning, false, "F8 goes through the variants of the fixed haze",
              rex::kRestart);
  REX_SETTING(rex::i32, ambient_occlusion, 0, "Ambient occlusion, its strength in percent",
              rex::Range{0, 100}, rex::kRestart);
  REX_SETTING(rex::f64, sharpness, 0.0, "Sharpening of the scene", rex::Range{0.0, 1.0},
              rex::kRestart);
  REX_SETTING(rex::f64, mip_bias, 0.0, "Texture sharpness: below 0 sharper, above 0 softer",
              rex::Range{-2.0, 2.0}, rex::kRestart);
  REX_SETTING(rex::i32, anisotropy, 16, "Anisotropic texture filtering, 1 for the game's own",
              rex::Range{1, 16}, rex::kRestart);
  REX_SETTING(std::string, prompts, "auto",
              "Button prompts: controller, keyboard, or auto for the keyboard driver's choice",
              rex::Allowed("auto", "controller", "keyboard"), rex::kRestart);
  REX_SETTING(rex::i32, field_of_view, 100,
              "Field of view in percent of the game's own, as the width of the picture",
              rex::Range{75, 150}, rex::kRestart);
  REX_SETTING(std::string, aspect_ratio, "16:9",
              "Shape of the picture: 16:9 as the games, 21:9, 32:9, or the display's",
              rex::Allowed("16:9", "21:9", "32:9", "display"), rex::kRestart);
  REX_SETTING(std::string, hud_layout, "centered",
              "Menus and HUD on a wider picture: centered (16:9 in the middle) or stretched across",
              rex::Allowed("centered", "stretched"), rex::kRestart);
  REX_SETTING(bool, vsync, true, "Wait for the display when the native renderer presents",
              rex::kRestart);
  REX_SETTING(bool, shader_wait, true,
              "Wait for shaders seen for the first time: nothing is left out of a frame, but it can stutter "
              "(false: the frame is shown without them and they appear a frame or a few later)",
              rex::kRestart);
};

NativeSettings& Settings();
// The picture's width over its height from the aspect_ratio setting: 16/9 unless a wider one is
// chosen. "display" is the primary display's.
double AspectRatio();
// Whether menus and HUD are kept 16:9 in the middle of a wider picture (hud_layout).
bool HudCentered();
void SetApp(rex::app::App* app);
void Call(uint32_t function, PPCContext& ctx, uint8_t* base,
          void (*original)(PPCContext&, uint8_t*));

// Each title defines its own.
extern const GrawboxTitle kTitle;
// Whether the title's camera takes the aspect_ratio setting (its camera.cpp); without it the
// picture stays 16:9.
extern const bool kTitleWidens;

}  // namespace grawbox

#define GRAWBOX_HOOK(address, function)                                                     \
  REX_EXTERN(__imp__sub_##address);                                                         \
  REX_HOOK_RAW(sub_##address) { grawbox::Call(function, ctx, base, __imp__sub_##address); } \
  static_assert(true)
