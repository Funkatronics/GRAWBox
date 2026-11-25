// The games' distance haze drawn anew, ambient occlusion and sharpening: compute passes over the
// resolved scene, recorded just before the games' own haze pass, which then has nothing to add.
#pragma once

#include <d3d12.h>

#include <cstdint>
#include <memory>

namespace graw::native {

struct ResolvedImage;

// The haze pass's constants.
struct HazeConstants {
  float wave[4];   // vertex shader c8: the frequencies of the moving displacement
  float time[2];   // c9.xy
  float shift[2];  // c10.xy: the displacement's size, in texture coordinates
  float range;     // pixel shader c1.x
};

class PostProcess {
 public:
  PostProcess();
  ~PostProcess();

  // Whether the settings ask for anything.
  static bool Wanted();
  // Whether the haze is drawn here (--graw_native_haze=fixed) rather than by the game's pass.
  static bool DrawsHaze();

  // Records the passes: `scene` (k_8_8_8_8) is replaced by the result. Sets its own compute root
  // signature, pipelines and descriptor heap on the list. False (nothing recorded) when it cannot.
  bool Run(ID3D12Device* device, ID3D12GraphicsCommandList* list, uint64_t frame,
           const ResolvedImage& scene, const ResolvedImage& depth, const HazeConstants& haze);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace graw::native
