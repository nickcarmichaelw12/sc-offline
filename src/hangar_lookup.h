#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hangarlookup {
inline constexpr unsigned char Prologue[] = {
    0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,
    0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,
    0x24,0x78,0xff,0xff,0xff,0x48,0x81,0xec,0x88,0x01,0x00,0x00
};
inline constexpr std::size_t Extent = 0x66f;

// Common compiler prologues are not function identities. Require the service
// call and the already resolved, typed future binder before counting a match.
inline unsigned char* Find(unsigned char* text, std::size_t size,
                           const void* binder, int& matches) {
    matches = 0;
    if (!text || !binder || size < Extent) return nullptr;
    unsigned char* found = nullptr;
    for (std::size_t i = 0; i <= size-Extent; ++i) {
        const void* hit = std::memchr(text+i, Prologue[0], size-Extent-i+1);
        if (!hit) break;
        i = static_cast<const unsigned char*>(hit)-text;
        auto* p = text+i;
        if (std::memcmp(p, Prologue, sizeof(Prologue)) || p[0x5e8] != 0xff
            || p[0x5e9] != 0x50 || p[0x5ea] != 0x10 || p[0x66a] != 0xe8) continue;
        std::int32_t displacement;
        std::memcpy(&displacement, p+0x66b, sizeof(displacement));
        const auto target = reinterpret_cast<std::uintptr_t>(p+Extent) + displacement;
        if (target != reinterpret_cast<std::uintptr_t>(binder)) continue;
        found = p;
        ++matches;
    }
    return matches == 1 ? found : nullptr;
}
}
