#pragma once

#include <functional>

namespace rex::ui {
void RegisterBind(const char* name, const char* key, const char* description,
                  std::function<void()> callback);
void UnregisterBind(const char* name);
}  // namespace rex::ui
