// The controls of the keyboard and mouse driver, their default keys and grawbox-keys.txt. The game
// program reads the same file, to show the keys in the games' button prompts.
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace grawbox::keys {

enum Control : size_t {
  kUp, kDown, kLeft, kRight, kStart, kBack, kLS, kRS, kLB, kRB, kA, kB, kX, kY,
  kLT, kRT, kMoveUp, kMoveDown, kMoveLeft, kMoveRight, kControlCount
};

struct ControlInfo {
  const char* name;
  uint16_t button;  // rex::hid::PadButton
  const char* keys;
};

// Both games: A reloads, B is the inventory (a tap swaps weapons), X vision, Y acts, and the left
// stick's press is the stance.
inline constexpr std::array<ControlInfo, kControlCount> kControls = {{
    {"dpad_up", 0x0001, "UP 3 WHEELUP"},
    {"dpad_down", 0x0002, "DOWN 4 WHEELDOWN"},
    {"dpad_left", 0x0004, "LEFT"},
    {"dpad_right", 0x0008, "TAB RIGHT"},
    {"start", 0x0010, "ESC"},
    {"back", 0x0020, "LALT M"},
    {"left_stick_press", 0x0040, "C LCTRL"},
    {"right_stick_press", 0x0080, "MOUSE3 Z"},
    {"left_bumper", 0x0100, "Q"},
    {"right_bumper", 0x0200, "F"},
    {"a", 0x1000, "R ENTER"},
    {"b", 0x2000, "1 2 G BACKSPACE"},
    {"x", 0x4000, "V N"},
    {"y", 0x8000, "E SPACE"},
    {"left_trigger", 0, "MOUSE2"},
    {"right_trigger", 0, "MOUSE1"},
    {"move_forward", 0, "W"},
    {"move_back", 0, "S"},
    {"move_left", 0, "A"},
    {"move_right", 0, "D"},
}};

// The wheel is a key that is down for a moment per notch.
inline constexpr int kWheelUp = 0x100, kWheelDown = 0x101;

// Windows virtual keys by name.
inline int KeyFromName(std::string name) {
  for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
  if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0]))) return name[0];
  static const std::map<std::string, int> names = {
      {"MOUSE1", 0x01},  {"MOUSE2", 0x02}, {"MOUSE3", 0x04}, {"MOUSE4", 0x05},
      {"MOUSE5", 0x06},  {"UP", 0x26},     {"DOWN", 0x28},   {"LEFT", 0x25},
      {"RIGHT", 0x27},   {"SPACE", 0x20},  {"ENTER", 0x0D},  {"ESC", 0x1B},
      {"TAB", 0x09},     {"BACKSPACE", 0x08}, {"LSHIFT", 0xA0}, {"RSHIFT", 0xA1},
      {"LCTRL", 0xA2},   {"RCTRL", 0xA3},  {"LALT", 0xA4},   {"RALT", 0xA5},
      {"CAPS", 0x14},    {"INSERT", 0x2D}, {"DELETE", 0x2E}, {"HOME", 0x24},
      {"END", 0x23},     {"PAGEUP", 0x21}, {"PAGEDOWN", 0x22}, {"WHEELUP", kWheelUp},
      {"WHEELDOWN", kWheelDown}};
  if (const auto found = names.find(name); found != names.end()) return found->second;
  if (name.size() >= 2 && name[0] == 'F') {
    const int n = std::atoi(name.c_str() + 1);
    if (n >= 1 && n <= 24) return 0x70 + n - 1;
  }
  return 0;
}

// What a key cap shows.
inline std::string Label(std::string name) {
  for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
  static const std::map<std::string, std::string> labels = {
      {"MOUSE1", "LMB"},   {"MOUSE2", "RMB"},   {"MOUSE3", "MMB"},   {"MOUSE4", "M4"},
      {"MOUSE5", "M5"},    {"LSHIFT", "SHIFT"}, {"RSHIFT", "SHIFT"}, {"LCTRL", "CTRL"},
      {"RCTRL", "CTRL"},   {"LALT", "ALT"},     {"RALT", "ALT"},     {"BACKSPACE", "BKSP"},
      {"PAGEUP", "PGUP"},  {"PAGEDOWN", "PGDN"}, {"INSERT", "INS"},  {"DELETE", "DEL"},
      {"WHEELUP", "WH UP"}, {"WHEELDOWN", "WH DN"}};
  const auto found = labels.find(name);
  return found == labels.end() ? name : found->second;
}

struct Settings {
  std::array<std::vector<std::string>, kControlCount> keys;  // names that KeyFromName knows
  double mouse_sensitivity = 1.0;
  double mouse_deadzone = 0.24;
  bool mouse_invert = false;
};

inline std::vector<std::string> ParseKeys(const std::string& text) {
  std::vector<std::string> keys;
  std::istringstream in(text);
  for (std::string name; in >> name;)
    if (KeyFromName(name)) keys.push_back(name);
  return keys;
}

inline Settings Load(const std::filesystem::path& path) {
  Settings s;
  for (size_t i = 0; i < kControlCount; ++i) s.keys[i] = ParseKeys(kControls[i].keys);
  std::ifstream file(path);
  for (std::string line; std::getline(file, line);) {
    line = line.substr(0, line.find('#'));
    const size_t equals = line.find('=');
    if (equals == std::string::npos) continue;
    std::istringstream name_in(line.substr(0, equals));
    std::string name;
    name_in >> name;
    const std::string value = line.substr(equals + 1);
    if (name == "mouse_sensitivity")
      s.mouse_sensitivity = std::clamp(std::atof(value.c_str()), 0.05, 20.0);
    else if (name == "mouse_deadzone")
      s.mouse_deadzone = std::clamp(std::atof(value.c_str()), 0.0, 0.9);
    else if (name == "mouse_invert")
      s.mouse_invert = std::atoi(value.c_str()) != 0;
    for (size_t i = 0; i < kControlCount; ++i)
      if (name == kControls[i].name) s.keys[i] = ParseKeys(value);
  }
  return s;
}

// The prompts' labels as the renderer takes them: "a=SPACE;b=C;...".
inline std::string PromptLabels(const Settings& s) {
  auto first = [&](size_t i) { return s.keys[i].empty() ? std::string() : Label(s.keys[i][0]); };
  std::string out;
  for (size_t i = 0; i < kControlCount; ++i) out += std::string(kControls[i].name) + "=" + first(i) + ";";
  out += "left_stick=" + first(kMoveUp) + first(kMoveLeft) + first(kMoveDown) + first(kMoveRight) + ";";
  out += "right_stick=MOUSE;";
  // In the menus, the last of each: ENTER and BACKSPACE by default.
  auto last = [&](size_t i) { return s.keys[i].empty() ? std::string() : Label(s.keys[i].back()); };
  out += "menu_a=" + last(kA) + ";menu_b=" + last(kB) + ";";
  return out;
}

}  // namespace grawbox::keys
