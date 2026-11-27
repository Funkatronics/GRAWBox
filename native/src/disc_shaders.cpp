#include "disc_shaders.h"

#include "pipeline_store.h"  // ReadWholeFile

#include <rex/logging.h>

#include <xxhash.h>

#include <cstring>

namespace graw::native {

namespace {
uint32_t Be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
}  // namespace

bool DiscShaders::Load(const std::filesystem::path& file) {
  const std::vector<uint8_t> data = ReadWholeFile(file);
  if (data.size() < 24 || std::memcmp(data.data(), "SDFX", 4)) {
    REXLOG_WARN("GRAW native shaders: {} is not a shader file of the games", file.string());
    return false;
  }
  const uint32_t count = Be32(&data[0x10]);
  size_t pos = 0x18;
  std::unordered_map<uint64_t, Shader> shaders;
  std::unordered_map<uint32_t, uint64_t> entries;
  size_t pixel = 0, bad = 0;
  for (uint32_t i = 0; i < count; ++i) {
    if (data.size() - pos < 12) break;
    const uint32_t size = Be32(&data[pos + 4]);
    const size_t at = pos + 12;
    if (data.size() - at < size) break;
    pos = at + size;
    const uint8_t* c = &data[at];
    const uint32_t magic = size >= 28 ? Be32(c) : 0;
    uint64_t offset = size >= 28 ? Be32(c + 4) : 0;
    uint32_t bytes = size >= 28 ? Be32(c + 8) : 0;
    if ((magic & ~1u) == 0x102A1100u) {
      // Version 0x11 (GRAW 2): field 6 points to a record of the code's offset past field 1's
      // and its size, as COGBox reads Gears' containers.
      const uint32_t record = Be32(c + 24);
      if (record <= size - 8) {
        offset += Be32(c + record);  // 64-bit: a corrupt sum must not wrap past the checks
        bytes = Be32(c + record + 4);
      } else {
        bytes = 0;
      }
    } else if ((magic & ~1u) != 0x102A0E00u) {
      bytes = 0;  // not a shader container (0x0E, GRAW 1: the code at field 1, its size field 2)
    }
    if (!bytes || bytes % 4 || offset > size || size - offset < bytes) {
      ++bad;
      continue;
    }
    const uint64_t hash = XXH3_64bits(c + offset, bytes);
    Shader s;
    s.pixel = !(magic & 1);
    s.ucode.resize(bytes / 4);
    std::memcpy(s.ucode.data(), c + offset, bytes);
    entries[i] = hash;
    if (shaders.emplace(hash, std::move(s)).second && !(magic & 1)) ++pixel;
  }
  shaders_ = std::move(shaders);
  entries_ = std::move(entries);
  pixel_ = pixel;
  stand_in_ = nullptr;
  for (const auto& [hash, s] : shaders_)
    if (!s.pixel) {
      stand_in_ = &s;
      break;
    }
  REXLOG_INFO("GRAW native shaders: {} pixel and {} vertex shaders from {} ({} entries, {} unread)",
              pixel_, vertex_count(), file.string(), count, bad);
  return !shaders_.empty();
}

}  // namespace graw::native
