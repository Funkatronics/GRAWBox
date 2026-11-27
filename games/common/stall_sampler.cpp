#include "stall_sampler.h"

#include <rex/logging/log_category.h>
#include <rex/ppc/recompiled_code.h>

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace grawbox::stall {

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kSampleAfter = std::chrono::milliseconds(50);
constexpr auto kSampleEvery = std::chrono::milliseconds(2);
constexpr auto kLogFrom = std::chrono::milliseconds(100);
constexpr int kLinesPerMinute = 20;

std::atomic<int64_t> g_last_swap{0};  // Clock ticks of the last Heartbeat
HANDLE g_game_thread = nullptr;

std::mutex g_mutex;  // the samples of the current frame
std::map<std::string, int> g_samples;

// Host address -> guest function, and the bounds of the translated code.
std::vector<std::pair<uintptr_t, uint32_t>> g_functions;
uintptr_t g_code_begin = 0, g_code_end = 0;

void BuildFunctionTable() {
  for (const PPCFuncMapping* m = kPPCFuncMappings; m->host; ++m)
    g_functions.push_back({reinterpret_cast<uintptr_t>(m->host), uint32_t(m->address)});
  std::sort(g_functions.begin(), g_functions.end());
  // The translated code is the executable's "rexguest" section (REX_RECOMPILED_CODE_SECTION).
  auto* image = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
  IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s)
    if (!strncmp(reinterpret_cast<const char*>(s->Name), "rexguest", 8)) {
      g_code_begin = reinterpret_cast<uintptr_t>(image + s->VirtualAddress);
      g_code_end = g_code_begin + s->Misc.VirtualSize;
    }
}

std::string Place(uintptr_t rip) {
  char text[64];
  if (rip >= g_code_begin && rip < g_code_end) {
    auto it = std::upper_bound(g_functions.begin(), g_functions.end(),
                               std::make_pair(rip, ~uint32_t(0)));
    if (it != g_functions.begin()) {
      std::snprintf(text, sizeof(text), "sub_%08X", std::prev(it)->second);
      return text;
    }
  }
  HMODULE module = nullptr;
  wchar_t path[MAX_PATH] = {};
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(rip), &module) &&
      GetModuleFileNameW(module, path, MAX_PATH)) {
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    std::string ascii;
    for (; *name; ++name) ascii += *name < 128 ? char(*name) : '?';
    return ascii;
  }
  return "unknown";
}

// Windows and the C++ runtime: a wait or lock in them is named by the module that called them.
bool SystemModule(const std::string& name) {
  std::string n;
  for (char ch : name) n += char(std::tolower(static_cast<unsigned char>(ch)));
  for (const char* s : {"ntdll.dll", "kernelbase.dll", "kernel32.dll", "msvcp140.dll",
                        "vcruntime140.dll", "vcruntime140_1.dll", "ucrtbase.dll"})
    if (n == s) return true;
  return false;
}

void Sampler() {
  BuildFunctionTable();
  for (;;) {
    std::this_thread::sleep_for(kSampleEvery);
    const int64_t last = g_last_swap.load(std::memory_order_relaxed);
    if (Clock::now().time_since_epoch().count() - last <
        std::chrono::duration_cast<Clock::duration>(kSampleAfter).count())
      continue;
    // The instruction pointer and, by unwinding the stack while the thread is suspended (as
    // sampling profilers do; nothing is allocated meanwhile), up to kFrames return addresses.
    constexpr int kFrames = 24;
    uintptr_t frames[kFrames];
    int count = 0;
    if (SuspendThread(g_game_thread) == DWORD(-1)) continue;
    CONTEXT c{};
    c.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(g_game_thread, &c)) {
      while (count < kFrames && c.Rip) {
        frames[count++] = uintptr_t(c.Rip);
        DWORD64 image = 0;
        PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c.Rip, &image, nullptr);
        if (!f) {  // a leaf function: its return address is on top of the stack
          c.Rip = *reinterpret_cast<DWORD64*>(c.Rsp);
          c.Rsp += 8;
          continue;
        }
        void* handler_data = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.Rip, f, &c, &handler_data, &frame, nullptr);
      }
    }
    ResumeThread(g_game_thread);
    if (!count) continue;
    // Where it is, the first of our modules or the game below a system module, and the first
    // game function: "ntdll.dll < rexkernel.dll < sub_82101798".
    std::string place = Place(frames[0]);
    std::string caller, game;
    for (int i = 1; i < count && game.empty(); ++i) {
      const std::string p = Place(frames[i]);
      if (p.starts_with("sub_")) game = p;
      else if (caller.empty() && p != place && !SystemModule(p))
        caller = p;
    }
    if (!place.starts_with("sub_")) {
      if (!caller.empty()) place += " < " + caller;
      if (!game.empty()) place += " < " + game;
    }
    std::lock_guard lock(g_mutex);
    // Only if the frame is still the one being sampled (no swap in between).
    if (g_last_swap.load(std::memory_order_relaxed) == last) ++g_samples[place];
  }
}

}  // namespace

void Heartbeat() {
  const int64_t now = Clock::now().time_since_epoch().count();
  const int64_t last = g_last_swap.exchange(now, std::memory_order_relaxed);
  if (!last) {
    // The first frame: the sampler watches this thread from now on.
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_game_thread,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0);
    std::thread(Sampler).detach();
    return;
  }
  std::map<std::string, int> samples;
  {
    std::lock_guard lock(g_mutex);
    samples.swap(g_samples);
  }
  const auto frame = Clock::duration(now - last);
  if (frame < kLogFrom || samples.empty()) return;
  static int64_t window = 0;
  static int lines = 0;
  if (now - window > std::chrono::duration_cast<Clock::duration>(std::chrono::minutes(1)).count()) {
    window = now;
    lines = 0;
  }
  if (++lines > kLinesPerMinute) return;
  std::vector<std::pair<int, std::string>> top;
  int total = 0;
  for (auto& [place, n] : samples) {
    top.push_back({n, place});
    total += n;
  }
  std::sort(top.rbegin(), top.rend());
  std::string list;
  for (size_t i = 0; i < top.size() && i < 6; ++i) {
    char item[96];
    std::snprintf(item, sizeof(item), "%s%s %d%%", i ? ", " : "", top[i].second.c_str(),
                  100 * top[i].first / total);
    list += item;
  }
  REXLOG_INFO("GRAW stall: frame took {:.0f} ms, {} samples of the game thread after 50 ms: {}",
              std::chrono::duration<double, std::milli>(frame).count(), total, list);
}

}  // namespace grawbox::stall
