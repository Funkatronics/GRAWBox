// The host's services, under the names the renderer's sources use.
#include "host.h"

#include <rex/cvar.h>
#include <rex/graphics/command_processor.h>
#include <rex/kernel/xam/module.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/keybinds.h>

#include <map>
#include <mutex>
#include <string>

namespace graw::native {
GrawboxHost g_host{};
GrawboxTitle g_title{};
}  // namespace graw::native

using graw::native::g_host;

namespace {
std::mutex g_flag_mutex;
std::map<std::string, std::string> g_flags;
std::mutex g_bind_mutex;
std::function<void()> g_switch;
rex::memory::Memory g_memory;

// The callbacks' pair packed for the C interface.
struct Watcher {
  rex::memory::PhysicalMemoryInvalidationCallback callback;
  void* context;
  void* handle;
};

uint64_t OnWrite(void* context, uint32_t start, uint32_t length, bool exact) {
  const auto* w = static_cast<const Watcher*>(context);
  const std::pair<uint32_t, uint32_t> r = w->callback(w->context, start, length, exact);
  return uint64_t(r.first) | uint64_t(r.second) << 32;
}
}  // namespace

void grawbox::Log(int level, const std::string& text) {
  if (g_host.log) g_host.log(level, text.c_str());
}

bool rex::cvar::Lookup(const char* name, std::string& value) {
  std::string n = name;
  if (n.starts_with("graw_")) n.erase(0, 5);
  const char* v = g_host.option ? g_host.option(n.c_str()) : nullptr;
  if (!v) return false;
  value = v;
  return true;
}

std::string rex::cvar::GetFlagByName(const char* name) {
  {
    std::lock_guard lock(g_flag_mutex);
    if (auto it = g_flags.find(name); it != g_flags.end()) return it->second;
  }
  std::string v;
  Lookup(name, v);
  return v;
}

bool rex::cvar::SetFlagByName(const char* name, const std::string& value) {
  const std::string n = name;
  if (n == "gpu_skip_draws") {
    if (!g_host.skip_emulated_draws) return false;
    g_host.skip_emulated_draws(value == "true");
  }
  std::lock_guard lock(g_flag_mutex);
  g_flags[n] = value;
  return true;
}

uint8_t* rex::memory::Memory::physical_membase() const { return g_host.physical_base; }
uint8_t* rex::memory::Memory::virtual_membase() const { return g_host.virtual_base; }

void* rex::memory::Memory::RegisterPhysicalMemoryInvalidationCallback(
    PhysicalMemoryInvalidationCallback callback, void* context) {
  auto* w = new Watcher{callback, context, nullptr};
  w->handle = g_host.watch_register(OnWrite, w);
  return w;
}

void rex::memory::Memory::UnregisterPhysicalMemoryInvalidationCallback(void* handle) {
  auto* w = static_cast<Watcher*>(handle);
  g_host.watch_unregister(w->handle);
  delete w;
}

void rex::memory::Memory::EnablePhysicalMemoryAccessCallbacks(uint32_t start, uint32_t length, bool,
                                                              bool) {
  g_host.watch(start, length);
}

rex::memory::Memory* rex::system::kernel_memory() { return &g_memory; }

rex::system::KernelState* rex::system::kernel_state() {
  static KernelState state;
  return &state;
}

rex::Runtime* rex::Runtime::instance() {
  static Runtime runtime;
  return &runtime;
}

std::filesystem::path rex::Runtime::cache_root() const {
  return g_host.cache_dir ? std::filesystem::path(reinterpret_cast<const char8_t*>(g_host.cache_dir))
                          : std::filesystem::path();
}

bool rex::kernel::xam::xeXamIsUIActive() {
  return g_host.system_ui_active && g_host.system_ui_active();
}

void rex::ui::RegisterBind(const char*, const char*, const char*, std::function<void()> callback) {
  std::lock_guard lock(g_bind_mutex);
  g_switch = std::move(callback);
}

void rex::ui::UnregisterBind(const char*) {
  std::lock_guard lock(g_bind_mutex);
  g_switch = nullptr;
}

GRAWBOX_NATIVE_API void grawbox_native_request_switch() {
  std::lock_guard lock(g_bind_mutex);
  if (g_switch) g_switch();
}

const rex::graphics::reg::DC_LUT_PWL_DATA* rex::graphics::CommandProcessor::gamma_ramp_pwl_rgb()
    const {
  return reinterpret_cast<const reg::DC_LUT_PWL_DATA*>(g_host.gamma_pwl());
}

const rex::graphics::reg::DC_LUT_30_COLOR*
rex::graphics::CommandProcessor::gamma_ramp_256_entry_table() const {
  return reinterpret_cast<const reg::DC_LUT_30_COLOR*>(g_host.gamma_table());
}
