#include "document/CompactArray.h"

#include <climits>

#include <lz4/lz4.h>

namespace colorizing::document::compact_storage {

std::vector<char> pack(const void* bytes, std::size_t size)
{
    if (size == 0 || size > static_cast<std::size_t>(LZ4_MAX_INPUT_SIZE)) {
        return {};
    }
    std::vector<char> packed(static_cast<std::size_t>(LZ4_compressBound(static_cast<int>(size))));
    const int length = LZ4_compress_default(static_cast<const char*>(bytes), packed.data(),
                                            static_cast<int>(size),
                                            static_cast<int>(packed.size()));
    if (length <= 0) {
        return {};
    }
    packed.resize(static_cast<std::size_t>(length));
    packed.shrink_to_fit();
    return packed;
}

bool unpack(const std::vector<char>& packed, void* bytes, std::size_t size)
{
    if (size == 0) {
        return true;
    }
    if (packed.empty() || size > static_cast<std::size_t>(INT_MAX)) {
        return false;
    }
    return LZ4_decompress_safe(packed.data(), static_cast<char*>(bytes),
                               static_cast<int>(packed.size()),
                               static_cast<int>(size)) == static_cast<int>(size);
}

}  // namespace colorizing::document::compact_storage
