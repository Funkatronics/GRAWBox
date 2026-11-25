// Key caps in place of the games' controller buttons: drawn into the button textures as they are
// uploaded. The textures are known by the hash of their guest data.
#pragma once

#include "texture_layout.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace graw::native {

// After a texture's data was untiled to `dst` (its first subresource at `base`): draws the key caps
// when it is a button texture and keyboard prompts are on. `guest` is the base level as the guest
// has it. Any thread.
void ApplyPrompts(const TextureKey& key, const uint8_t* guest, size_t guest_bytes, uint8_t* dst,
                  const HostSubresource& base);

// The small button textures are too coarse for key caps at high resolutions: they are replaced by
// larger ones. PromptSize: whether the texture could be one. BuildPrompts: if it is, its picture
// with the key caps, kPromptScale times as large each way, as tightly packed RGBA8. `bytes`: what
// the texture is known by, as above.
constexpr uint32_t kPromptScale = 4;
bool PromptSize(const TextureKey& key);
bool BuildPrompts(const TextureKey& key, const GuestTextureMemory& memory, size_t bytes,
                  std::vector<uint8_t>* rgba);

}  // namespace graw::native
