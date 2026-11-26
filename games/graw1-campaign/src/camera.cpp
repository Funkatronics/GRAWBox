// GRAW 1's camera (title update 0.0.2.1): the field of view and aspect ratio settings.
//
// sub_82181F58 builds a view's projection with sub_8212B298(matrix, f1 = the camera's field of
// view, f2 = aspect, f3 = near, f4 = far), the same design as GRAW 2's sub_8232F0D0. The camera is
// at view+592 and keeps the field of view at +72; sub_8212B298 scales it by the constant at
// 0x8209AE90 to get the vertical half angle. The setting scales the tangent of that half angle.
//
// A wider aspect keeps the vertical field of view and widens the horizontal (the view's own, from
// its 1280x720 at +604 / +606, is kept at +588 too). The game still draws 1280x720; the native
// renderer stretches the picture to the aspect when it presents.
//
// The GUI (menus and HUD) would come out stretched with it. sub_8216EAA0 copies its pixel-to-clip
// map (2/1280 and -2/720) to its stack and sets it as c0..c3 with sub_824C87D0, the
// SetVertexShaderConstantF; the only caller of it with that map. As in GRAW 2, the map's row for x
// is scaled by 16:9 over the aspect about the middle, so menus and HUD keep their shapes in a 16:9
// area in the centre.
#include "generated/campaign/graw1_campaign_init.h"

#include "native_host.h"

#include <rex/ppc/hook.h>

#include <bit>
#include <cmath>
#include <cstring>

namespace {
void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(base + address, &value, 4);
}
float LoadFloat(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, 4);
  return std::bit_cast<float>(__builtin_bswap32(value));
}
}  // namespace

REX_EXTERN(__imp__sub_8212B298);
REX_HOOK_RAW(sub_8212B298) {
  if (static_cast<uint32_t>(ctx.lr) == 0x82181FEC) {
    static const double scale = grawbox::Settings().field_of_view.Get() / 100.0;
    if (scale != 1.0) {
      const double half = LoadFloat(base, 0x8209AE90);
      if (half > 0.0) ctx.f1.f64 = std::atan(std::tan(ctx.f1.f64 * half) * scale) / half;
    }
    static const double aspect = grawbox::AspectRatio();
    if (aspect > 16.0 / 9.0 + 0.01) {
      ctx.f2.f64 = aspect;
      const uint32_t view = ctx.r31.u32;  // sub_82181F58's, at the call
      Store32(base, view + 588, std::bit_cast<uint32_t>(static_cast<float>(aspect)));
    }
  }
  __imp__sub_8212B298(ctx, base);
}

REX_EXTERN(__imp__sub_824C87D0);
REX_HOOK_RAW(sub_824C87D0) {
  // SetVertexShaderConstantF(device, start, data, count) from sub_8216EAA0: the GUI's map.
  static const double aspect = grawbox::AspectRatio();
  if (static_cast<uint32_t>(ctx.lr) == 0x8216EB3C && aspect > 16.0 / 9.0 + 0.01) {
    const float k = static_cast<float>(16.0 / 9.0 / aspect);
    const uint32_t c0 = ctx.r5.u32;  // on sub_8216EAA0's stack
    Store32(base, c0, std::bit_cast<uint32_t>(LoadFloat(base, c0) * k));
    Store32(base, c0 + 12, std::bit_cast<uint32_t>(LoadFloat(base, c0 + 12) * k));
  }
  __imp__sub_824C87D0(ctx, base);
}
