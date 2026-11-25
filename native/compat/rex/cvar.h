// Options for the renderer's sources, which were written for the SDK's cvars. Values come from the
// host's option callback the first time each is read.
#pragma once

#include <cstdint>
#include <string>

namespace rex::cvar {
enum class Lifecycle { kHotReload, kRequiresRestart, kInitOnly };
struct Flag {
  Flag lifecycle(Lifecycle) const { return *this; }
  template <typename A, typename B>
  Flag range(A, B) const { return *this; }
};
bool Lookup(const char* name, std::string& value);
inline void Load(const char* name, bool& v) {
  if (std::string s; Lookup(name, s)) v = s == "true" || s == "1";
}
inline void Load(const char* name, int32_t& v) {
  if (std::string s; Lookup(name, s)) v = std::atoi(s.c_str());
}
inline void Load(const char* name, double& v) {
  if (std::string s; Lookup(name, s)) v = std::atof(s.c_str());
}
inline void Load(const char* name, std::string& v) { Lookup(name, v); }
std::string GetFlagByName(const char* name);
bool SetFlagByName(const char* name, const std::string& value);
}  // namespace rex::cvar

#define REXCVAR_DECLARE(type, name) type& FLAGS_##name##_storage_()
#define REXCVAR_GET(name) (FLAGS_##name##_storage_())
#define REXCVAR_SET(name, value) (FLAGS_##name##_storage_() = (value))
#define GRAWBOX_DEFINE_(type, name, value)            \
  type& FLAGS_##name##_storage_() {                   \
    static type storage = [] {                        \
      type v = value;                                 \
      ::rex::cvar::Load(#name, v);                    \
      return v;                                       \
    }();                                              \
    return storage;                                   \
  }                                                   \
  [[maybe_unused]] static const ::rex::cvar::Flag flag_##name##_ = ::rex::cvar::Flag()
#define REXCVAR_DEFINE_BOOL(name, value, category, desc) GRAWBOX_DEFINE_(bool, name, value)
#define REXCVAR_DEFINE_INT32(name, value, category, desc) GRAWBOX_DEFINE_(int32_t, name, value)
#define REXCVAR_DEFINE_DOUBLE(name, value, category, desc) GRAWBOX_DEFINE_(double, name, value)
#define REXCVAR_DEFINE_STRING(name, value, category, desc) GRAWBOX_DEFINE_(std::string, name, value)
