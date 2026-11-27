#include "native_host.h"

#include "../../input/keys.h"

#include <rex/kernel/heap.h>
#include <rex/kernel/thread_state.h>
#include <rex/logging/log_category.h>
#include <rex/platform/native_window.h>
#include <rex/ppc/hook.h>
#include <rex/settings_registry.h>
#include <rex/ui/ui_system.h>

#include <windows.h>

#include <atomic>
#include <cctype>
#include <map>
#include <mutex>
#include <string>
#include <thread>

REX_EXTERN(__imp__XamIsUIActive);

namespace grawbox {

double AspectRatio() {
  static const double aspect = [] {
    // Only the native renderer stretches the wider picture back out when it presents; the emulated
    // one would show it squashed. F9 to the emulated renderer mid-game still does (a test feature).
    const bool widens = kTitleWidens && Settings().renderer.Get() == "native";
    const std::string a = widens ? Settings().aspect_ratio.Get() : "16:9";
    if (a == "21:9") return 21.0 / 9.0;
    if (a == "32:9") return 32.0 / 9.0;
    if (a == "display") {
      const int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
      // Narrower than 16:9 keeps the games' 16:9, with bars above and below.
      if (w > 0 && h > 0 && double(w) / h > 16.0 / 9.0 + 0.01) return double(w) / h;
    }
    return 16.0 / 9.0;
  }();
  return aspect;
}

bool HudCentered() {
  static const bool centered = Settings().hud_layout.Get() != "stretched";
  return centered;
}

namespace {

constexpr uint32_t kSwap = 10;  // native/src/graw_d3d.h

rex::app::App* g_app = nullptr;
rex::kernel::GuestAddressSpace* g_memory = nullptr;
std::atomic<bool> g_system_ui = false;
HWND g_parent = nullptr;
std::atomic<HWND> g_child = nullptr;

decltype(&grawbox_native_start) g_start = nullptr;
decltype(&grawbox_native_call) g_call = nullptr;
decltype(&grawbox_native_request_switch) g_request_switch = nullptr;
void (*g_gpu_draws)(bool) = nullptr;
void (*g_gpu_presents)(bool) = nullptr;
const void* (*g_gpu_gamma)(bool) = nullptr;

// The renderer presents into a child over the game's window: the SDK's swap chain keeps the window
// itself. Disabled, so that mouse input goes to the game's window.
void ChildWindowThread() {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"GRAWBoxNative";
  RegisterClassW(&wc);
  RECT r = {};
  GetClientRect(g_parent, &r);
  HWND w = CreateWindowExW(WS_EX_NOPARENTNOTIFY, wc.lpszClassName, L"", WS_CHILD | WS_DISABLED, 0,
                           0, r.right, r.bottom, g_parent, nullptr, wc.hInstance, nullptr);
  g_child = w;
  if (!w) return;
  SetTimer(w, 1, 50, nullptr);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (msg.message == WM_TIMER && msg.hwnd == w) {
      RECT p = {}, c = {};
      GetClientRect(g_parent, &p);
      GetClientRect(w, &c);
      if (p.right > 0 && p.bottom > 0 && (p.right != c.right || p.bottom != c.bottom))
        SetWindowPos(w, HWND_TOP, 0, 0, p.right, p.bottom, SWP_NOACTIVATE);
      continue;
    }
    DispatchMessageW(&msg);
  }
}

void* Window() {
  if (!g_app || !g_app->GetUI().HasWindow()) return nullptr;
  if (!g_parent) {
    auto native = g_app->GetUI().GetNativeWindow();
    if (!native || native->type != rex::NativeWindow::Type::kWin32) return nullptr;
    g_parent = static_cast<HWND>(native->window);
    std::thread(ChildWindowThread).detach();
  }
  return g_child.load();
}

void HostLog(int level, const char* text) {
  if (level == 2)
    REXLOG_ERROR("{}", text);
  else if (level == 1)
    REXLOG_WARN("{}", text);
  else
    REXLOG_INFO("{}", text);
}

// Any of the renderer's options also comes from GRAWBOX_<NAME> in the environment.
const char* Option(const char* name) {
  static std::mutex mutex;
  static std::map<std::string, std::string> values;
  std::lock_guard lock(mutex);
  const std::string n = name;
  std::string upper = "GRAWBOX_";
  for (char c : n) upper += char(std::toupper(static_cast<unsigned char>(c)));
  char buffer[1024];
  const DWORD length = GetEnvironmentVariableA(upper.c_str(), buffer, sizeof(buffer));
  if (length && length < sizeof(buffer)) return (values[n] = buffer).c_str();
  if (n == "native_primary") return "true";
  if (n == "d3d12_present_vsync") return Settings().vsync.Get() ? "true" : "false";
  if (n == "native_pipeline_wait") return Settings().shader_wait.Get() ? "true" : "false";
  if (n == "native_haze") return (values[n] = Settings().haze.Get()).c_str();
  if (n == "native_anisotropy")
    return (values[n] = std::to_string(Settings().anisotropy.Get())).c_str();
  if (n == "native_haze_variant")
    return (values[n] = std::to_string(Settings().haze_variant.Get())).c_str();
  if (n == "native_haze_tuning") return Settings().haze_tuning.Get() ? "true" : "false";
  if (n == "native_ao")
    return (values[n] = std::to_string(Settings().ambient_occlusion.Get())).c_str();
  if (n == "native_sharpness")
    return (values[n] = std::to_string(Settings().sharpness.Get())).c_str();
  if (n == "native_mip_bias")
    return (values[n] = std::to_string(Settings().mip_bias.Get())).c_str();
  if (n == "native_prompts") {
    std::string prompts = Settings().prompts.Get();
    // Automatic: the keys while the keyboard and mouse were used last, with the keyboard driver.
    const bool automatic = prompts == "auto";
    if (automatic) {
      auto drivers = REX_SETTINGS().Find("hid.drivers");
      prompts = drivers && (*drivers)->GetText().find("keyboard") != std::string::npos
                    ? "keyboard" : "controller";
    }
    if (prompts != "keyboard") return nullptr;
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return (values[n] = keys::PromptLabels(keys::Load(
                std::filesystem::path(path).parent_path() / "grawbox-keys.txt")) +
                        (automatic ? "@auto=1;" : ""))
        .c_str();
  }
  if (n == "native_aspect")
    return (values[n] = std::to_string(AspectRatio())).c_str();
  if (n == "native_resolution_scale")
    return (values[n] = std::to_string(Settings().resolution_scale.Get())).c_str();
  return nullptr;
}

struct Watch {
  GrawboxWriteCallback callback;
  void* context;
  void* handle;
};

std::pair<rex::u32, rex::u32> OnWrite(void* context, rex::u32 start, rex::u32 length, bool exact) {
  const auto* w = static_cast<const Watch*>(context);
  const uint64_t r = w->callback(w->context, start, length, exact);
  return {rex::u32(r), rex::u32(r >> 32)};
}

void* WatchRegister(GrawboxWriteCallback callback, void* context) {
  auto* w = new Watch{callback, context, nullptr};
  w->handle = g_memory->RegisterPhysicalMemoryInvalidationCallback(OnWrite, w);
  return w;
}

void WatchUnregister(void* handle) {
  auto* w = static_cast<Watch*>(handle);
  g_memory->UnregisterPhysicalMemoryInvalidationCallback(w->handle);
  delete w;
}

template <typename F>
bool Load(HMODULE module, const char* name, F& out) {
  out = reinterpret_cast<F>(GetProcAddress(module, name));
  if (!out) REXLOG_ERROR("GRAW native: {} not found", name);
  return out != nullptr;
}

bool Start() {
  if (Settings().renderer.Get() == "off") return false;
  HMODULE gpu = GetModuleHandleW(L"rexgpu-xenos-d3d12.dll");
  if (!gpu) {
    REXLOG_WARN("GRAW native: needs the d3d12 graphics driver");
    return false;
  }
  HMODULE native = LoadLibraryW(L"grawbox_native.dll");
  if (!native) {
    REXLOG_WARN("GRAW native: grawbox_native.dll did not load ({})", GetLastError());
    return false;
  }
  if (!Load(gpu, "rexgpu_host_renderer_draws", g_gpu_draws) ||
      !Load(gpu, "rexgpu_host_renderer_presents", g_gpu_presents) ||
      !Load(gpu, "rexgpu_gamma_ramp", g_gpu_gamma) ||
      !Load(native, "grawbox_native_start", g_start) ||
      !Load(native, "grawbox_native_call", g_call) ||
      !Load(native, "grawbox_native_request_switch", g_request_switch))
    return false;
  rex::kernel::ThreadState* thread = rex::kernel::ThreadState::Get();
  if (!thread || !thread->GetAddressSpace()) return false;
  g_memory = thread->GetAddressSpace();
  static std::string cache;
  if (auto root = REX_SETTINGS().Find("app.cache_root")) cache = (*root)->GetText();
  GrawboxHost host = {};
  host.size = sizeof(host);
  host.virtual_base = g_memory->GetVirtualMembase();
  host.physical_base = g_memory->GetPhysicalMembase();
  host.log = HostLog;
  host.window = Window;
  host.gamma_pwl = [] { return static_cast<const uint32_t*>(g_gpu_gamma(true)); };
  host.gamma_table = [] { return static_cast<const uint32_t*>(g_gpu_gamma(false)); };
  host.watch_register = WatchRegister;
  host.watch_unregister = WatchUnregister;
  host.watch = [](uint32_t start, uint32_t length) {
    g_memory->EnablePhysicalMemoryAccessCallbacks(start, length, true, false);
  };
  host.skip_emulated_draws = [](bool skip) { g_gpu_draws(skip); };
  host.connect_presenter = [](bool connect) {
    g_gpu_presents(!connect);
    if (HWND child = g_child) ShowWindowAsync(child, connect ? SW_HIDE : SW_SHOWNA);
  };
  host.system_ui_active = [] { return g_system_ui.load(); };
  host.cache_dir = cache.c_str();
  host.option = Option;
  return g_start(&host, &kTitle);
}

void AtSwap(PPCContext& ctx, uint8_t* base) {
  // XAM is asked on the guest's thread, the renderer reads the answer on its own.
  PPCContext copy = ctx;
  __imp__XamIsUIActive(copy, base);
  g_system_ui = copy.r3.u32 != 0;
  static bool down = false, first = true;
  if (first) {
    first = false;
    if (Settings().renderer.Get() == "emulated") g_request_switch();
  }
  const bool now = GetForegroundWindow() == g_parent && (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
  if (now && !down) g_request_switch();
  down = now;
}

}  // namespace

NativeSettings& Settings() {
  static NativeSettings settings;
  return settings;
}

void SetApp(rex::app::App* app) { g_app = app; }

void Call(uint32_t function, PPCContext& ctx, uint8_t* base,
          void (*original)(PPCContext&, uint8_t*)) {
  static const bool started = Start();
  if (!started) {
    original(ctx, base);
    return;
  }
  struct Bound {
    PPCContext& ctx;
    uint8_t* base;
    void (*original)(PPCContext&, uint8_t*);
  } bound{ctx, base, original};
  GrawboxCall call;
  call.function = function;
  const uint32_t r[8] = {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,
                         ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32};
  for (int i = 0; i < 8; ++i) call.r[i] = r[i];
  call.sp = ctx.r1.u32;
  call.f[0] = ctx.f1.f64;
  call.f[1] = ctx.f2.f64;
  call.f[2] = ctx.f3.f64;
  call.f[3] = ctx.f4.f64;
  call.original = [](void* context) -> uint64_t {
    auto& b = *static_cast<Bound*>(context);
    b.original(b.ctx, b.base);
    return b.ctx.r3.u64;
  };
  call.context = &bound;
  g_call(base, &call);
  if (function == kSwap) AtSwap(ctx, base);
}

}  // namespace grawbox
