#include "report.h"

#include "build_id.h"

#include <rex/logging/log_category.h>
#include <rex/settings_registry.h>

#include <windows.h>
#include <dxgi.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace grawbox::report {

namespace {

namespace fs = std::filesystem;

std::string Utf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring Env(const wchar_t* name) {
  wchar_t buffer[MAX_PATH];
  const DWORD n = GetEnvironmentVariableW(name, buffer, MAX_PATH);
  return n && n < MAX_PATH ? std::wstring(buffer, n) : std::wstring();
}

// `whole`: only where it is not part of a longer word (a user name like "dev" must not turn
// "device" into "<user>ice"); a path is replaced wherever it appears.
// Case-insensitive (ASCII), as Windows paths and account names are: a log line with the profile
// folder in other case, or a user name written "Dev" for "dev", still matches.
size_t FindNoCase(const std::string& text, const std::string& what, size_t from) {
  auto lower = [](char c) { return char(std::tolower(static_cast<unsigned char>(c))); };
  for (size_t at = from; at + what.size() <= text.size(); ++at) {
    size_t i = 0;
    while (i < what.size() && lower(text[at + i]) == lower(what[i])) ++i;
    if (i == what.size()) return at;
  }
  return std::string::npos;
}

void ReplaceAll(std::string& text, const std::string& what, const std::string& with, bool whole) {
  if (what.size() < 2) return;  // a one-letter name would garble the text
  auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
  for (size_t at = 0; (at = FindNoCase(text, what, at)) != std::string::npos;) {
    const size_t end = at + what.size();
    if (whole && ((at && word(text[at - 1])) || (end < text.size() && word(text[end])))) {
      at = end;
      continue;
    }
    text.replace(at, what.size(), with);
    at += with.size();
  }
}

// The last `bytes` of a file, starting at a line.
std::string Tail(const fs::path& file, size_t bytes) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return {};
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  const std::streamoff from = size > std::streamoff(bytes) ? size - std::streamoff(bytes) : 0;
  in.seekg(from);
  std::string text(size_t(size - from), '\0');
  in.read(text.data(), std::streamsize(text.size()));
  if (from) text.erase(0, text.find('\n') + 1);
  return text;
}

void WriteText(const fs::path& file, const std::string& text) {
  std::ofstream(file, std::ios::binary) << Scrub(text);
}

std::string Info() {
  std::ostringstream o;
  o << "GRAWBox build: " << GRAWBOX_BUILD << "\n";
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  o << "Program: " << Utf8(fs::path(exe).filename().wstring()) << "\n";
  o << "Arguments: " << Utf8(GetCommandLineW()) << "\n";
  // Windows version (RtlGetVersion: GetVersionEx reports a compatibility value).
  using RtlGetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
  if (auto get = reinterpret_cast<RtlGetVersion>(
          GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) {
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    if (get(&v) == 0)
      o << "Windows: " << v.dwMajorVersion << "." << v.dwMinorVersion << "." << v.dwBuildNumber
        << "\n";
  }
  // CPU and memory.
  wchar_t cpu[128] = {};
  DWORD size = sizeof(cpu);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                   L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, cpu, &size) == ERROR_SUCCESS) {
    std::string name = Utf8(cpu);
    name.erase(name.find_last_not_of(' ') + 1);
    o << "CPU: " << name << " (" << std::thread::hardware_concurrency() << " threads)\n";
  }
  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof(memory);
  if (GlobalMemoryStatusEx(&memory))
    o << "Memory: " << memory.ullTotalPhys / (1024 * 1024) << " MB\n";
  // GPUs and their drivers.
  IDXGIFactory1* factory = nullptr;
  if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
      DXGI_ADAPTER_DESC1 d{};
      adapter->GetDesc1(&d);
      LARGE_INTEGER umd{};
      o << "GPU " << i << ": " << Utf8(d.Description) << ", " << d.DedicatedVideoMemory / (1024 * 1024)
        << " MB";
      if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
        o << ", driver " << HIWORD(umd.HighPart) << "." << LOWORD(umd.HighPart) << "."
          << HIWORD(umd.LowPart) << "." << LOWORD(umd.LowPart);
      o << "\n";
      adapter->Release();
    }
    factory->Release();
  }
  return o.str();
}

}  // namespace

std::string Scrub(std::string text) {
  // Longest first, so the profile folder goes before the user name inside it.
  const std::string profile = Utf8(Env(L"USERPROFILE"));
  const std::string user = Utf8(Env(L"USERNAME"));
  const std::string computer = Utf8(Env(L"COMPUTERNAME"));
  ReplaceAll(text, profile, "<profile>", false);
  std::string forward = profile;
  for (char& c : forward)
    if (c == '\\') c = '/';
  ReplaceAll(text, forward, "<profile>", false);
  ReplaceAll(text, user, "<user>", true);
  ReplaceAll(text, computer, "<computer>", true);
  return text;
}

void Write(void (*screenshot)(const char* path)) {
  // Next to the log (the launcher and the test scripts put it beside the game's folders).
  fs::path base;
  if (auto log = REX_SETTINGS().Find("log.file_name")) base = fs::path((*log)->GetText()).parent_path();
  if (base.empty()) {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    base = fs::path(exe).parent_path();
  }
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
  // Two reports in the same second (a key repeat, a test trigger) get their own folders.
  std::error_code ec;
  fs::path dir = base / "reports" / stamp;
  for (int i = 1; fs::exists(dir, ec) && i < 100; ++i)
    dir = base / "reports" / (std::string(stamp) + "-" + std::to_string(i));
  fs::create_directories(dir, ec);
  if (ec) {
    REXLOG_WARN("GRAW report: cannot create the report folder ({})", ec.message());
    return;
  }
  REXLOG_INFO("GRAW report: written at {} (build {})", stamp, GRAWBOX_BUILD);
  if (screenshot) screenshot(Utf8((dir / "screenshot.png").wstring()).c_str());
  WriteText(dir / "info.txt", Info());
  if (auto log = REX_SETTINGS().Find("log.file_name"); log && !(*log)->GetText().empty())
    WriteText(dir / "log-tail.txt", Tail(fs::path((*log)->GetText()), 2 << 20));
  const std::wstring csv = Env(L"GRAWBOX_NATIVE_CSV");
  if (!csv.empty()) {
    // The newest of frames.csv, frames.1.csv, ... (the renderer picks the first unused name).
    const fs::path first(csv);
    fs::path newest;
    fs::file_time_type newest_time{};
    for (const auto& e : fs::directory_iterator(first.parent_path(), ec)) {
      const std::wstring name = e.path().filename().wstring();
      if (name.starts_with(first.stem().wstring()) && e.path().extension() == L".csv" &&
          name.find(L"presents") == std::wstring::npos && e.last_write_time(ec) > newest_time) {
        newest = e.path();
        newest_time = e.last_write_time(ec);
      }
    }
    if (!newest.empty()) {
      std::string header;
      std::getline(std::ifstream(newest, std::ios::binary), header);
      std::string tail = Tail(newest, 1 << 20);
      if (!tail.starts_with(header)) tail = header + '\n' + tail;
      WriteText(dir / "frames-tail.csv", tail);
    }
  }
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  for (const auto& e : fs::directory_iterator(fs::path(exe).parent_path(), ec))
    if (e.path().filename().wstring().starts_with(L"grawbox-keys"))
      fs::copy_file(e.path(), dir / e.path().filename(), fs::copy_options::overwrite_existing, ec);
  MessageBeep(MB_OK);
}

}  // namespace grawbox::report
