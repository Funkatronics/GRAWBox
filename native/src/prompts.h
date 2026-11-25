// Key caps in place of the games' controller buttons: drawn into the button textures as they are
// uploaded. The textures are known by the hash of their guest data.
#pragma once

#include "texture_layout.h"

#include <cstddef>
#include <cstdint>

namespace graw::native {

// After a texture's data was untiled to `dst` (its first subresource at `base`): draws the key caps
// when it is a button texture and keyboard prompts are on. `guest` is the base level as the guest
// has it. Any thread.
void ApplyPrompts(const TextureKey& key, const uint8_t* guest, size_t guest_bytes, uint8_t* dst,
                  const HostSubresource& base);

}  // namespace graw::native
