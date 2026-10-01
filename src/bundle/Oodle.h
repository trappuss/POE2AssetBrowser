#pragma once
#include <cstddef>
#include <cstdint>

// The one entry point into the bundled Oodle decoder (third_party/ooz, powzix's open-source
// Kraken / Mermaid / Leviathan implementation). Every PoE2 bundle block decodes through here.
//
// The decoder may write up to 64 bytes PAST dstLen (measured behaviour of ooz, and documented by
// the ggpk.discussion wiki for the real Oodle as well). Callers over-allocate by kOverrun and pass
// the real size as dstLen.
namespace oodle {

constexpr size_t kOverrun = 64;

// Decompress one Oodle-compressed block. Returns the number of bytes produced (== dstLen on
// success) or -1 on failure. dst must have room for dstLen + kOverrun bytes.
int decompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen);

}  // namespace oodle
