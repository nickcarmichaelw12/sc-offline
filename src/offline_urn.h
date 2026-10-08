#pragma once
#include <cstdint>
#include <cstring>

// CL12660092 native URN: communal / entitlement / uuid. POD UUID storage
// avoids string ownership and floating-point comparison of identifier bits.
namespace offlineurn {
constexpr uint16_t Header = 0x160F;
constexpr uint8_t Kind = 3;
constexpr uint8_t Payload = 2;
constexpr uint64_t Marker = 0x53434F4600000000ull;
constexpr uint64_t Namespace = 0x4F46464C494E4531ull;
constexpr size_t Size = 0x28;
inline void Encode(void* destination, uint32_t index) {
    auto* p = static_cast<uint8_t*>(destination);
    std::memset(p, 0, Size);
    const uint16_t header = Header;
    const uint64_t id = Marker | index;
    const uint64_t ns = Namespace;
    std::memcpy(p, &header, 2);
    p[8] = Kind;
    std::memcpy(p + 0x10, &id, 8);
    std::memcpy(p + 0x18, &ns, 8);
    p[0x20] = Payload;
}
inline int Index(const void* source, uint32_t count) {
    const auto* p = static_cast<const uint8_t*>(source);
    uint16_t header; uint64_t id, ns;
    std::memcpy(&header, p, 2);
    std::memcpy(&id, p + 0x10, 8);
    std::memcpy(&ns, p + 0x18, 8);
    const uint32_t index = static_cast<uint32_t>(id);
    return header == Header && p[8] == Kind && p[0x20] == Payload &&
        (id & 0xFFFFFFFF00000000ull) == Marker && ns == Namespace && index < count
        ? static_cast<int>(index) : -1;
}
}
