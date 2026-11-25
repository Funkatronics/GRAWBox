// The counters the host's frame limiter keeps in graw360-recomp; nothing waits here.
#include <rex/cvar.h>

#include <cstdint>

REXCVAR_DEFINE_INT32(graw_d3d_stats_frames, 600, "GRAW native", "");

uint64_t GrawTakeLimitWaitNs() { return 0; }
uint64_t GrawTakeRingWaitNs() { return 0; }

#include <cstdio>
#include <filesystem>
#include <string_view>

namespace rex::filesystem {
FILE* OpenFile(const std::filesystem::path& path, const std::string_view mode) {
  const std::wstring wide_mode(mode.begin(), mode.end());
  return _wfopen(path.c_str(), wide_mode.c_str());
}
}  // namespace rex::filesystem
