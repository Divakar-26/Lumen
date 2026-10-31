#include "lumen/lumen.h"

namespace Lumen {
namespace {

std::uint64_t Mix(std::uint64_t value) noexcept {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

} // namespace

std::uint64_t MakeId(std::uint64_t parent_id, const char* key) noexcept {
    std::uint64_t hash = 14695981039346656037ULL ^ parent_id;
    if (key == nullptr) {
        return Mix(hash == 0 ? 1 : hash);
    }
    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(key); *cursor != 0; ++cursor) {
        hash ^= static_cast<std::uint64_t>(*cursor);
        hash *= 1099511628211ULL;
    }
    hash = Mix(hash);
    return hash == 0 ? 1 : hash;
}

std::uint64_t MakeId(std::uint64_t parent_id, std::uint64_t key) noexcept {
    const std::uint64_t hash = Mix(parent_id ^ Mix(key + 0x9e3779b97f4a7c15ULL));
    return hash == 0 ? 1 : hash;
}

} // namespace Lumen
