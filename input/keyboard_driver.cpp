// Keyboard and mouse as a controller, as a ReXGlue input driver. Select it with
// --hid.drivers=keyboard,sdl3 and --hid.shared_pads=true to keep a controller working beside it.
//
// Unlike the SDK's own, a binding is a plain key: holding Shift or Ctrl leaves the others working.
// Bindings come from grawbox-keys.txt beside the program, one per line, '#' starts a comment:
//   <control> = <key> [<key>...]
//   mouse_sensitivity = 1.0      mouse_invert = 0      mouse_deadzone = 0.24
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include <rex/driver.h>
#include <rex/driver_context.h>
#include <rex/driver_module.h>
#include <rex/driver_registration.h>
#include <rex/hid/input_driver.h>
#include <rex/hid/pad.h>
#include <rex/module.h>

#include "keys.h"

namespace grawbox::keyboard {

using namespace rex;
using namespace rex::hid;

constexpr PadDeviceID kPad{0x4742584Bull};

using namespace grawbox::keys;

// Sums the mouse's raw motion on a thread of its own.
class Mouse {
 public:
  Mouse() : thread_([this] { Run(); }) {}
  ~Mouse() {
    if (const DWORD id = id_) PostThreadMessageW(id, WM_QUIT, 0, 0);
    thread_.join();
  }
  void Take(long& dx, long& dy) {
    dx = dx_.exchange(0);
    dy = dy_.exchange(0);
  }

 private:
  static LRESULT CALLBACK Proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_INPUT) {
      RAWINPUT raw;
      UINT size = sizeof(raw);
      if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &raw, &size,
                          sizeof(RAWINPUTHEADER)) != UINT(-1) &&
          raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
        auto* self = reinterpret_cast<Mouse*>(GetWindowLongPtrW(w, GWLP_USERDATA));
        if (self) {
          self->dx_ += raw.data.mouse.lLastX;
          self->dy_ += raw.data.mouse.lLastY;
        }
      }
    }
    return DefWindowProcW(w, m, wp, lp);
  }
  void Run() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"GRAWBoxMouse";
    RegisterClassW(&wc);
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                             wc.hInstance, nullptr);
    if (!w) return;
    SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    RAWINPUTDEVICE device = {0x01, 0x02, RIDEV_INPUTSINK, w};
    RegisterRawInputDevices(&device, 1, sizeof(device));
    id_ = GetCurrentThreadId();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    DestroyWindow(w);
  }
  std::atomic<long> dx_ = 0, dy_ = 0;
  std::atomic<DWORD> id_ = 0;
  std::thread thread_;
};

class Driver final : public IInputDriver {
 public:
  std::string_view GetName() const override { return "keyboard"; }

  Result<void> Initialize(IDriverContext&) override {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    settings_ = Load(std::filesystem::path(path).parent_path() / "grawbox-keys.txt");
    for (size_t i = 0; i < kControlCount; ++i)
      for (const std::string& name : settings_.keys[i]) keys_[i].push_back(KeyFromName(name));
    mouse_ = std::make_unique<Mouse>();
    last_ = std::chrono::steady_clock::now();
    return Ok();
  }

  void Shutdown() override {
    mouse_.reset();
    Lock(nullptr);
  }
  u64 GetDeviceRevision() const override { return 1; }

  void EnumerateDevices(std::vector<PadDevice>& out) override {
    out.clear();
    PadDevice device;
    device.id = kPad;
    device.name = "Keyboard and mouse";
    device.info.type = PadType::kX360;
    out.push_back(device);
  }

  Result<PadState> GetDeviceState(PadDeviceID) override {
    std::lock_guard lock(mutex_);
    PadState state;
    // Only while one of this program's windows has the focus.
    HWND window = GetForegroundWindow();
    DWORD process = 0;
    if (window) GetWindowThreadProcessId(window, &process);
    const bool focused = process == GetCurrentProcessId();
    Lock(focused ? window : nullptr);
    long dx = 0, dy = 0;
    if (mouse_) mouse_->Take(dx, dy);
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - last_).count();
    last_ = now;
    if (!focused) {
      rate_x_ = rate_y_ = 0;
      return state;
    }
    for (size_t i = 0; i < kControlCount; ++i)
      if (Down(i)) state.buttons |= kControls[i].button;
    state.left_trigger = Down(kLT) ? 255 : 0;
    state.right_trigger = Down(kRT) ? 255 : 0;
    double x = double(Down(kMoveRight)) - double(Down(kMoveLeft));
    double y = double(Down(kMoveUp)) - double(Down(kMoveDown));
    if (x && y) x *= 0.7071, y *= 0.7071;
    state.thumb_lx = Axis(x);
    state.thumb_ly = Axis(y);
    // Mouse speed as stick deflection, over about 25 ms so that frames without motion do not stop
    // the turn.
    if (dt > 0) {
      const double keep = std::exp(-dt / 0.025);
      rate_x_ = rate_x_ * keep + dx / dt * (1 - keep);
      rate_y_ = rate_y_ * keep + dy / dt * (1 - keep);
    }
    state.thumb_rx = Axis(Deflect(rate_x_));
    state.thumb_ry = Axis((settings_.mouse_invert ? 1 : -1) * Deflect(rate_y_));
    return state;
  }

  Result<PadCapabilities> GetDeviceCapabilities(PadDeviceID) override {
    PadCapabilities capabilities;
    capabilities.supported = {0xF3FF, 255, 255, 32767, 32767, 32767, 32767};
    return capabilities;
  }

  Result<void> SetDeviceVibration(PadDeviceID, const PadVibration&) override { return Ok(); }

 private:
  static i16 Axis(double v) { return i16(std::clamp(v, -1.0, 1.0) * 32767.0); }

  bool Down(size_t control) const {
    return std::any_of(keys_[control].begin(), keys_[control].end(),
                       [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; });
  }

  // Counts per second to deflection: past the games' dead zone at once, full at 1500 counts per
  // second with sensitivity 1.
  double Deflect(double rate) const {
    const double a = std::abs(rate) * settings_.mouse_sensitivity / 1500.0;
    if (a < 0.002) return 0;
    return std::copysign(settings_.mouse_deadzone + (1 - settings_.mouse_deadzone) * std::min(a, 1.0), rate);
  }

  // Keeps the cursor inside the game's window while it has the focus.
  void Lock(HWND window) {
    if (window) {
      RECT r;
      if (!GetClientRect(window, &r)) return;
      POINT a = {r.right / 2, r.bottom / 2};
      ClientToScreen(window, &a);
      const RECT clip = {a.x, a.y, a.x + 1, a.y + 1};
      ClipCursor(&clip);
      locked_ = true;
    } else if (locked_) {
      ClipCursor(nullptr);
      locked_ = false;
    }
  }

  std::mutex mutex_;
  std::array<std::vector<int>, kControlCount> keys_;
  std::unique_ptr<Mouse> mouse_;
  std::chrono::steady_clock::time_point last_;
  double rate_x_ = 0, rate_y_ = 0;
  Settings settings_;
  bool locked_ = false;
};

std::unique_ptr<IDriver> CreateDriver() { return std::make_unique<Driver>(); }

constexpr std::array<DriverRegistration, 1> kDrivers = {
    DriverRegistration{DriverType::kInput, "keyboard", {}, &CreateDriver}};

class Module final : public IModule, public IDriverModule {
 public:
  REX_MODULE_SERVES(::rex::IDriverModule)

  std::string_view GetName() const override { return "rexhid-keyboard"; }
  Result<void> Connect(IInterfaceFactory&) override { return Ok(); }
  void Disconnect() override {}
  Result<void> Init() override { return Ok(); }
  void Shutdown() override {}
  IInterface* QueryInterface(std::string_view name, u32 version) override {
    return FindModuleInterface<IDriverModule>(*this, name, version);
  }
  std::span<const DriverRegistration> GetDrivers() const override { return kDrivers; }
};

}  // namespace grawbox::keyboard

REX_DECLARE_MODULE(grawbox::keyboard::Module)
