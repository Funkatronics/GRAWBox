#pragma once

#include <rex/graphics/registers.h>
#include <rex/graphics/xenos.h>

namespace rex::graphics {
class CommandProcessor {
 public:
  const reg::DC_LUT_PWL_DATA* gamma_ramp_pwl_rgb() const;
  const reg::DC_LUT_30_COLOR* gamma_ramp_256_entry_table() const;
};
}  // namespace rex::graphics
