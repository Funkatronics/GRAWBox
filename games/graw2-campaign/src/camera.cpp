// GRAW 2's camera (title update 0.0.4.1): the field of view setting.
//
// sub_82351AB8 builds a view's projection, the one drawing and culling use, with
// sub_8232F0D0(matrix, f1 = the camera's field of view, f2 = aspect, f3 = near, f4 = far). The
// camera keeps the field of view at +72 as four times the vertical half angle, in radians (1.5 to
// 1.6 in play; 0x820C5BD4 holds the 0.25). The setting scales the tangent of that half angle, so
// the picture is that much wider and taller and the game's own zooms keep their proportions.
#include "generated/campaign/graw2_campaign_init.h"

#include "native_host.h"

#include <rex/ppc/hook.h>

#include <cmath>

REX_EXTERN(__imp__sub_8232F0D0);
REX_HOOK_RAW(sub_8232F0D0) {
  if (static_cast<uint32_t>(ctx.lr) == 0x82351B7C) {
    static const double scale = grawbox::Settings().field_of_view.Get() / 100.0;
    if (scale != 1.0) ctx.f1.f64 = 4.0 * std::atan(std::tan(ctx.f1.f64 * 0.25) * scale);
  }
  __imp__sub_8232F0D0(ctx, base);
}
