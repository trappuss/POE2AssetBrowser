#include "bundle/Oodle.h"

// Declared by third_party/ooz/kraken.cpp (compiled into this target by CMakeLists.txt). ooz has no
// header of its own; this is its only exported symbol we use.
int Kraken_Decompress(const unsigned char* src, size_t src_len, unsigned char* dst, size_t dst_len);

namespace oodle {

int decompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
    if (!src || !dst || srcLen == 0 || dstLen == 0) return -1;
    return Kraken_Decompress(src, srcLen, dst, dstLen);
}

}  // namespace oodle
