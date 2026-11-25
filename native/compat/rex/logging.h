#pragma once

#include <format>
#include <string>

namespace grawbox {
void Log(int level, const std::string& text);
}
#define GRAWBOX_LOG_(level, ...) ::grawbox::Log(level, ::std::format(__VA_ARGS__))
#define REXLOG_TRACE(...) ((void)0)
#define REXLOG_DEBUG(...) ((void)0)
#define REXLOG_INFO(...) GRAWBOX_LOG_(0, __VA_ARGS__)
#define REXLOG_WARN(...) GRAWBOX_LOG_(1, __VA_ARGS__)
#define REXLOG_ERROR(...) GRAWBOX_LOG_(2, __VA_ARGS__)
#define REXLOG_CRITICAL(...) GRAWBOX_LOG_(2, __VA_ARGS__)
#define REXGPU_TRACE(...) ((void)0)
#define REXGPU_DEBUG(...) ((void)0)
#define REXGPU_INFO(...) GRAWBOX_LOG_(0, __VA_ARGS__)
#define REXGPU_WARN(...) GRAWBOX_LOG_(1, __VA_ARGS__)
#define REXGPU_ERROR(...) GRAWBOX_LOG_(2, __VA_ARGS__)
#define REXGPU_CRITICAL(...) GRAWBOX_LOG_(2, __VA_ARGS__)
