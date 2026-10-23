#include "document/ComponentId.h"

#include <mutex>
#include <random>

namespace colorizing::document {

ComponentId ComponentId::generate()
{
    // One generator for the process, seeded from the system; UUIDs come from several threads
    // only rarely (a load on a worker thread).
    static std::mutex mutex;
    static std::mt19937_64 engine = []() {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device(), device(), device(), device(),
                           device()};
        return std::mt19937_64(seed);
    }();
    const std::lock_guard<std::mutex> lock(mutex);
    ComponentId id{engine(), engine()};
    // Version 4 (random) and the RFC 4122 variant.
    id.high = (id.high & ~0xf000ULL) | 0x4000ULL;
    id.low = (id.low & ~(0xc0ULL << 56)) | (0x80ULL << 56);
    return id;
}

std::string ComponentId::toString() const
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    text.reserve(36);
    const auto put = [&text](uint64_t value, int digits) {
        for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
            text.push_back(kDigits[(value >> shift) & 0xf]);
        }
    };
    put(high >> 32, 8);
    text.push_back('-');
    put((high >> 16) & 0xffff, 4);
    text.push_back('-');
    put(high & 0xffff, 4);
    text.push_back('-');
    put(low >> 48, 4);
    text.push_back('-');
    put(low & 0xffffffffffffULL, 12);
    return text;
}

std::optional<ComponentId> ComponentId::fromString(std::string_view text)
{
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' ||
        text[23] != '-') {
        return std::nullopt;
    }
    ComponentId id;
    int digits = 0;
    for (const char character : text) {
        if (character == '-') {
            continue;
        }
        uint64_t value = 0;
        if (character >= '0' && character <= '9') {
            value = static_cast<uint64_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            value = static_cast<uint64_t>(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
            value = static_cast<uint64_t>(character - 'A' + 10);
        } else {
            return std::nullopt;
        }
        uint64_t& half = digits < 16 ? id.high : id.low;
        half = (half << 4) | value;
        ++digits;
    }
    if (digits != 32) {
        return std::nullopt;
    }
    return id;
}

uint64_t contentHash(std::string_view bytes)
{
    // Eight bytes at a time, little endian, each multiplied in and rotated; murmur3's
    // finalizer mixes the result.
    constexpr uint64_t kPrime = 0x9e3779b97f4a7c15ULL;
    uint64_t hash = 0xcbf29ce484222325ULL ^ (bytes.size() * kPrime);
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    std::size_t left = bytes.size();
    while (left >= 8) {
        uint64_t word = 0;
        for (int i = 7; i >= 0; --i) {
            word = (word << 8) | data[i];
        }
        hash = ((hash ^ word) * kPrime);
        hash = (hash << 31) | (hash >> 33);
        data += 8;
        left -= 8;
    }
    uint64_t tail = 0;
    for (std::size_t i = left; i > 0; --i) {
        tail = (tail << 8) | data[i - 1];
    }
    hash = (hash ^ tail) * kPrime;
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    // 0 stands for "no hash yet".
    return hash == 0 ? 1 : hash;
}

}  // namespace colorizing::document
