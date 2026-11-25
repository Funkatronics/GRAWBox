#pragma once

#include <filesystem>

#include <rex/system/xmemory.h>

namespace rex {
class Runtime {
 public:
  static Runtime* instance();
  std::filesystem::path cache_root() const;
};
}  // namespace rex
