// Scripted controller for unattended tests, as a ReXGlue input driver.
// Build as rexhid-autopilot and select it with --hid.drivers=autopilot. The environment variable
// GRAWBOX_PAD_SCRIPT names the script. Not part of any release.
//
// One event per line, '#' starts a comment, times are seconds since the driver started:
//   <time> <button>[+<button>...] [hold <seconds>]
//   <time> stick <lx> <ly> <rx> <ry> hold <seconds>     -1 to 1
//   <time> trigger <lt> <rt> hold <seconds>             0 to 1

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <rex/driver.h>
#include <rex/driver_context.h>
#include <rex/driver_module.h>
#include <rex/driver_registration.h>
#include <rex/hid/input_driver.h>
#include <rex/hid/pad.h>
#include <rex/module.h>

namespace grawbox::autopilot {

using namespace rex;
using namespace rex::hid;

struct Event {
  double begin = 0;
  double end = 0;
  PadState state;
};

constexpr PadDeviceID kPad{0x47425831ull};
constexpr double kDefaultHold = 0.15;

u16 ButtonFromName(const std::string& name) {
  static const std::map<std::string, u16> names = {
      {"UP", kPadDPadUp},           {"DOWN", kPadDPadDown},
      {"LEFT", kPadDPadLeft},       {"RIGHT", kPadDPadRight},
      {"START", kPadStart},         {"BACK", kPadBack},
      {"LS", kPadLeftThumb},        {"RS", kPadRightThumb},
      {"LB", kPadLeftShoulder},     {"RB", kPadRightShoulder},
      {"A", kPadA},                 {"B", kPadB},
      {"X", kPadX},                 {"Y", kPadY}};
  const auto found = names.find(name);
  return found == names.end() ? 0 : found->second;
}

i16 Axis(double value) {
  return static_cast<i16>(std::clamp(value, -1.0, 1.0) * 32767.0);
}

u8 Trigger(double value) {
  return static_cast<u8>(std::clamp(value, 0.0, 1.0) * 255.0);
}

bool ParseLine(const std::string& line, Event& event) {
  std::istringstream in(line.substr(0, line.find('#')));
  std::string kind;
  if (!(in >> event.begin >> kind)) {
    return false;
  }
  if (kind == "stick") {
    double lx, ly, rx, ry;
    if (!(in >> lx >> ly >> rx >> ry)) {
      return false;
    }
    event.state.thumb_lx = Axis(lx);
    event.state.thumb_ly = Axis(ly);
    event.state.thumb_rx = Axis(rx);
    event.state.thumb_ry = Axis(ry);
  } else if (kind == "trigger") {
    double left, right;
    if (!(in >> left >> right)) {
      return false;
    }
    event.state.left_trigger = Trigger(left);
    event.state.right_trigger = Trigger(right);
  } else {
    std::istringstream buttons(kind);
    for (std::string name; std::getline(buttons, name, '+');) {
      const u16 bit = ButtonFromName(name);
      if (!bit) {
        return false;
      }
      event.state.buttons |= bit;
    }
  }
  double hold = kDefaultHold;
  if (std::string word; in >> word) {
    if (word != "hold" || !(in >> hold)) {
      return false;
    }
  }
  event.end = event.begin + hold;
  return true;
}

class Driver final : public IInputDriver {
 public:
  std::string_view GetName() const override { return "autopilot"; }

  Result<void> Initialize(IDriverContext&) override {
    start_ = std::chrono::steady_clock::now();
    const char* path = std::getenv("GRAWBOX_PAD_SCRIPT");
    std::ifstream file(path ? path : "");
    for (std::string line; std::getline(file, line);) {
      if (Event event; ParseLine(line, event)) {
        events_.push_back(event);
      }
    }
    return Ok();
  }

  void Shutdown() override {}
  u64 GetDeviceRevision() const override { return 1; }

  void EnumerateDevices(std::vector<PadDevice>& out) override {
    out.clear();
    PadDevice device;
    device.id = kPad;
    device.name = "GRAWBox autopilot";
    device.info.type = PadType::kX360;
    out.push_back(device);
  }

  Result<PadState> GetDeviceState(PadDeviceID) override {
    const double now =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    PadState merged;
    for (const Event& event : events_) {
      if (now < event.begin || now >= event.end) {
        continue;
      }
      merged.buttons |= event.state.buttons;
      merged.left_trigger = std::max(merged.left_trigger, event.state.left_trigger);
      merged.right_trigger = std::max(merged.right_trigger, event.state.right_trigger);
      if (event.state.thumb_lx) merged.thumb_lx = event.state.thumb_lx;
      if (event.state.thumb_ly) merged.thumb_ly = event.state.thumb_ly;
      if (event.state.thumb_rx) merged.thumb_rx = event.state.thumb_rx;
      if (event.state.thumb_ry) merged.thumb_ry = event.state.thumb_ry;
    }
    return merged;
  }

  Result<PadCapabilities> GetDeviceCapabilities(PadDeviceID) override {
    PadCapabilities capabilities;
    capabilities.supported = {0xF3FF, 255, 255, 32767, 32767, 32767, 32767};
    return capabilities;
  }

  Result<void> SetDeviceVibration(PadDeviceID, const PadVibration&) override { return Ok(); }

 private:
  std::chrono::steady_clock::time_point start_;
  std::vector<Event> events_;
};

std::unique_ptr<IDriver> CreateDriver() {
  return std::make_unique<Driver>();
}

constexpr std::array<DriverRegistration, 1> kDrivers = {
    DriverRegistration{DriverType::kInput, "autopilot", {}, &CreateDriver}};

class Module final : public IModule, public IDriverModule {
 public:
  REX_MODULE_SERVES(::rex::IDriverModule)

  std::string_view GetName() const override { return "rexhid-autopilot"; }
  Result<void> Connect(IInterfaceFactory&) override { return Ok(); }
  void Disconnect() override {}
  Result<void> Init() override { return Ok(); }
  void Shutdown() override {}
  IInterface* QueryInterface(std::string_view name, u32 version) override {
    return FindModuleInterface<IDriverModule>(*this, name, version);
  }
  std::span<const DriverRegistration> GetDrivers() const override { return kDrivers; }
};

}  // namespace grawbox::autopilot

REX_DECLARE_MODULE(grawbox::autopilot::Module)
