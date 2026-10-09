#pragma once
#include <cstddef>
#include <cstdint>
#include <new>

// CL12660092 CFuture<CigResult<vector<SHangar>, CigError>> ready-empty result.
// This describes the local session's absence of persisted hangars, not a
// successful instance/spawn. The native continuation still decides what to do.
// All payloads are empty POD; the native binder consumes the result. Its two
// shared_ptrs own this allocation through the MSVC _Ref_count_base ABI.
#if defined(_MSC_VER)
#define SCO_HANGAR_ABI __fastcall
#else
#define SCO_HANGAR_ABI __attribute__((ms_abi))
#endif
namespace hangarfuture {
struct Owner;
struct VTable {
    void (SCO_HANGAR_ABI *destroy)(Owner*);
    void (SCO_HANGAR_ABI *deleteThis)(Owner*);
};
struct Future { void* control; Owner* controlOwner; void* state; Owner* stateOwner; };
struct Owner {
    const VTable* vtable;
    std::int32_t uses;
    std::int32_t weak;
    alignas(8) unsigned char control[0x30];
    alignas(8) unsigned char state[0x120];
};
static_assert(offsetof(Owner, uses) == 8 && offsetof(Owner, weak) == 12);
static_assert(sizeof(Future) == 32);
inline void SCO_HANGAR_ABI Destroy(Owner*) {} // No strings/vector storage owned.
inline void SCO_HANGAR_ABI Delete(Owner* p) { delete p; }
inline const VTable vtable = { &Destroy, &Delete };
inline bool Make(Future& out) {
    Owner* p = new (std::nothrow) Owner{};
    if (!p) return false;
    p->vtable = &vtable;
    p->uses = 2; // one owner for each shared_ptr in Future
    p->weak = 1;
    const std::int32_t unowned = -1;
    // Lock owner at +4, ready at +0x28, abandoned at +0x29.
    for (unsigned i = 0; i < sizeof(unowned); ++i) p->control[4+i] = 0xff;
    p->control[0x28] = 1;
    p->state[0xc8] = 1;  // CigResult success discriminant; empty vector follows.
    p->state[0x118] = 1; // optional result engaged
    out = { p->control, p, p->state, p };
    return true;
}
} // namespace hangarfuture
#undef SCO_HANGAR_ABI
