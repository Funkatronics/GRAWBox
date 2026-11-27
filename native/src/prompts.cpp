#include "prompts.h"

#include <type_traits>
#include <unordered_map>
#include <rex/cvar.h>
#include <rex/logging.h>

#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <vector>

REXCVAR_DEFINE_STRING(graw_native_prompts, "", "GRAW native",
                      "Labels of the button prompts as <control>=<label>; each, empty for the games' "
                      "own buttons");

REXCVAR_DEFINE_STRING(graw_native_prompt_dump, "", "GRAW native",
                      "Debug: writes the small textures as uploaded to this folder, to find buttons");

namespace graw::native {
namespace {

namespace xenos = rex::graphics::xenos;

// A button in a texture: what to clear, where its cap goes (texels) and which control it is.
struct Cell {
  const char* control;
  int16_t clear_x, clear_y, clear_w, clear_h;
  int16_t x, y, w, h;
};

struct Sheet {
  uint64_t hash;
  xenos::TextureFormat format;
  uint32_t width, height;
  bool flipped;  // stored bottom up
  std::span<const Cell> cells;
};

// GRAW 2's font: the pad icons in its left columns.
constexpr Cell kGraw2Font[] = {
    {"left_stick", 43, 5, 43, 44, 44, 6, 41, 42},
    {"right_stick", 0, 22, 43, 43, 1, 23, 41, 41},
    {"move_forward", 43, 48, 43, 57, 44, 56, 41, 41},
    {"back", 0, 65, 43, 50, 1, 70, 42, 42},
    {"right_stick", 0, 115, 43, 56, 1, 122, 41, 41},
    {"start", 0, 171, 43, 51, 1, 176, 42, 42},
    {"dpad_down", 48, 201, 43, 56, 49, 208, 41, 41},
    {"left_trigger", 0, 257, 51, 41, 1, 258, 49, 40},
    {"right_trigger", 0, 298, 50, 42, 1, 299, 49, 40},
    {"move_back", 50, 284, 43, 56, 51, 291, 41, 41},
    {"b", 0, 375, 54, 37, 8, 376, 40, 36},
    {"left_bumper", 0, 412, 53, 37, 1, 413, 51, 36},
    {"right_bumper", 0, 449, 54, 37, 1, 450, 51, 36},
    {"right_stick", 54, 375, 42, 56, 54, 383, 41, 41},
    {"dpad_up", 54, 431, 42, 55, 54, 438, 41, 41},
    {"x", 0, 486, 54, 37, 8, 487, 40, 36},
    {"dpad_right", 0, 523, 55, 43, 6, 524, 41, 41},
    {"right_stick", 55, 522, 43, 43, 56, 523, 41, 41},
    {"dpad_left", 0, 602, 57, 43, 8, 603, 41, 41},
    {"a", 56, 628, 38, 72, 57, 645, 36, 40},
    {"right_stick", 0, 644, 56, 43, 7, 645, 41, 41},
    {"right_stick", 0, 687, 56, 43, 7, 688, 41, 41},
    {"y", 56, 680, 38, 57, 57, 696, 36, 40},
    {"move_right", 0, 730, 56, 43, 7, 731, 41, 41},
    {"move_left", 0, 773, 58, 43, 8, 774, 41, 41},
    {"right_stick_press", 0, 815, 63, 52, 3, 826, 57, 32},
    {"left_stick_press", 0, 866, 64, 52, 3, 876, 57, 34},
    {"left_stick_press", 0, 917, 64, 40, 3, 920, 57, 34},
    {"right_stick_press", 0, 957, 64, 64, 3, 972, 57, 34},
};

// GRAW 2's HUD atlas.
constexpr Cell kGraw2Hud[] = {
    {"a", 872, 170, 31, 31, 873, 171, 29, 29},
    {"x", 904, 170, 31, 31, 905, 171, 29, 29},
    {"b", 902, 459, 31, 31, 903, 460, 29, 29},
    {"y", 625, 268, 30, 31, 626, 269, 28, 29},
    {"left_stick", 870, 136, 34, 33, 871, 137, 32, 32},
    {"right_stick", 869, 251, 33, 34, 870, 252, 32, 32},
    {"right_trigger", 934, 201, 29, 34, 935, 202, 27, 32},
    {"left_trigger", 933, 445, 29, 34, 934, 446, 27, 32},
    {"left_stick_press", 907, 327, 28, 28, 908, 328, 26, 26},
    {"right_stick_press", 907, 356, 28, 28, 908, 357, 26, 26},
    {"right_stick_press", 702, 87, 42, 25, 704, 88, 38, 22},
    {"back", 835, 231, 34, 42, 836, 236, 32, 30},
};

// GRAW 1: some HUD icons, and a texture each for the menus' buttons.
constexpr Cell kGraw1Hud[] = {
    {"right_stick", 2, 66, 28, 28, 3, 67, 26, 26},
    {"left_stick", 34, 66, 28, 28, 35, 67, 26, 26},
    {"right_stick_press", 0, 97, 32, 31, 1, 100, 30, 26},
};
constexpr Cell kWholeA[] = {{"menu_a", 0, 0, 32, 32, 1, 1, 28, 28}};
constexpr Cell kWholeB[] = {{"menu_b", 0, 0, 32, 32, 1, 1, 28, 28}};

// Entries with hash 0 only make textures of that size show up in the log.
constexpr Sheet kSheets[] = {
    {0xE8F40274CE21F2AEull, xenos::TextureFormat::k_8_8_8_8, 256, 1024, true, kGraw2Font},
    {0x97B5A183CB0ABE72ull, xenos::TextureFormat::k_DXT2_3, 1024, 512, false, kGraw2Hud},
    {0x07B5160213F13C00ull, xenos::TextureFormat::k_DXT2_3, 32, 32, false, kWholeA},
    {0x34D222E8E813A554ull, xenos::TextureFormat::k_DXT2_3, 32, 32, false, kWholeB},
    {0xA9CDF1F3B2007BF3ull, xenos::TextureFormat::k_DXT2_3, 128, 128, false, kGraw1Hud},
};

const std::map<std::string, std::string>& Labels() {
  static const std::map<std::string, std::string> labels = [] {
    std::map<std::string, std::string> m;
    const std::string& s = REXCVAR_GET(graw_native_prompts);
    for (size_t i = 0; i < s.size();) {
      const size_t end = std::min(s.find(';', i), s.size());
      const size_t equals = s.find('=', i);
      if (equals < end) m[s.substr(i, equals - i)] = s.substr(equals + 1, end - equals - 1);
      i = end + 1;
    }
    return m;
  }();
  return labels;
}

// A key cap of w x h pixels: luminance and alpha per pixel.
struct Cap {
  int w = 0, h = 0;
  std::vector<uint8_t> luminance, alpha;
};

Cap DrawCap(const std::string& text, int w, int h) {
  // Drawn at 4x and averaged, so that the corners and the small letters stay smooth.
  constexpr int kScale = 4;
  Cap cap;
  cap.w = w;
  cap.h = h;
  cap.luminance.resize(size_t(w) * h);
  cap.alpha.resize(size_t(w) * h);
  const int W = w * kScale, H = h * kScale;
  BITMAPINFO info = {};
  info.bmiHeader = {sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB};
  void* bits = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dc || !bitmap) {
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    return cap;
  }
  HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
  // Blue marks what is outside the cap.
  RECT all = {0, 0, W, H};
  HBRUSH outside = CreateSolidBrush(RGB(0, 0, 255));
  FillRect(dc, &all, outside);
  HBRUSH face = CreateSolidBrush(RGB(38, 38, 38));
  HPEN edge = CreatePen(PS_SOLID, std::max(3, kScale * std::min(w, h) / 16), RGB(225, 225, 225));
  HGDIOBJ old_brush = SelectObject(dc, face), old_pen = SelectObject(dc, edge);
  const int inset = kScale;
  const int round = std::min(W, H) / 3;
  RoundRect(dc, inset, inset, W - inset, H - inset, round, round);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  int size = H * 11 / 16;
  HFONT font = nullptr;
  for (; size > 8; size -= 2) {
    font = CreateFontW(-size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                       VARIABLE_PITCH | FF_SWISS, L"Arial Narrow");
    HGDIOBJ old = SelectObject(dc, font);
    SIZE extent = {};
    GetTextExtentPoint32A(dc, text.c_str(), int(text.size()), &extent);
    if (extent.cx <= W * 13 / 16) {
      DrawTextA(dc, text.c_str(), int(text.size()), &all, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(dc, old);
      break;
    }
    SelectObject(dc, old);
    DeleteObject(font);
    font = nullptr;
  }
  GdiFlush();
  const uint8_t* p = static_cast<const uint8_t*>(bits);  // BGRA
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      uint32_t sum = 0, inside = 0;
      for (int sy = 0; sy < kScale; ++sy)
        for (int sx = 0; sx < kScale; ++sx) {
          const uint8_t* t = p + ((size_t(y) * kScale + sy) * W + size_t(x) * kScale + sx) * 4;
          if (t[0] > 200 && t[1] < 20 && t[2] < 20) continue;
          sum += t[1];
          ++inside;
        }
      cap.luminance[size_t(y) * w + x] = inside ? uint8_t(sum / inside) : 0;
      cap.alpha[size_t(y) * w + x] = uint8_t(inside * 255 / (kScale * kScale));
    }
  if (font) DeleteObject(font);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_bitmap);
  DeleteObject(edge);
  DeleteObject(face);
  DeleteObject(outside);
  DeleteObject(bitmap);
  DeleteDC(dc);
  return cap;
}

struct Pixel {
  uint8_t r, g, b, a;
};

// The sheet's texels around its cells, read and written a block at a time.
class Dxt3Block {
 public:
  explicit Dxt3Block(uint8_t* data) : data_(data) {
    std::memcpy(bytes_, data, 16);  // write-combined memory: read once
    uint16_t c[2];
    uint32_t bits;
    std::memcpy(c, bytes_ + 8, 4);
    std::memcpy(&bits, bytes_ + 12, 4);
    int p[4][3];
    for (int i = 0; i < 2; ++i) {
      p[i][0] = (c[i] >> 11) * 255 / 31;
      p[i][1] = ((c[i] >> 5) & 63) * 255 / 63;
      p[i][2] = (c[i] & 31) * 255 / 31;
    }
    for (int k = 0; k < 3; ++k) {
      p[2][k] = (2 * p[0][k] + p[1][k]) / 3;
      p[3][k] = (p[0][k] + 2 * p[1][k]) / 3;
    }
    for (int i = 0; i < 16; ++i) {
      const int* q = p[(bits >> (2 * i)) & 3];
      pixels[i] = {uint8_t(q[0]), uint8_t(q[1]), uint8_t(q[2]),
                   uint8_t(((bytes_[i / 2] >> (4 * (i & 1))) & 15) * 17)};
    }
  }
  void Store() const {
    uint8_t out[16] = {};
    auto luminance = [](const Pixel& p) { return 2 * p.r + 5 * p.g + p.b; };
    int lo = 0, hi = 0;
    for (int i = 0; i < 16; ++i) {
      out[i / 2] |= uint8_t(((pixels[i].a + 8) / 17) << (4 * (i & 1)));
      if (luminance(pixels[i]) < luminance(pixels[lo])) lo = i;
      if (luminance(pixels[i]) > luminance(pixels[hi])) hi = i;
    }
    auto pack = [](const Pixel& p) {
      return uint16_t((p.r * 31 + 127) / 255 << 11 | (p.g * 63 + 127) / 255 << 5 |
                      (p.b * 31 + 127) / 255);
    };
    const uint16_t c0 = pack(pixels[hi]), c1 = pack(pixels[lo]);
    const int a = luminance(pixels[hi]), b = luminance(pixels[lo]);
    uint32_t bits = 0;
    for (int i = 0; i < 16; ++i) {
      // 0: c0, 2: 2/3 c0, 3: 1/3 c0, 1: c1
      const int r = a == b ? 0 : ((a - luminance(pixels[i])) * 6 + (a - b)) / (2 * (a - b));
      static constexpr uint32_t kIndex[4] = {0, 2, 3, 1};
      bits |= kIndex[std::clamp(r, 0, 3)] << (2 * i);
    }
    std::memcpy(out + 8, &c0, 2);
    std::memcpy(out + 10, &c1, 2);
    std::memcpy(out + 12, &bits, 4);
    std::memcpy(data_, out, 16);
  }
  Pixel pixels[16];

 private:
  uint8_t* data_;
  uint8_t bytes_[16];
};

void DrawCell(const Sheet& sheet, const Cell& cell, uint8_t* dst, const HostSubresource& base) {
  const auto label = Labels().find(cell.control);
  if (label == Labels().end() || label->second.empty()) return;
  const Cap cap = DrawCap(label->second, cell.w, cell.h);
  auto texel = [&](int x, int y) -> Pixel {
    const int cx = x - cell.x;
    const int cy = sheet.flipped ? cell.y + cell.h - 1 - y : y - cell.y;
    if (cx < 0 || cy < 0 || cx >= cap.w || cy >= cap.h) return {0, 0, 0, 0};
    const uint8_t l = cap.luminance[size_t(cy) * cap.w + cx];
    return {l, l, l, cap.alpha[size_t(cy) * cap.w + cx]};
  };
  const int x0 = std::max<int>(cell.clear_x, 0), y0 = std::max<int>(cell.clear_y, 0);
  const int x1 = std::min<int>(cell.clear_x + cell.clear_w, int(sheet.width));
  const int y1 = std::min<int>(cell.clear_y + cell.clear_h, int(sheet.height));
  if (sheet.format == xenos::TextureFormat::k_8_8_8_8) {
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        const Pixel p = texel(x, y);
        std::memcpy(dst + size_t(y) * base.row_pitch + size_t(x) * 4, &p, 4);
      }
    return;
  }
  for (int by = y0 / 4; by <= (y1 - 1) / 4; ++by)
    for (int bx = x0 / 4; bx <= (x1 - 1) / 4; ++bx) {
      Dxt3Block block(dst + size_t(by) * base.row_pitch + size_t(bx) * 16);
      for (int i = 0; i < 16; ++i) {
        const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
        if (x >= x0 && x < x1 && y >= y0 && y < y1) block.pixels[i] = texel(x, y);
      }
      block.Store();
    }
}

}  // namespace

void ApplyPrompts(const TextureKey& key, const uint8_t* guest, size_t guest_bytes, uint8_t* dst,
                  const HostSubresource& base) {
  static const std::string dump = REXCVAR_GET(graw_native_prompt_dump);
  if (!dump.empty() && guest && key.width <= 128 && key.height <= 128 && key.width >= 16 &&
      key.height >= 16 && key.format == xenos::TextureFormat::k_DXT2_3) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    const uint64_t hash = XXH3_64bits(guest, guest_bytes);
    char name[64];
    std::snprintf(name, sizeof(name), "\\%016llX_%ux%u.bin", (unsigned long long)hash, key.width,
                  key.height);
    if (FILE* file = std::fopen((dump + name).c_str(), "wb")) {
      for (uint32_t y = 0; y < base.block_rows; ++y)
        std::fwrite(dst + size_t(y) * base.row_pitch, 1, base.row_bytes, file);
      std::fclose(file);
    }
  }
  if (!PromptsShown() || !guest || base.level) return;
  bool candidate = false;
  for (const Sheet& s : kSheets)
    candidate |= s.format == key.format && s.width == key.width && s.height == key.height;
  if (!candidate) return;
  const uint64_t hash = XXH3_64bits(guest, guest_bytes);
  for (const Sheet& s : kSheets) {
    if (s.hash != hash || s.format != key.format || s.width != key.width || s.height != key.height)
      continue;
    for (const Cell& c : s.cells) DrawCell(s, c, dst, base);
    REXLOG_INFO("GRAW native prompts: {} key caps drawn into {}x{} {:016X}", s.cells.size(),
                key.width, key.height, hash);
    return;
  }
  // Buttons not known yet show up here.
  static std::mutex mutex;
  static std::set<uint64_t> logged;
  std::lock_guard lock(mutex);
  if (logged.size() < 400 && logged.insert(hash).second)
    REXLOG_INFO("GRAW native prompts: unknown {}x{} format {} at {:08X}: {:016X}", key.width,
                key.height, uint32_t(key.format), key.base_page << 12, hash);
}

namespace {
std::atomic<bool> g_keyboard_last{true};

// XInputGetState, loaded on first use (no import of a particular XInput version).
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, void*);
XInputGetStateFn XInputGetState() {
  static const XInputGetStateFn fn = [] {
    for (const wchar_t* name : {L"xinput1_4.dll", L"xinput9_1_0.dll"})
      if (HMODULE m = LoadLibraryW(name))
        return reinterpret_cast<XInputGetStateFn>(GetProcAddress(m, "XInputGetState"));
    return XInputGetStateFn(nullptr);
  }();
  return fn;
}

// A controller used since the last call: its state changed in a way a person makes (a button or
// trigger, or a stick moved well off its centre), not the small drift of a resting stick.
// `changed`: any change at all, the small ones included.
bool PadUsed(bool* changed) {
  *changed = false;
  const XInputGetStateFn get = XInputGetState();
  if (!get) return false;
  struct State {
    DWORD packet;
    WORD buttons;
    BYTE lt, rt;
    SHORT lx, ly, rx, ry;
  };
  static State last[4] = {};
  bool used = false;
  for (DWORD i = 0; i < 4; ++i) {
    State s;
    if (get(i, &s) != ERROR_SUCCESS || s.packet == last[i].packet) continue;
    *changed = true;
    const State& o = last[i];
    auto moved = [](int a, int b, int by) { return std::abs(a - b) > by; };
    auto off = [](SHORT v) { return v > 12000 || v < -12000; };
    if (s.buttons != o.buttons || moved(s.lt, o.lt, 40) || moved(s.rt, o.rt, 40) ||
        ((off(s.lx) || off(s.ly) || off(s.rx) || off(s.ry)) &&
         (moved(s.lx, o.lx, 4000) || moved(s.ly, o.ly, 4000) || moved(s.rx, o.rx, 4000) ||
          moved(s.ry, o.ry, 4000))))
      used = true;
    last[i] = s;
  }
  return used;
}

// The same for the controllers SDL reads (the games' sdl3 input driver: DualSense, Switch and other
// pads XInput doesn't see). Only if that driver already loaded SDL3 (nothing is loaded here); SDL's
// gamepad getters take its joystick lock, so they can be read from this thread.
bool SdlPadUsed(bool* changed) {
  using Id = uint32_t;
  struct Sdl {
    Id* (*get_gamepads)(int*) = nullptr;
    void* (*from_id)(Id) = nullptr;
    bool (*button)(void*, int) = nullptr;
    int16_t (*axis)(void*, int) = nullptr;
    void (*free)(void*) = nullptr;
  };
  static const Sdl sdl = [] {
    Sdl s;
    if (HMODULE m = GetModuleHandleW(L"SDL3.dll")) {
      auto get = [m](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(m, name));
      };
      get(s.get_gamepads, "SDL_GetGamepads");
      get(s.from_id, "SDL_GetGamepadFromID");
      get(s.button, "SDL_GetGamepadButton");
      get(s.axis, "SDL_GetGamepadAxis");
      get(s.free, "SDL_free");
    }
    return s;
  }();
  if (!sdl.get_gamepads || !sdl.from_id || !sdl.button || !sdl.axis || !sdl.free) return false;
  struct State {
    uint32_t buttons = 0;  // the 15 standard buttons (SDL_GAMEPAD_BUTTON_SOUTH ... DPAD_RIGHT)
    int16_t axes[6] = {};  // left x/y, right x/y, left/right trigger
    bool operator==(const State&) const = default;
  };
  static std::unordered_map<Id, State> last;
  int count = 0;
  Id* ids = sdl.get_gamepads(&count);
  if (!ids) return false;
  bool used = false;
  for (int i = 0; i < count; ++i) {
    void* pad = sdl.from_id(ids[i]);  // only pads the driver opened
    if (!pad) continue;
    State s;
    for (int b = 0; b < 15; ++b) s.buttons |= uint32_t(sdl.button(pad, b)) << b;
    for (int a = 0; a < 6; ++a) s.axes[a] = sdl.axis(pad, a);
    auto it = last.find(ids[i]);
    if (it == last.end()) {
      last.emplace(ids[i], s);
      continue;
    }
    const State& o = it->second;
    if (s == o) continue;
    *changed = true;
    auto moved = [](int a, int b, int by) { return std::abs(a - b) > by; };
    auto off = [](int16_t v) { return v > 12000 || v < -12000; };
    bool stick = false;
    for (int a = 0; a < 4; ++a) stick |= off(s.axes[a]) && moved(s.axes[a], o.axes[a], 4000);
    if (s.buttons != o.buttons || moved(s.axes[4], o.axes[4], 5000) ||
        moved(s.axes[5], o.axes[5], 5000) || stick)
      used = true;
    it->second = s;
  }
  sdl.free(ids);
  return used;
}

// A key or mouse button pressed since the last call.
bool KeyPressed() {
  static bool down[256] = {};
  bool pressed = false;
  for (int k = 1; k < 256; ++k) {
    if (k >= 0xC3 && k <= 0xDA) continue;  // VK_GAMEPAD_*: Windows can report a controller as keys
    const bool now = (GetAsyncKeyState(k) & 0x8000) != 0;
    if (now && !down[k]) pressed = true;
    down[k] = now;
  }
  return pressed;
}
}  // namespace

bool PromptSheet(const TextureKey& key) {
  if (REXCVAR_GET(graw_native_prompts).empty()) return false;
  for (const Sheet& s : kSheets)
    if (s.format == key.format && s.width == key.width && s.height == key.height) return true;
  return false;
}

bool PromptsShown() {
  if (Labels().empty()) return false;
  return !Labels().count("@auto") || g_keyboard_last.load(std::memory_order_relaxed);
}

bool PollPromptDevice() {
  if (!Labels().count("@auto")) return false;
  // The last device used wins. A controller also counts as input to Windows (GetLastInputInfo):
  // that is mouse motion only while the controller's state did not change in the last half second.
  static DWORD last_input = 0;
  static ULONGLONG pad_at = 0;
  const ULONGLONG now = GetTickCount64();
  bool pad_changed = false, sdl_changed = false;
  const bool pad = PadUsed(&pad_changed) | SdlPadUsed(&sdl_changed);
  pad_changed |= sdl_changed;
  if (pad_changed) pad_at = now;
  LASTINPUTINFO info = {sizeof(info)};
  const bool any = GetLastInputInfo(&info) && last_input && info.dwTime != last_input;
  last_input = info.dwTime;
  const bool keys = KeyPressed() || (any && !pad && now - pad_at > 500);
  const bool before = g_keyboard_last.load(std::memory_order_relaxed);
  const bool keyboard = pad ? false : keys ? true : before;
  if (keyboard == before) return false;
  g_keyboard_last.store(keyboard, std::memory_order_relaxed);
  REXLOG_INFO("GRAW native prompts: {}", keyboard ? "keys" : "controller buttons");
  return true;
}

bool PromptSize(const TextureKey& key) {
  if (REXCVAR_GET(graw_native_prompts).empty() || key.mip_levels() != 1 || key.width > 128 ||
      key.format != xenos::TextureFormat::k_DXT2_3)
    return false;
  for (const Sheet& s : kSheets)
    if (s.format == key.format && s.width == key.width && s.height == key.height) return true;
  return false;
}

bool BuildPrompts(const TextureKey& key, const GuestTextureMemory& memory, size_t bytes,
                  std::vector<uint8_t>* rgba) {
  if (!memory.base || !PromptsShown()) return false;
  const uint64_t hash = XXH3_64bits(memory.base, bytes);
  const Sheet* sheet = nullptr;
  for (const Sheet& s : kSheets)
    if (s.hash == hash && s.width == key.width && s.height == key.height) sheet = &s;
  if (!sheet) return false;
  std::vector<HostSubresource> subresources;
  std::vector<uint8_t> blocks(size_t(GetHostFootprints(key, true, &subresources))), scratch;
  if (subresources.empty() || !UntileTexture(key, memory, subresources, blocks.data(), &scratch))
    return false;
  const HostSubresource& base = subresources[0];
  const int w = int(key.width), h = int(key.height), n = int(kPromptScale);
  std::vector<Pixel> small_picture(size_t(w) * h);
  for (int y = 0; y < h; y += 4)
    for (int x = 0; x < w; x += 4) {
      const Dxt3Block block(blocks.data() + base.offset + size_t(y / 4) * base.row_pitch +
                            size_t(x / 4) * 16);
      for (int i = 0; i < 16; ++i)
        if (x + i % 4 < w && y + i / 4 < h)
          small_picture[size_t(y + i / 4) * w + x + i % 4] = block.pixels[i];
    }
  // Enlarged as the sampler would.
  rgba->resize(size_t(w) * n * h * n * 4);
  for (int y = 0; y < h * n; ++y)
    for (int x = 0; x < w * n; ++x) {
      const float fx = std::clamp((x + 0.5f) / n - 0.5f, 0.0f, float(w - 1));
      const float fy = std::clamp((y + 0.5f) / n - 0.5f, 0.0f, float(h - 1));
      const int x0 = int(fx), y0 = int(fy);
      const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
      const float tx = fx - x0, ty = fy - y0;
      const Pixel a = small_picture[size_t(y0) * w + x0], b = small_picture[size_t(y0) * w + x1];
      const Pixel c = small_picture[size_t(y1) * w + x0], d = small_picture[size_t(y1) * w + x1];
      auto mix = [&](uint8_t Pixel::*m) {
        const float top = a.*m * (1 - tx) + b.*m * tx, low = c.*m * (1 - tx) + d.*m * tx;
        return uint8_t(top * (1 - ty) + low * ty + 0.5f);
      };
      uint8_t* out = rgba->data() + (size_t(y) * w * n + x) * 4;
      out[0] = mix(&Pixel::r);
      out[1] = mix(&Pixel::g);
      out[2] = mix(&Pixel::b);
      out[3] = mix(&Pixel::a);
    }
  Sheet large = *sheet;
  large.format = xenos::TextureFormat::k_8_8_8_8;
  large.width *= kPromptScale;
  large.height *= kPromptScale;
  HostSubresource rows = {};
  rows.row_pitch = large.width * 4;
  for (Cell c : sheet->cells) {
    for (int16_t* v : {&c.clear_x, &c.clear_y, &c.clear_w, &c.clear_h, &c.x, &c.y, &c.w, &c.h})
      *v = int16_t(*v * n);
    DrawCell(large, c, rgba->data(), rows);
  }
  REXLOG_INFO("GRAW native prompts: {}x{} {:016X} replaced at {}x{}", key.width, key.height, hash,
              large.width, large.height);
  return true;
}

}  // namespace graw::native
