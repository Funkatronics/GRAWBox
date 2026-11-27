// GRAW native renderer: the compiled shaders the games ship in NewYetiShadersX360.sdf, read from the
// player's own files at start-up (nothing is stored or shipped).
//
// The file (both games): a 24-byte header ("SDFX", version, two ids, the entry count, 0), then per
// entry an id, the size of a D3D shader container and 0, then the container. Its first field is
// 0x102A0E00 (pixel) or 0x102A0E01 (vertex) in GRAW 1, 0x102A1100 or 0x102A1101 in GRAW 2. GRAW 1:
// the microcode at field 1, its size in bytes field 2. GRAW 2: field 6 points to a record of the
// microcode's offset past field 1 and its size. The microcode is big-endian, as in guest memory, so
// its XXH3 is the renderer's shader hash (ShaderCache::Load).
//
// Pixel shaders are drawn as stored (about half of those the games draw are in the file); vertex
// shaders are patched for their vertex layout when bound, so drawn ones never match, but any of
// them can stand in when only a pixel shader's pipeline stage is warmed.
#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace graw::native {

class DiscShaders {
 public:
  struct Shader {
    bool pixel;
    std::vector<uint32_t> ucode;  // big-endian, as in guest memory
  };
  // Reads `file`; false (and nothing loaded) if it is missing or not such a file.
  bool Load(const std::filesystem::path& file);
  const Shader* Find(uint64_t hash) const {
    auto it = shaders_.find(hash);
    return it == shaders_.end() ? nullptr : &it->second;
  }
  // A vertex shader from the file, to stand in for a pipeline's own (nullptr if none).
  const Shader* stand_in_vertex() const { return stand_in_; }
  size_t pixel_count() const { return pixel_; }
  size_t vertex_count() const { return shaders_.size() - pixel_; }

 private:
  std::unordered_map<uint64_t, Shader> shaders_;
  const Shader* stand_in_ = nullptr;
  size_t pixel_ = 0;
};

}  // namespace graw::native
