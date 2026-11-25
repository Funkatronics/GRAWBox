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

namespace grawbox::keyboard {

using namespace rex;
using namespace rex::hid;

constexpr PadDeviceID kPad{0x4742584Bull};

enum Control : size_t {
  kUp, kDown, kLeft, kRight, kStart, kBack, kLS, kRS, kLB, kRB, kA, kB, kX, kY,
  kLT, kRT, kMoveUp, kMoveDown, kMoveLeft, kMoveRight, kControlCount
};

struct ControlInfo {
  const char* name;
  u16 button;
  const char* keys;
};

constexpr std::array<ControlInfo, kControlCount> kControls = {{
    {"dpad_up", kPadDPadUp, "UP 1"},
    {"dpad_down", kPadDPadDown, "DOWN 2"},
    {"dpad_left", kPadDPadLeft, "LEFT 3"},
    {"dpad_right", kPadDPadRight, "RIGHT 4"},
    {"start", kPadStart, "ESC"},
    {"back", kPadBack, "TAB M"},
    {"left_stick_press", kPadLeftThumb, "LSHIFT"},
    {"right_stick_press", kPadRightThumb, "MOUSE3 Z"},
    {"left_bumper", kPadLeftShoulder, "Q"},
    {"right_bumper", kPadRightShoulder, "F"},
    {"a", kPadA, "SPACE ENTER"},
    {"b", kPadB, "C BACKSPACE"},
    {"x", kPadX, "R"},
    {"y", kPadY, "E"},
    {"left_trigger", 0, "MOUSE2"},
    {"right_trigger", 0, "MOUSE1"},
    {"move_forward", 0, "W"},
    {"move_back", 0, "S"},
    {"move_left", 0, "A"},
    {"move_right", 0, "D"},
}};

int KeyFromName(std::string name) {
  for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
  if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0]))) return name[0];
  static const std::map<std::string, int> names = {
      {"MOUSE1", VK_LBUTTON},  {"MOUSE2", VK_RBUTTON}, {"MOUSE3", VK_MBUTTON},
      {"MOUSE4", VK_XBUTTON1}, {"MOUSE5", VK_XBUTTON2}, {"UP", VK_UP},
      {"DOWN", VK_DOWN},       {"LEFT", VK_LEFT},      {"RIGHT", VK_RIGHT},
      {"SPACE", VK_SPACE},     {"ENTER", VK_RETURN},   {"ESC", VK_ESCAPE},
      {"TAB", VK_TAB},         {"BACKSPACE", VK_BACK}, {"LSHIFT", VK_LSHIFT},
      {"RSHIFT", VK_RSHIFT},   {"LCTRL", VK_LCONTROL}, {"RCTRL", VK_RCONTROL},
      {"LALT", VK_LMENU},      {"RALT", VK_RMENU},     {"CAPS", VK_CAPITAL},
      {"INSERT", VK_INSERT},   {"DELETE", VK_DELETE},  {"HOME", VK_HOME},
      {"END", VK_END},         {"PAGEUP", VK_PRIOR},   {"PAGEDOWN", VK_NEXT}};
  if (const auto found = names.find(name); found != names.end()) return found->second;
  if (name.size() >= 2 && name[0] == 'F') {
    const int n = std::atoi(name.c_str() + 1);
    if (n >= 1 && n <= 24) return VK_F1 + n - 1;
  }
  return 0;
}

std::vector<int> ParseKeys(const std::string& text) {
  std::vector<int> keys;
  std::istringstream in(text);
  for (std::string name; in >> name;)
    if (const int key = KeyFromName(name)) keys.push_back(key);
  return keys;
}

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
    for (size_t i = 0; i < kControlCount; ++i) keys_[i] = ParseKeys(kControls[i].keys);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    Load(std::filesystem::path(path).parent_path() / "grawbox-keys.txt");
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
    state.thumb_ry = Axis((invert_ ? 1 : -1) * Deflect(rate_y_));
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
    const double a = std::abs(rate) * sensitivity_ / 1500.0;
    if (a < 0.002) return 0;
    return std::copysign(deadzone_ + (1 - deadzone_) * std::min(a, 1.0), rate);
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

  void Load(const std::filesystem::path& path) {
    std::ifstream file(path);
    for (std::string line; std::getline(file, line);) {
      line = line.substr(0, line.find('#'));
      const size_t equals = line.find('=');
      if (equals == std::string::npos) continue;
      std::istringstream name_in(line.substr(0, equals));
      std::string name;
      name_in >> name;
      const std::string value = line.substr(equals + 1);
      if (name == "mouse_sensitivity") sensitivity_ = std::clamp(std::atof(value.c_str()), 0.05, 20.0);
      else if (name == "mouse_deadzone") deadzone_ = std::clamp(std::atof(value.c_str()), 0.0, 0.9);
      else if (name == "mouse_invert") invert_ = std::atoi(value.c_str()) != 0;
      for (size_t i = 0; i < kControlCount; ++i)
        if (name == kControls[i].name) keys_[i] = ParseKeys(value);
    }
  }

  std::mutex mutex_;
  std::array<std::vector<int>, kControlCount> keys_;
  std::unique_ptr<Mouse> mouse_;
  std::chrono::steady_clock::time_point last_;
  double rate_x_ = 0, rate_y_ = 0;
  double sensitivity_ = 1.0, deadzone_ = 0.24;
  bool invert_ = false;
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
