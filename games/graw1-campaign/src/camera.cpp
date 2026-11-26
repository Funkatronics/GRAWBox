// GRAW 1's camera (title update 0.0.2.1): the field of view setting.
//
// sub_82181F58 builds a view's projection with sub_8212B298(matrix, f1 = the camera's field of
// view, f2 = aspect, f3 = near, f4 = far), the same design as GRAW 2's sub_8232F0D0. The camera is
// at view+592 and keeps the field of view at +72; sub_8212B298 scales it by the constant at
// 0x8209AE90 to get the vertical half angle. The setting scales the tangent of that half angle.
#include "generated/campaign/graw1_campaign_init.h"

#include "native_host.h"

#include <rex/ppc/hook.h>

#include <bit>
#include <cmath>
#include <cstring>

REX_EXTERN(__imp__sub_8212B298);
REX_HOOK_RAW(sub_8212B298) {
  if (static_cast<uint32_t>(ctx.lr) == 0x82181FEC) {
    static const double scale = grawbox::Settings().field_of_view.Get() / 100.0;
    if (scale != 1.0) {
      uint32_t bits;
      std::memcpy(&bits, base + 0x8209AE90, 4);
      const double half = std::bit_cast<float>(__builtin_bswap32(bits));
      if (half > 0.0) ctx.f1.f64 = std::atan(std::tan(ctx.f1.f64 * half) * scale) / half;
    }
  }
  __imp__sub_8212B298(ctx, base);
}
