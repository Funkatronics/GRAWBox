#pragma once

#include <rex/runtime.h>
#include <rex/system/xmemory.h>

namespace rex::system {
struct KernelState {
  rex::Runtime* emulator() const { return rex::Runtime::instance(); }
};
rex::memory::Memory* kernel_memory();
KernelState* kernel_state();
}  // namespace rex::system
