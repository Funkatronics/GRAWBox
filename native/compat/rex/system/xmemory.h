// Guest memory, as the host gives it (grawbox_native.h).
#pragma once

#include <cstdint>
#include <utility>

namespace rex::memory {
namespace detail {
// The 0xE0000000 view of physical memory sits 4 KiB higher in the host mapping.
inline uint32_t PhysicalHostOffset(uint32_t a) { return a >= 0xE0000000u ? 0x1000u : 0u; }
}  // namespace detail
using PhysicalMemoryInvalidationCallback = std::pair<uint32_t, uint32_t> (*)(void* context,
                                                                             uint32_t start,
                                                                             uint32_t length,
                                                                             bool exact);
class Memory {
 public:
  uint8_t* physical_membase() const;
  uint8_t* virtual_membase() const;
  template <typename T = uint8_t*>
  T TranslatePhysical(uint32_t address) const {
    return reinterpret_cast<T>(physical_membase() + (address & 0x1FFFFFFF));
  }
  void* RegisterPhysicalMemoryInvalidationCallback(PhysicalMemoryInvalidationCallback callback,
                                                   void* context);
  void UnregisterPhysicalMemoryInvalidationCallback(void* handle);
  void EnablePhysicalMemoryAccessCallbacks(uint32_t start, uint32_t length, bool invalidation,
                                           bool data_providers);
};
}  // namespace rex::memory
