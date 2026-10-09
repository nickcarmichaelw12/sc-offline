#include <intrin.h>
#include <utility>
#include "services.h"
#include "hooks.h"
#include "hangar_future.h"
#include "teleport.h"

constexpr int kFakeServices = 32;
constexpr int kFakeSlots = 96;
struct FakeService { const uintptr_t* vtable; int id; };
static uintptr_t   g_fakeVtables[kFakeServices][kFakeSlots];
static FakeService g_fakeServices[kFakeServices];
static char        g_fakeNames[kFakeServices][32] = { "hub" };
static int         g_fakeChild[kFakeServices][kFakeSlots];
static volatile LONG g_fakeSeen[kFakeServices][kFakeSlots];
static LONG        g_fakeCount = 1;
static SRWLOCK     g_fakeLock = SRWLOCK_INIT;
static uintptr_t*  g_service = nullptr;
static uintptr_t   g_serviceVtable[64];

static const struct { const char* service; int slot; } kObjectSlots[] = {
    { "hub", 3 },
    { "hub", 10 },
    { "hub", 14 },
    { "hub", 23 },
    { "hub", 26 },
    { "hub", 29 },
    { "hub", 31 },
    { "hub", 37 },
};

static bool UncheckedOnlySlot(int s, int n) { return s == 0 && n == 14; }

static bool ReadsThroughResult(const void* caller) {
    const uint8_t* p = static_cast<const uint8_t*>(caller);
    __try {
        for (int i = 0; i < 24; ++i) {
            if (p[i] == 0x48 && p[i + 1] == 0x85 && p[i + 2] == 0xC0) return false;
            if ((p[i] == 0x48 || p[i] == 0x4C) && p[i + 1] == 0x8B && (p[i + 2] & 0xC7) == 0x00) return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

enum class ValueKind { Future, Handle, EmptyList };
static const struct { const char* service; int slot; ValueKind kind; } kValueSlots[] = {
    { "hub.29", 1, ValueKind::Future },
    { "hub.29", 2, ValueKind::Future },
    { "hub.29", 3, ValueKind::Future },
    { "hub.29", 4, ValueKind::Future },
    { "hub.29", 5, ValueKind::Future },
    { "hub.29", 9, ValueKind::Future },
    { "hub.29", 11, ValueKind::Handle },
    { "hub.10", 2, ValueKind::EmptyList },
};

struct DeadControl {
    uint8_t bytes[256] = {};
    DeadControl() { bytes[4] = bytes[5] = bytes[6] = bytes[7] = 0xFF; bytes[41] = 1; }
};
alignas(16) static DeadControl g_deadControl;
alignas(16) static uint8_t g_deadState[1024];

static const char* AnswerByValue(int s, int n, uintptr_t out) {
    const ValueKind* kind = nullptr;
    for (const auto& v : kValueSlots) if (v.slot == n && !strcmp(v.service, g_fakeNames[s])) kind = &v.kind;
    if (!kind) return nullptr;
    ULONG_PTR low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    if (out < low || out + 32 > high) return nullptr;
    if (*kind == ValueKind::Handle) {
        *reinterpret_cast<uint32_t*>(out) = 0xFFFFFFFF;
        return "no handle";
    }
    uintptr_t* f = reinterpret_cast<uintptr_t*>(out);
    if (*kind == ValueKind::EmptyList) {
        if (hangarfuture::Make(*reinterpret_cast<hangarfuture::Future*>(out)))
            return "an owned ready future: no persisted hangars in this offline session";
    }
    f[0] = reinterpret_cast<uintptr_t>(g_deadControl.bytes);
    f[1] = 0;
    f[2] = reinterpret_cast<uintptr_t>(g_deadState);
    f[3] = 0;
    return "an abandoned future";
}

static uintptr_t FakeCall(int s, int n, const void* caller, uintptr_t out = 0) {
    if (const char* answer = AnswerByValue(s, n, out)) {
        if (!InterlockedExchange(&g_fakeSeen[s][n], 1)) {
            const uintptr_t at = reinterpret_cast<uintptr_t>(caller) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + 0x140000000;
            Log("[hub] %s slot %d (+%Xh) asked for by 0x%llX, answered %s", g_fakeNames[s], n, n * 8, static_cast<unsigned long long>(at), answer);
        }
        return out;
    }
    if (UncheckedOnlySlot(s, n) && !ReadsThroughResult(caller)) {
        static const void* logged[16];
        static volatile LONG loggedCount = 0;
        bool known = false;
        for (LONG i = 0; i < loggedCount && i < 16 && !known; ++i) known = logged[i] == caller;
        if (!known && loggedCount < 16) {
            logged[InterlockedIncrement(&loggedCount) - 1] = caller;
            const uintptr_t at = reinterpret_cast<uintptr_t>(caller) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + 0x140000000;
            Log("[hub] %s slot %d (+%Xh) asked for by 0x%llX, which checks it: answered 0", g_fakeNames[s], n, n * 8, static_cast<unsigned long long>(at));
        }
        return 0;
    }
    int child = g_fakeChild[s][n];
    if (!child) {
        bool object = false;
        for (const auto& o : kObjectSlots) object |= o.slot == n && !strcmp(o.service, g_fakeNames[s]);
        if (object) {
            AcquireSRWLockExclusive(&g_fakeLock);
            if (!(child = g_fakeChild[s][n]) && g_fakeCount < kFakeServices) {
                const LONG c = g_fakeCount++;
                sprintf_s(g_fakeNames[c], "%s.%d", g_fakeNames[s], n);
                g_fakeChild[s][n] = child = c + 1;
            }
            ReleaseSRWLockExclusive(&g_fakeLock);
        }
    }
    if (!InterlockedExchange(&g_fakeSeen[s][n], 1)) {
        const uintptr_t at = reinterpret_cast<uintptr_t>(caller) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + 0x140000000;
        Log("[hub] %s slot %d (+%Xh) asked for by 0x%llX, answered %s", g_fakeNames[s], n, n * 8, static_cast<unsigned long long>(at),
            child ? g_fakeNames[child - 1] : "0");
    }
    return child ? reinterpret_cast<uintptr_t>(&g_fakeServices[child - 1]) : 0;
}

template <int S, int N> static uintptr_t __fastcall FakeSlot(uintptr_t, uintptr_t out, uintptr_t, uintptr_t) { return FakeCall(S, N, _ReturnAddress(), out); }

template <int S, int... N> static void FillFakeService(std::integer_sequence<int, N...>) {
    ((g_fakeVtables[S][N] = reinterpret_cast<uintptr_t>(&FakeSlot<S, N>)), ...);
    g_fakeServices[S] = { g_fakeVtables[S], S };
}

template <int... S> static void FillFakeServices(std::integer_sequence<int, S...>) {
    (FillFakeService<S>(std::make_integer_sequence<int, kFakeSlots>{}), ...);
}

uintptr_t StandInHub() { return reinterpret_cast<uintptr_t>(&g_fakeServices[0]); }

uint8_t* FindServicesObject(const Section& text) {
    int n = 0;
    uint8_t* first = FindUniquePattern(text, "40 55 53 41 55 41 57 48 8D AC 24 B8 FE FF FF 48 81 EC 48 02 00 00 45 33 ED 44 38 2D ?? ?? ?? ?? 0F 85 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 18 48 8B 08 48 8B 51 40", n);
    if (!first) return nullptr;
    g_service = reinterpret_cast<uintptr_t*>(first + 0x2D + Rel32(first + 0x29));
    static bool filled = false;
    if (!filled) { filled = true; FillFakeServices(std::make_integer_sequence<int, kFakeServices>{}); }
    return first;
}

bool SwapHubSlot(HubFn hub, HubFn* real) {
    const uintptr_t service = g_service ? *g_service : 0;
    uintptr_t** vt = reinterpret_cast<uintptr_t**>(service);
    if (!service || *vt == g_serviceVtable) return false;
    memcpy(g_serviceVtable, *vt, sizeof(g_serviceVtable));
    if (real) *real = reinterpret_cast<HubFn>(g_serviceVtable[3]);
    g_serviceVtable[3] = reinterpret_cast<uintptr_t>(hub);
    *vt = g_serviceVtable;
    return true;
}

static thread_local int t_inInstanceRequest = 0;
static HubFn g_realHub = nullptr;

static uintptr_t __fastcall InstanceRequestHub(uintptr_t service) {
    const uintptr_t real = g_realHub ? g_realHub(service) : 0;
    return real || !t_inInstanceRequest ? real : StandInHub();
}

using RequestInstanceFn = uintptr_t(__fastcall*)(uintptr_t manager, uint64_t owner, uintptr_t request);
static RequestInstanceFn g_requestInstanceOrig = nullptr;
using RequestListFn = uintptr_t(__fastcall*)(uintptr_t manager, uintptr_t out, uint64_t owner);
static RequestListFn g_requestListOrig = nullptr;

static void EnsureHangarHub() {
    __try {
        if (SwapHubSlot(&InstanceRequestHub, &g_realHub)) Log("[hangar] installed scoped offline hangar lookup adapter");
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("[atc] fault preparing the hangar request"); }
}

static uintptr_t __fastcall RequestInstanceHook(uintptr_t manager, uint64_t owner, uintptr_t request) {
    Log("[hangar] native instance request entered; awaiting native creation result");
    EnsureHangarHub();
    __try {
        const uintptr_t service = g_service ? *g_service : 0;
        const bool offline = service && g_realHub && !g_realHub(service);
        // Online elevator requests only reopen persisted instances. A new local
        // session has none. Use the native manager's creation-capable origin
        // for this player's first elevator request, retaining its owner, size,
        // gateway and promises. Do not restore a potentially erased request.
        if (offline && owner && owner == LocalPlayerId() && request
            && *reinterpret_cast<uint32_t*>(request + 8) == 4
            && *reinterpret_cast<uintptr_t*>(request + 0x50)) {
            *reinterpret_cast<uint32_t*>(request + 8) = 1;
            Log("[hangar] offline elevator request uses native InstanceManagerDebug creation origin; selected gateway retained");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("[hangar] elevator creation eligibility could not be read"); }
    ++t_inInstanceRequest;
    uintptr_t result = 0;
    __try { result = g_requestInstanceOrig(manager, owner, request); }
    __finally { --t_inInstanceRequest; }
    return result;
}

static uintptr_t __fastcall RequestListHook(uintptr_t manager, uintptr_t out, uint64_t owner) {
    EnsureHangarHub();
    ++t_inInstanceRequest;
    uintptr_t result = 0;
    __try { result = g_requestListOrig(manager, out, owner); }
    __finally { --t_inInstanceRequest; }
    Log("[hangar] native destination-list request returned; destination/instance readiness is not yet confirmed");
    return result;
}

static uint8_t* FunctionOf(const uint8_t* site) {
    DWORD64 base = 0;
    PRUNTIME_FUNCTION rf = site ? RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(site), &base, nullptr) : nullptr;
    for (int i = 0; rf && i < 8; ++i) {
        const uint8_t* info = reinterpret_cast<const uint8_t*>(base + rf->UnwindData);
        if (!((info[0] >> 3) & UNW_FLAG_CHAININFO)) break;
        rf = reinterpret_cast<PRUNTIME_FUNCTION>(const_cast<uint8_t*>(info + 4 + ((info[2] + 1) & ~1) * 2));
    }
    return rf ? reinterpret_cast<uint8_t*>(base + rf->BeginAddress) : nullptr;
}

static uint8_t* FindLeaAny(const Section& text, const uint8_t* target) {
    static const uint8_t kRex[] = { 0x48, 0x4C };
    static const uint8_t kModRm[] = { 0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D };
    for (uint8_t r : kRex)
        for (uint8_t m : kModRm)
            if (uint8_t* p = target ? FindRipLea(text, r, 0x8D, m, target) : nullptr) return p;
    return nullptr;
}

void ResolveHangarsApi(const Section& text, const Section& rdata) {
    uint8_t* fn = FunctionOf(FindLeaAny(text, FindCString(rdata, "IIM_RequestInstanceImpl_Requesting")));
    int count = 0;
    uint8_t* list = FindUniquePattern(text,
        "4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 78 FF FF FF 48 81 EC 88 01 00 00", count);
    // Both paths must bind the same typed future; reject changed ABI layouts.
    uint8_t* bind = fn && BytesMatch(fn + 0x414, "E8") ? fn + 0x419 + Rel32(fn + 0x415) : nullptr;
    uint8_t* ctor = fn && BytesMatch(fn + 0x404, "E8") ? fn + 0x409 + Rel32(fn + 0x405) : nullptr;
    uint8_t* thunk = ctor && BytesMatch(ctor + 0xAB, "48 8D 05") ? ctor + 0xB2 + Rel32(ctor + 0xAE) : nullptr;
    uint8_t* continuation = thunk && BytesMatch(thunk + 0x1C, "E8") ? thunk + 0x21 + Rel32(thunk + 0x1D) : nullptr;
    if (!FindServicesObject(text) || !fn || !list || !bind || !continuation
        || !BytesMatch(fn, "48 89 5C 24 10 48 89 4C 24 08 55 56 57")
        || !BytesMatch(fn + 0x3C, "41 8B 58 08")
        || !BytesMatch(list + 0x5E8, "FF 50 10") || !BytesMatch(list + 0x66A, "E8")
        || list + 0x66F + Rel32(list + 0x66B) != bind
        || !BytesMatch(bind + 0x89, "44 38 60 29")
        || !BytesMatch(bind + 0x97, "44 38 60 28")
        || !BytesMatch(bind + 0xA3, "48 81 C7 C8 00 00 00")
        || !BytesMatch(bind + 0xB0, "38 47 50")
        || !BytesMatch(continuation + 0x2F93, "41 83 7C 24 08 04 0F 84 ?? ?? ?? ?? 4D 8B 6C 24 50 4D 85 ED")) {
        Log("[!] hangar lookup ABI not recognized; offline hangar adapter disabled");
        return;
    }
    if (!HookFunction(fn, 10, reinterpret_cast<void*>(&RequestInstanceHook), reinterpret_cast<void**>(&g_requestInstanceOrig)))
        Log("[!] hangar instance request hook failed");
    if (!HookFunction(list, 10, reinterpret_cast<void*>(&RequestListHook), reinterpret_cast<void**>(&g_requestListOrig)))
        Log("[!] hangar destination-list hook failed");
}
