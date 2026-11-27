#include "stall_sampler.h"

#include <rex/logging/log_category.h>
#include <rex/ppc/recompiled_code.h>

#include <windows.h>
#include <tlhelp32.h>
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
#include <tuple>
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
constexpr auto kOthersAfter = std::chrono::milliseconds(100);  // then the other threads too
constexpr uint64_t kOthersEvery = 5;  // ticks (10 ms) between samples of the other threads

// Never destroyed: the sampler thread is detached and may still run while the program exits.
struct State {
  std::atomic<int64_t> last_swap{0};  // Clock ticks of the last Heartbeat
  std::mutex mutex;                    // the fields below
  DWORD thread_id = 0;                 // the thread that swaps
  HANDLE thread = nullptr;
  uintptr_t stack_base = 0;            // its stack's upper end (from its TEB)
  // This frame's samples by where the thread was, each with the deeper game callers seen there.
  struct Place {
    int n = 0;
    std::map<std::string, int> deeper;
  };
  std::map<std::string, Place> samples;
  // During a stall of kOthersAfter or more: the process's other threads (WatchOthers).
  struct Other {
    HANDLE handle = nullptr;
    uintptr_t stack_base = 0;
    uint64_t cycles_start = 0;  // QueryThreadCycleTime when the watch began
    int64_t since = 0;          // Clock ticks when the watch began
    std::string name;           // its description, else the module it started in
    bool sampled = false;       // started in our code: suspended and sampled (others only timed)
    std::map<std::string, int> places;
  };
  std::map<DWORD, Other> others;
  int64_t others_frame = 0;  // the frame (last_swap) `others` belongs to
  // Handles of watches that ended: closed by the sampler thread, the only one that uses them.
  std::vector<HANDLE> retired;
  double cycles_per_second = 0;  // for QueryThreadCycleTime (calibrated at start)
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

// One sample of `thread`: where it is (`place`) and, below a wait, its deeper game callers.
bool SampleThread(State& s, HANDLE thread, uintptr_t stack_base, std::vector<uintptr_t>& stack,
                  std::string& place, std::string& deeper) {
  // While suspended: only the registers and a copy of the stack (ReadProcessMemory fails
  // instead of faulting). Nothing that takes a lock the game thread might hold.
  if (SuspendThread(thread) == DWORD(-1)) return false;
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
  if (!c.Rip) return false;
  // Where it is; if that is not a game function, the first of our modules and the first game
  // function found among the return addresses on its stack (a scan, so approximate), which
  // group the samples: "ntdll.dll < rexkernel.dll < sub_82101798". The next game functions
  // (callers of a wait wrapper, which say what the game waits for) are kept per group: the scan
  // can pick up stale addresses that deep, so they must not split a group.
  place = GameFunction(s, uintptr_t(c.Rip));
  deeper.clear();
  if (place.empty()) {
    place = ModuleName(uintptr_t(c.Rip));
    if (place.empty()) place = "unknown";
    // Up to kGameDepth game functions: a wait wrapper names only itself, its callers say what
    // the game waits for.
    constexpr int kGameDepth = 3;
    std::string caller;
    std::vector<std::string> games;
    for (size_t i = 0; i < words && games.size() < kGameDepth; ++i) {
      const uintptr_t v = stack[i];
      if (v < 0x10000) continue;
      if (std::string g = GameFunction(s, v); !g.empty()) {
        if (std::find(games.begin(), games.end(), g) == games.end()) games.push_back(std::move(g));
      } else if (games.empty() && caller.empty()) {
        const std::string m = ModuleName(v);
        if (!m.empty() && m != place && !SystemModule(m)) caller = m;
      }
    }
    if (!caller.empty()) place += " < " + caller;
    if (!games.empty()) place += " < " + games[0];
    for (size_t i = 1; i < games.size(); ++i) deeper += (i > 1 ? " < " : "") + games[i];
  }
  return true;
}
// CPU cycles the thread has used (QueryThreadCycleTime counts exactly, unlike GetThreadTimes,
// which moves in scheduler ticks of about 15.6 ms); false if it can't be read.
bool Cycles(HANDLE thread, uint64_t& cycles) {
  ULONG64 c = 0;
  if (!QueryThreadCycleTime(thread, &c)) return false;
  cycles = c;
  return true;
}

// Cycles per second, for turning a thread's cycles into a share of the time watched: the
// sampler thread's own cycles while it spins for 50 ms.
double CalibrateCycles() {
  uint64_t c0 = 0, c1 = 0;
  const auto t0 = Clock::now();
  if (!Cycles(GetCurrentThread(), c0)) return 0;
  while (Clock::now() - t0 < std::chrono::milliseconds(50)) {
  }
  if (!Cycles(GetCurrentThread(), c1)) return 0;
  return double(c1 - c0) / std::chrono::duration<double>(Clock::now() - t0).count();
}

// The module the thread started in ("" if unknown), from its Win32 start address.
std::string StartModule(HANDLE thread) {
  using Query = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
  static auto query = reinterpret_cast<Query>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
  PVOID start = nullptr;
  if (!query || query(thread, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof(start),
                      nullptr) < 0)
    return {};
  return ModuleName(reinterpret_cast<uintptr_t>(start));
}

// Our code: the game program, its runtime and renderer, and the C runtime's thread start, which
// std::thread uses (the runtime's guest threads and the renderer's workers start there; drivers
// link their own). Threads started there are suspended and sampled; driver, audio and thread-pool
// threads are only timed, so pausing them can't lengthen the stall being measured.
bool OurModule(const std::string& name) {
  std::string n;
  for (char ch : name) n += char(std::tolower(static_cast<unsigned char>(ch)));
  return n.starts_with("graw") || n.starts_with("rex") || n == "ucrtbase.dll";
}

// The process's other threads, once the game thread has been stalled for kOthersAfter: their
// names and CPU cycles so far, and for ours their stacks, for sampling until the next Swap. Kept
// only if `frame` is still the current one.
void WatchOthers(State& s, DWORD game_thread, int64_t frame) {
  std::map<DWORD, State::Other> others;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return;
  THREADENTRY32 e{};
  e.dwSize = sizeof(e);
  for (BOOL ok = Thread32First(snapshot, &e); ok; ok = Thread32Next(snapshot, &e)) {
    if (e.th32OwnerProcessID != GetCurrentProcessId() || e.th32ThreadID == game_thread ||
        e.th32ThreadID == GetCurrentThreadId())
      continue;
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                          FALSE, e.th32ThreadID);
    if (!h) continue;
    uint64_t cycles = 0;
    if (!Cycles(h, cycles)) {
      CloseHandle(h);
      continue;
    }
    State::Other& o = others[e.th32ThreadID];
    o.handle = h;
    o.cycles_start = cycles;
    o.since = Clock::now().time_since_epoch().count();
    PWSTR description = nullptr;
    if (SUCCEEDED(GetThreadDescription(h, &description)) && description) {
      for (const wchar_t* c = description; *c && o.name.size() < 40; ++c)
        o.name += *c < 128 ? char(*c) : '?';
      LocalFree(description);
    }
    const std::string module = StartModule(h);
    if (o.name.empty()) o.name = module.empty() ? "thread" : module;
    o.sampled = OurModule(module);
    if (o.sampled) o.stack_base = StackBase(h);
  }
  CloseHandle(snapshot);
  std::lock_guard lock(s.mutex);
  if (s.last_swap.load(std::memory_order_relaxed) != frame) {
    // The stall ended meanwhile: not for the next frame.
    for (auto& [id, o] : others) s.retired.push_back(o.handle);
    return;
  }
  for (auto& [id, o] : s.others) s.retired.push_back(o.handle);
  s.others = std::move(others);
  s.others_frame = frame;
}

void Sampler() {
  State& s = S();
  BuildFunctionTable(s);
  {
    const double rate = CalibrateCycles();
    std::lock_guard lock(s.mutex);
    s.cycles_per_second = rate;
  }
  std::vector<uintptr_t> stack(kStackCopy / sizeof(uintptr_t));
  std::string place, deeper;
  int64_t watched = 0;  // the frame (last swap) whose other threads are being sampled
  std::vector<HANDLE> retired;
  for (uint64_t tick = 0;; ++tick) {
    std::this_thread::sleep_for(kSampleEvery);
    // Handles of ended watches: only this thread samples with them, so only it closes them.
    {
      std::lock_guard lock(s.mutex);
      retired.swap(s.retired);
    }
    for (HANDLE h : retired) CloseHandle(h);
    retired.clear();
    const int64_t last = s.last_swap.load(std::memory_order_relaxed);
    const int64_t age = Clock::now().time_since_epoch().count() - last;
    if (age < std::chrono::duration_cast<Clock::duration>(kSampleAfter).count()) continue;
    HANDLE thread;
    uintptr_t stack_base;
    DWORD thread_id;
    {
      std::lock_guard lock(s.mutex);
      thread = s.thread;
      stack_base = s.stack_base;
      thread_id = s.thread_id;
    }
    if (!thread) continue;
    if (SampleThread(s, thread, stack_base, stack, place, deeper)) {
      std::lock_guard lock(s.mutex);
      // Only if the frame is still the one being sampled (no swap in between).
      if (s.last_swap.load(std::memory_order_relaxed) == last) {
        State::Place& p = s.samples[place];
        ++p.n;
        if (!deeper.empty()) ++p.deeper[deeper];
      }
    }
    // A long stall: what the other threads are doing meanwhile, every kOthersEvery ticks.
    if (age < std::chrono::duration_cast<Clock::duration>(kOthersAfter).count()) continue;
    if (watched != last) {
      WatchOthers(s, thread_id, last);
      watched = last;
    }
    if (tick % kOthersEvery) continue;
    std::vector<std::tuple<DWORD, HANDLE, uintptr_t>> others;
    {
      std::lock_guard lock(s.mutex);
      if (s.others_frame == last)
        for (auto& [id, o] : s.others)
          if (o.sampled) others.emplace_back(id, o.handle, o.stack_base);
    }
    for (auto& [id, handle, base] : others) {
      if (!SampleThread(s, handle, base, stack, place, deeper)) continue;
      std::lock_guard lock(s.mutex);
      if (s.last_swap.load(std::memory_order_relaxed) != last) break;
      if (auto it = s.others.find(id); it != s.others.end()) ++it->second.places[place];
    }
  }
}

}  // namespace

void Heartbeat() {
  State& s = S();
  const int64_t now = Clock::now().time_since_epoch().count();
  const int64_t last = s.last_swap.exchange(now, std::memory_order_relaxed);
  std::map<std::string, State::Place> samples;
  std::map<DWORD, State::Other> others;
  double cycles_per_second = 0;
  {
    std::lock_guard lock(s.mutex);
    // Follow the thread that swaps, should it ever change.
    if (s.thread_id != GetCurrentThreadId()) {
      if (s.thread) s.retired.push_back(s.thread);  // closed by the sampler, which may use it
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
    // The watched threads' cycles since the watch began, read while their handles are still open;
    // the handles go to the sampler thread to close.
    if (s.others_frame == last)
      for (auto& [id, o] : s.others) {
        uint64_t end = 0;
        if (Cycles(o.handle, end) && end >= o.cycles_start) {
          o.cycles_start = end - o.cycles_start;  // from here on: cycles used
          others.emplace(id, std::move(o));
        }
      }
    for (auto& [id, o] : s.others) s.retired.push_back(o.handle);
    s.others.clear();
    s.others_frame = 0;
    cycles_per_second = s.cycles_per_second;
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
  for (auto& [place, p] : samples) {
    top.push_back({p.n, place});
    total += p.n;
  }
  std::sort(top.rbegin(), top.rend());
  std::string list;
  for (size_t i = 0; i < top.size() && i < 6; ++i) {
    if (i) list += ", ";
    list += top[i].second + " " + std::to_string(100 * top[i].first / total) + "%";
    // Its most frequent deeper callers, with how often they were seen: "(via sub_X < sub_Y 80%)".
    const State::Place& p = samples[top[i].second];
    auto best = std::max_element(p.deeper.begin(), p.deeper.end(),
                                 [](const auto& a, const auto& b) { return a.second < b.second; });
    if (best != p.deeper.end())
      list += " (via " + best->first + " " + std::to_string(100 * best->second / p.n) + "%)";
  }
  // The other threads that used the most CPU while watched (from kOthersAfter into the stall),
  // with, for ours, where they were most often: "rexkernel.dll (1234) 92% CPU at sub_X 80%".
  std::string busy;
  if (cycles_per_second > 0) {
    std::vector<std::pair<uint64_t, DWORD>> by_cycles;
    for (auto& [id, o] : others) by_cycles.push_back({o.cycles_start, id});
    std::sort(by_cycles.rbegin(), by_cycles.rend());
    for (size_t i = 0; i < by_cycles.size() && i < 4; ++i) {
      const State::Other& o = others[by_cycles[i].second];
      const double seconds = std::chrono::duration<double>(Clock::duration(now - o.since)).count();
      if (seconds <= 0) continue;
      const int cpu = int(std::min(100.0 * double(by_cycles[i].first) / cycles_per_second / seconds,
                                   999.0));
      if (cpu < 5) break;
      if (!busy.empty()) busy += "; ";
      busy += o.name + " (" + std::to_string(by_cycles[i].second) + ") " + std::to_string(cpu) +
              "% CPU";
      int n = 0;
      for (auto& [p, c] : o.places) n += c;
      auto top = std::max_element(o.places.begin(), o.places.end(),
                                  [](const auto& a, const auto& b) { return a.second < b.second; });
      if (top != o.places.end())
        busy += " at " + top->first + " " + std::to_string(100 * top->second / n) + "%";
    }
  }
  REXLOG_INFO("GRAW stall: frame took {:.0f} ms, {} samples of the game thread after {} ms: {}",
              std::chrono::duration<double, std::milli>(frame).count(), total,
              std::chrono::duration_cast<std::chrono::milliseconds>(kSampleAfter).count(), list);
  if (!busy.empty()) REXLOG_INFO("GRAW stall: busiest other threads meanwhile: {}", busy);
}

}  // namespace grawbox::stall
