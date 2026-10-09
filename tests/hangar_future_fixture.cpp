#include "../src/hangar_future.h"
#include <cassert>
#include <cstring>

#define ABI __attribute__((ms_abi))
namespace {
int calls, destroyed, deleted;
void ABI Destroy(hangarfuture::Owner* p) {
    assert(p->uses == 0);
    ++destroyed;
}
void ABI Delete(hangarfuture::Owner* p) {
    assert(p->weak == 0);
    ++deleted;
    hangarfuture::Delete(p);
}
const hangarfuture::VTable observed{ &Destroy, &Delete };
void ABI Callback(void* result) {
    auto* p = static_cast<unsigned char*>(result);
    assert(p[0] == 1);
    std::uintptr_t vector[3];
    std::memcpy(vector, p+8, sizeof(vector));
    assert(!vector[0] && !vector[1] && !vector[2]);
    ++calls;
}
using Bind = void* (ABI *)(hangarfuture::Future*, void*, void*);
using Dispose = void (ABI *)(hangarfuture::Future*);
}

extern "C" void exercise(void* binder, void* disposer) {
    auto bind = reinterpret_cast<Bind>(binder);
    auto dispose = reinterpret_cast<Dispose>(disposer);
    for (int iteration = 0; iteration < 1000; ++iteration) {
        hangarfuture::Future f{};
        assert(hangarfuture::Make(f));
        f.controlOwner->vtable = &observed;
        const int before = calls;
        // Native move-only-function's plain function-pointer representation.
        std::uintptr_t callback[3] = { reinterpret_cast<std::uintptr_t>(&Callback), 1, 0 };
        std::uintptr_t handle[2] = { 0xdead, 0xbeef };
        assert(bind(&f, handle, callback) == handle);
        assert(calls == before+1);
        assert(!handle[0] && !handle[1]);
        assert(!f.control && !f.controlOwner && !f.state && !f.stateOwner);
        dispose(&f); // moved-from destructor must not destroy twice
        assert(destroyed == iteration*2+1 && deleted == destroyed);
        // A ready result may be discarded before attaching a continuation.
        assert(hangarfuture::Make(f));
        f.controlOwner->vtable = &observed;
        dispose(&f);
        assert(destroyed == iteration*2+2 && deleted == destroyed);
        assert(calls == before+1);
    }
}
