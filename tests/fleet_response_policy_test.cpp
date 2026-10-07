#include "../src/fleet_response_policy.h"
#include <cassert>
#include <limits>
using namespace fleetpolicy;
int main() {
    assert(Classify(false, 1, 0, 0) == Response::Unreadable);
    assert(!NeedsFallback(Response::Unreadable));
    assert(Classify(true, 0, 0, 0) == Response::Failed);
    assert(NeedsFallback(Response::Failed));
    assert(Classify(true, 1, 0, 0) == Response::Empty);
    assert(Classify(true, 1, 0x1000, 0x1000) == Response::Empty);
    assert(NeedsFallback(Response::Empty));
    assert(Classify(true, 1, 0x1000, 0x1150) == Response::Populated);
    assert(!NeedsFallback(Response::Populated));
    assert(Classify(true, 1, 0, 0x150) == Response::InvalidRange);
    assert(Classify(true, 1, 0x2000, 0x1000) == Response::InvalidRange);
    assert(Classify(true, 1, 0x1000, 0x1001) == Response::InvalidRange);
    assert(!NeedsFallback(Response::InvalidRange));
    assert(Classify(true, 1, std::numeric_limits<std::uintptr_t>::max(), 0) == Response::InvalidRange);
}
