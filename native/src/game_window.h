// The game window and the SDK's presenter, which presents into it unless the native renderer does.
#pragma once

#include "host.h"

namespace graw::native {
inline void* GameWindowHandle() { return g_host.window ? g_host.window() : nullptr; }
inline void ConnectSdkPresenter(bool connect) {
  if (g_host.connect_presenter) g_host.connect_presenter(connect);
}
}  // namespace graw::native
