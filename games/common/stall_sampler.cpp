#include "stall_sampler.h"

#include <rex/logging/log_category.h>
#include <rex/ppc/recompiled_code.h>

#include <windows.h>
#include <winternl.h>

#include <algorithm>
#include <atomic>
#include <cctype>
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
constexpr size_t kStackCopy = 32 * 1024;  // bytes of the game thread's stack scanned per sample

// Never destroyed: the sampler thread is detached and may still run while the program exits.
struct State {
  std::atomic<int64_t> last_swap{0};  // Clock ticks of the last Heartbeat
  std::mutex mutex;                    // the fields below
  DWORD thread_id = 0;                 // the thread that swaps
  HANDLE thread = nullptr;
  uintptr_t stack_base = 0;            // its stack's upper end (from its TEB)
  std::map<std::string, int> samples;  // this frame's
  // Host address -> guest function, and the bounds of the translated code.
  std::vector<std::pair<uintptr_t, uint32_t>> functions;
  uintptr_t code_begin = 0, code_end = 0;
};
State& S() {
  static State* s = new State;
  return *s;
}

void BuildFunctionTable(State& s) {
  for (const PPCFuncMapping* m = kPPCFuncMappings; m->host; ++m)
    s.functions.push_back({reinterpret_cast<uintptr_t>(m->host), uint32_t(m->address)});
  std::sort(s.functions.begin(), s.functions.end());
  // The translated code is the executable's "rexguest" section (REX_RECOMPILED_CODE_SECTION).
  auto* image = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
  IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    if (!strncmp(reinterpret_cast<const char*>(section->Name), "rexguest", 8)) {
      s.code_begin = reinterpret_cast<uintptr_t>(image + section->VirtualAddress);
      s.code_end = s.code_begin + section->Misc.VirtualSize;
    }
}

// The game function at `address` (the nearest mapped start below it: approximate, since helper
// code in the section is credited to the function before it), else empty.
std::string GameFunction(const State& s, uintptr_t address) {
  if (address < s.code_begin || address >= s.code_end) return {};
  auto it = std::upper_bound(s.functions.begin(), s.functions.end(),
                             std::make_pair(address, ~uint32_t(0)));
  if (it == s.functions.begin()) return {};
  char text[16];
  std::snprintf(text, sizeof(text), "sub_%08X", std::prev(it)->second);
  return text;
}

std::string ModuleName(uintptr_t address) {
  HMODULE module = nullptr;
  wchar_t path[MAX_PATH] = {};
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(address), &module) ||
      !GetModuleFileNameW(module, path, MAX_PATH))
    return {};
  const wchar_t* name = wcsrchr(path, L'\\');
  name = name ? name + 1 : path;
  std::string ascii;
  for (; *name; ++name) ascii += *name < 128 ? char(*name) : '?';
  return ascii;
}

// Windows and the C++ runtime: a wait or lock in them is named by the module that called them.
bool SystemModule(const std::string& name) {
  std::string n;
  for (char ch : name) n += char(std::tolower(static_cast<unsigned char>(ch)));
  for (const char* m : {"ntdll.dll", "kernelbase.dll", "kernel32.dll", "msvcp140.dll",
                        "vcruntime140.dll", "vcruntime140_1.dll", "ucrtbase.dll"})
    if (n == m) return true;
  return false;
}

uintptr_t StackBase(HANDLE thread) {
  struct ThreadInfo {  // THREAD_BASIC_INFORMATION
    LONG exit_status;
    PVOID teb;
    CLIENT_ID client;
    ULONG_PTR affinity;
    LONG priority, base_priority;
  } info{};
  using Query = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
  static auto query = reinterpret_cast<Query>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
  NT_TIB tib{};
  SIZE_T read = 0;
  if (!query || query(thread, 0 /* ThreadBasicInformation */, &info, sizeof(info), nullptr) < 0 ||
      !ReadProcessMemory(GetCurrentProcess(), info.teb, &tib, sizeof(tib), &read))
    return 0;
  return reinterpret_cast<uintptr_t>(tib.StackBase);
}

void Sampler() {
  State& s = S();
  BuildFunctionTable(s);
  std::vector<uintptr_t> stack(kStackCopy / sizeof(uintptr_t));
  for (;;) {
    std::this_thread::sleep_for(kSampleEvery);
    const int64_t last = s.last_swap.load(std::memory_order_relaxed);
    if (Clock::now().time_since_epoch().count() - last <
        std::chrono::duration_cast<Clock::duration>(kSampleAfter).count())
      continue;
    HANDLE thread;
    uintptr_t stack_base;
    {
      std::lock_guard lock(s.mutex);
      thread = s.thread;
      stack_base = s.stack_base;
    }
    if (!thread) continue;
    // While suspended: only the registers and a copy of the stack (ReadProcessMemory fails
    // instead of faulting). Nothing that takes a lock the game thread might hold.
    if (SuspendThread(thread) == DWORD(-1)) continue;
    CONTEXT c{};
    c.ContextFlags = CONTEXT_CONTROL;
    size_t words = 0;
    if (GetThreadContext(thread, &c) && stack_base > c.Rsp) {
      const size_t bytes = std::min<size_t>(kStackCopy, stack_base - c.Rsp);
      SIZE_T read = 0;
      if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(c.Rsp), stack.data(),
                            bytes, &read))
        words = read / sizeof(uintptr_t);
    }
    ResumeThread(thread);
    if (!c.Rip) continue;
    // Where it is; if that is not a game function, the first of our modules and the first game
    // function found among the return addresses on its stack (a scan, so approximate):
    // "ntdll.dll < rexkernel.dll < sub_82101798".
    std::string place = GameFunction(s, uintptr_t(c.Rip));
    if (place.empty()) {
      place = ModuleName(uintptr_t(c.Rip));
      if (place.empty()) place = "unknown";
      std::string caller, game;
      for (size_t i = 0; i < words && game.empty(); ++i) {
        const uintptr_t v = stack[i];
        if (v < 0x10000) continue;
        game = GameFunction(s, v);
        if (game.empty() && caller.empty()) {
          const std::string m = ModuleName(v);
          if (!m.empty() && m != place && !SystemModule(m)) caller = m;
        }
      }
      if (!caller.empty()) place += " < " + caller;
      if (!game.empty()) place += " < " + game;
    }
    std::lock_guard lock(s.mutex);
    // Only if the frame is still the one being sampled (no swap in between).
    if (s.last_swap.load(std::memory_order_relaxed) == last) ++s.samples[place];
  }
}

}  // namespace

void Heartbeat() {
  State& s = S();
  const int64_t now = Clock::now().time_since_epoch().count();
  const int64_t last = s.last_swap.exchange(now, std::memory_order_relaxed);
  std::map<std::string, int> samples;
  {
    std::lock_guard lock(s.mutex);
    // Follow the thread that swaps, should it ever change.
    if (s.thread_id != GetCurrentThreadId()) {
      if (s.thread) CloseHandle(s.thread);
      s.thread = nullptr;
      s.thread_id = GetCurrentThreadId();
      if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &s.thread,
                          THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                          FALSE, 0))
        s.stack_base = StackBase(s.thread);
      else
        REXLOG_WARN("GRAW stall: cannot watch the game thread ({}), no stall lines",
                    GetLastError());
    }
    samples.swap(s.samples);
  }
  if (!last) {
    std::thread(Sampler).detach();
    return;
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
    if (i) list += ", ";
    list += top[i].second + " " + std::to_string(100 * top[i].first / total) + "%";
  }
  REXLOG_INFO("GRAW stall: frame took {:.0f} ms, {} samples of the game thread after {} ms: {}",
              std::chrono::duration<double, std::milli>(frame).count(), total,
              std::chrono::duration_cast<std::chrono::milliseconds>(kSampleAfter).count(), list);
}

}  // namespace grawbox::stall
