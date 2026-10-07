#pragma once
#include <cstdint>

namespace fleetpolicy {
enum class Response { Unreadable, Failed, Empty, Populated, InvalidRange };
inline Response Classify(bool readable, unsigned char status, std::uintptr_t begin, std::uintptr_t end) {
    if (!readable) return Response::Unreadable;
    if (status != 1) return Response::Failed;
    if (begin == end) return Response::Empty;
    if (!begin || end < begin || (end - begin) % 0x150 != 0) return Response::InvalidRange;
    return Response::Populated;
}
inline bool NeedsFallback(Response response) {
    return response == Response::Failed || response == Response::Empty;
}
}
