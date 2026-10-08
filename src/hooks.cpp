#include "hooks.h"
#include "bridge.h"
#include "offline_urn.h"
#include "fleet_response_policy.h"
#include "teleport.h"
#include <initializer_list>
#include <nmmintrin.h>
#include <share.h>

static uint8_t* g_cave    = nullptr;
static uint8_t* g_caveEnd = nullptr;
bool            g_hooksInstalled = false;

static bool AllocCaveNear(const uint8_t* anchor) {
    if (g_cave) return true;
    SYSTEM_INFO si; GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity;
    const uintptr_t a = reinterpret_cast<uintptr_t>(anchor) & ~(gran - 1);
    for (uintptr_t d = gran; d < 0x70000000; d += gran) {
        for (uintptr_t cand : { a - d, a + d }) {
            void* p = VirtualAlloc(reinterpret_cast<void*>(cand), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (p) { g_cave = static_cast<uint8_t*>(p); g_caveEnd = g_cave + 0x1000; return true; }
        }
    }
    return false;
}

static bool InstallDetour(uint8_t* target, size_t stolen, void* detour, void** original, DWORD& err) {
    const size_t need = 14 + stolen + 14;
    if (!g_cave || g_cave + need > g_caveEnd || stolen < 5 || stolen > 16) return false;
    uint8_t* relay = g_cave;
    relay[0] = 0xFF; relay[1] = 0x25; memset(relay + 2, 0, 4); memcpy(relay + 6, &detour, 8);
    uint8_t* tramp = relay + 14;
    memcpy(tramp, target, stolen);
    uint8_t* back = target + stolen;
    tramp[stolen] = 0xFF; tramp[stolen + 1] = 0x25; memset(tramp + stolen + 2, 0, 4); memcpy(tramp + stolen + 6, &back, 8);
    g_cave = tramp + stolen + 14;
    g_cave += (16 - reinterpret_cast<uintptr_t>(g_cave) % 16) % 16;

    const int64_t rel = relay - (target + 5);
    if (rel < INT32_MIN || rel > INT32_MAX) return false;
    uint8_t patch[16];
    patch[0] = 0xE9;
    const int32_t rel32 = static_cast<int32_t>(rel);
    memcpy(patch + 1, &rel32, 4);
    memset(patch + 5, 0x90, stolen - 5);
    *original = tramp;
    return WriteCode(target, patch, stolen, err);
}

using ValidateFilterFn     = uint64_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using ValidateProjectionFn = uint64_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static ValidateFilterFn     g_origValidateFilter     = nullptr;
static ValidateProjectionFn g_origValidateProjection = nullptr;

static const char* ArenaString(uintptr_t tagged) {
    const uintptr_t s = tagged & ~uintptr_t(3);
    if (!s) return "";
    return *reinterpret_cast<const size_t*>(s + 24) > 15 ? *reinterpret_cast<const char* const*>(s)
                                                          : reinterpret_cast<const char*>(s);
}

static void DescribeFilter(uintptr_t filter, char* out, size_t n) {
    __try {
        const int type = *reinterpret_cast<const int*>(filter + 28);
        const uintptr_t payload = *reinterpret_cast<const uintptr_t*>(filter + 16);
        if (type == 3 && payload) {
            const int count = Rd<int>(payload + 24);
            const uintptr_t rep = Rd<uintptr_t>(payload + 32);
            char kinds[64] = "";
            size_t k = 0;
            for (int i = 0; i < count && i < 8 && rep; ++i)
                k += snprintf(kinds + k, sizeof(kinds) - k, "%s%d", i ? "," : "", Rd<int>(Rd<uintptr_t>(rep + 8 + 8 * i) + 28));
            snprintf(out, n, "type=3 property='%.80s' op=%d values=%d kinds=[%s]",
                     ArenaString(Rd<uintptr_t>(payload + 40)), Rd<int>(payload + 48), count, kinds);
        }
        else if (type == 5 && payload)
            snprintf(out, n, "type=5 property='%.80s' bit=%d", ArenaString(*reinterpret_cast<const uintptr_t*>(payload + 16)),
                     *reinterpret_cast<const int*>(payload + 24));
        else if (type == 2 && payload)
            snprintf(out, n, "type=2 value='%.80s' kind=%d", ArenaString(*reinterpret_cast<const uintptr_t*>(payload + 16)),
                     *reinterpret_cast<const int*>(payload + 36));
        else
            snprintf(out, n, "type=%d payload=0x%llx", type, static_cast<unsigned long long>(payload));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        snprintf(out, n, "<unreadable filter 0x%llx>", static_cast<unsigned long long>(filter));
    }
}

static void DescribeDescriptor(uintptr_t d, char* out, size_t n) {
    size_t k = 0;
    out[0] = '\0';
    for (uintptr_t off = 0; off < 0xE0 && k + 40 < n; off += 8) {
        __try {
            const uintptr_t p = Rd<uintptr_t>(d + off);
            const char* s = nullptr;
            if (p > 0x10000 && p < 0x7FFFFFFFFFFF) {
                const char* c = reinterpret_cast<const char*>(p);
                int len = 0;
                while (len < 64 && c[len] >= 0x20 && c[len] < 0x7F) ++len;
                if (len >= 3 && c[len] == '\0') s = c;
            }
            if (s) k += snprintf(out + k, n - k, " +%llx='%.48s'", static_cast<unsigned long long>(off), s);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

static void DumpPropertyRegistry(uintptr_t self) {
    static volatile LONG done = 0;
    if (InterlockedExchange(&done, 1)) return;
    __try {
        for (int t = 0; t < 8; ++t)
            Log("[inv] registry: property type %d allowed-op mask 0x%08x", t, Rd<uint32_t>(self + 4 * (638 + 3 * t)));
        const uintptr_t head = Rd<uintptr_t>(self + 0x440);
        int count = 0;
        for (uintptr_t node = Rd<uintptr_t>(head); node && node != head && count < 2000; node = Rd<uintptr_t>(node), ++count) {
            const uintptr_t d = Rd<uintptr_t>(node + 24);
            char names[512];
            DescribeDescriptor(d, names, sizeof(names));
            Log("[inv] registry: key=0x%08x type=%d desc=0x%llx%s", Rd<uint32_t>(node + 16), d ? Rd<int>(d + 20) : -1,
                static_cast<unsigned long long>(d), names);
        }
        Log("[inv] registry: %d properties", count);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[inv] registry: dump faulted");
    }
}

static uint32_t Crc32c(const char* s) {
    uint32_t c = 0xFFFFFFFFu;
    for (; *s; ++s) c = _mm_crc32_u8(c, static_cast<uint8_t>(*s));
    return ~c;
}

static bool PropertyRegistered(uintptr_t self, uint32_t key) {
    const uintptr_t head = Rd<uintptr_t>(self + 0x440);
    int guard = 0;
    for (uintptr_t node = Rd<uintptr_t>(head); node && node != head && guard < 4000; node = Rd<uintptr_t>(node), ++guard)
        if (Rd<uint32_t>(node + 16) == key) return true;
    return false;
}

static const char* UnregisteredFilterProperty(uintptr_t self, uintptr_t filter) {
    __try {
        const int type = Rd<int>(filter + 28);
        const uintptr_t payload = Rd<uintptr_t>(filter + 16);
        if (!payload || (type != 3 && type != 5)) return nullptr;
        const char* name = ArenaString(Rd<uintptr_t>(payload + (type == 3 ? 40 : 16)));
        if (!name[0] || PropertyRegistered(self, Crc32c(name))) return nullptr;
        return name;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static uint64_t __fastcall Hook_ValidateFilter(uintptr_t self, uintptr_t parent, uintptr_t state, uintptr_t filter,
                                               uintptr_t a5, uintptr_t a6, uintptr_t a7, uintptr_t a8) {
    const uint64_t r = g_origValidateFilter(self, parent, state, filter, a5, a6, a7, a8);
    if (r == 1 && parent != 0) {
        if (const char* prop = UnregisteredFilterProperty(self, filter)) {
            static volatile LONG reported = 0;
            if (InterlockedIncrement(&reported) <= 20)
                Log("[inv] dropping filter on property '%.80s' (not indexed offline)", prop);
            return 4;
        }
    }
    if (r == 1 || r == 3) {
        DumpPropertyRegistry(self);
        char desc[256];
        DescribeFilter(filter, desc, sizeof(desc));
        Log("[inv] filter REJECTED (result %llu, parent 0x%llx): %s", static_cast<unsigned long long>(r),
            static_cast<unsigned long long>(parent), desc);
    }
    return r;
}

static uint64_t __fastcall Hook_ValidateProjection(uintptr_t self, uintptr_t state, uintptr_t projection, uintptr_t a4) {
    const uint64_t r = g_origValidateProjection(self, state, projection, a4);
    if (r != 0) Log("[inv] projection REJECTED (result %llu, projection 0x%llx)", static_cast<unsigned long long>(r),
                    static_cast<unsigned long long>(projection));
    return r;
}

using InstanceGroupQueryFn = void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t*);
static InstanceGroupQueryFn g_origInstanceGroupQuery = nullptr;
static uintptr_t*           g_servicesManager = nullptr;

static bool PrepareInstanceGroupQuery(uint8_t* target) {
    static const uint8_t loadMgr[] = { 0x48, 0x8B, 0x0D };
    static const uint8_t callHub[] = { 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x18 };
    if (memcmp(target + 0x5B, loadMgr, sizeof(loadMgr)) != 0 || memcmp(target + 0x99, callHub, sizeof(callHub)) != 0)
        return false;
    g_servicesManager = reinterpret_cast<uintptr_t*>(target + 0x5B + 7 + Rel32(target + 0x5E));
    return true;
}

static void __fastcall Hook_InstanceGroupQuery(uintptr_t self, uintptr_t key, uintptr_t* out) {
    uintptr_t hub = 0;
    __try {
        const uintptr_t mgr = *g_servicesManager;
        if (mgr) hub = reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t)>(Rd<uintptr_t>(Rd<uintptr_t>(mgr) + 0x18))(mgr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        hub = 0;
    }
    if (hub) { g_origInstanceGroupQuery(self, key, out); return; }
    out[1] = out[0];
    static volatile LONG reported = 0;
    if (InterlockedIncrement(&reported) <= 5)
        Log("[fix] hangar instance-group lookup skipped: server services hub unavailable offline (prevented crash)");
}

using EntitlementsResultFn = void(__fastcall*)(uintptr_t, uintptr_t);
using RetrieveVehicleFn    = void(__fastcall*)(uintptr_t, uintptr_t);
static EntitlementsResultFn g_origEntitlementsResult = nullptr;
static RetrieveVehicleFn    g_origRetrieveVehicle = nullptr;
static RetrieveVehicleFn    g_origDeliverVehicle = nullptr;
static void __fastcall Hook_EntitlementsResult(uintptr_t self, uintptr_t result);
static void __fastcall Hook_RetrieveVehicle(uintptr_t asop, uintptr_t slot);
static void __fastcall Hook_DeliverVehicle(uintptr_t asop, uintptr_t event);
static bool PrepareEntitlementsResult(uint8_t* target);
static bool PrepareRetrieveVehicle(uint8_t* target);
static bool PrepareDeliverVehicle(uint8_t* target);

// Verified against client 4.10.193.11644 (CL 12660092). Observe only:
// never retain engine objects or fabricate completion of an outstanding future.
using FleetStageFn = uintptr_t(__fastcall*)(uintptr_t, uintptr_t);
static thread_local LONG t_terminalTrace = 0;
using ChannelLookupFn = unsigned(__fastcall*)(uintptr_t, uintptr_t);
static ChannelLookupFn g_origChannelLookup = nullptr;
static FleetStageFn g_origPlayerResolve = nullptr;
static FleetStageFn g_origATCResolve = nullptr;
static unsigned __fastcall Hook_ChannelLookup(uintptr_t player, uintptr_t arg) {
    const unsigned result = g_origChannelLookup(player, arg);
    if (t_terminalTrace) Log("[terminal-detail] open #%ld player_channel=%u", t_terminalTrace, result);
    return result;
}
static void TraceResolvedHandle(const char* label, uintptr_t out) {
    if (!t_terminalTrace) return;
    bool readable = false, nonzero = false;
    __try { if (out) { nonzero = Rd<uint64_t>(out) != 0; readable = true; } }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    Log("[terminal-detail] open #%ld %s: readable=%d handle_nonzero=%d (not a validity check)",
        t_terminalTrace, label, readable ? 1 : 0, nonzero ? 1 : 0);
}
static uintptr_t __fastcall Hook_PlayerResolve(uintptr_t out, uintptr_t id) {
    const uintptr_t result = g_origPlayerResolve(out, id);
    TraceResolvedHandle("player resolution", out);
    return result;
}
static uintptr_t __fastcall Hook_ATCResolve(uintptr_t out, uintptr_t id) {
    const uintptr_t result = g_origATCResolve(out, id);
    TraceResolvedHandle("ATC resolution", out);
    return result;
}
// Observe the exact virtual call used by OnRequestOpen, without making an
// extra authority query or changing its result. RCX/RDX/R8/R9 are forwarded.
static const uint8_t* g_terminalBranchFlag = nullptr;
static PatchStatus g_terminalBranchStatus;
static uint8_t __fastcall Hook_TerminalBranch(uintptr_t entity, uintptr_t table,
                                             uintptr_t arg3, uintptr_t arg4) {
    using Fn = uint8_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    const auto original = reinterpret_cast<Fn>(Rd<uintptr_t>(table + 0x7A8));
    const uint8_t result = original(entity, table, arg3, arg4);
    if (t_terminalTrace) {
        int flag = -1;
        __try { if (g_terminalBranchFlag) flag = *g_terminalBranchFlag; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        Log("[terminal-branch] open #%ld virtual_7a8=%u client_path_flag=%d (observation only)",
            t_terminalTrace, static_cast<unsigned>(result), flag);
    }
    return result;
}

static void InstallTerminalBranchTrace(const Section& text) {
    PatchStatus& st = g_terminalBranchStatus;
    st.expected = 1;
    // Includes the saved channel, original six-byte indirect CALL, and both
    // result/channel tests. No RIP-relative instruction is moved to the relay.
    uint8_t* anchor = FindUniquePattern(text,
        "48 8B 4E 08 49 23 CC 89 85 D0 00 00 00 48 8B 11 FF 92 A8 07 00 00 84 C0 0F 84 E7 02 00 00 8B BD D0 00 00 00 85 FF 0F 85 94 02 00 00", st.sites);
    if (!anchor) { st.result = st.sites ? PatchResult::WrongMatchCount : PatchResult::NotFound; return; }
    uint8_t* flagTest = anchor + 0x305;
    if (flagTest + 13 > text.base + text.size ||
        !BytesMatch(flagTest, "80 3D AB 2C 21 05 00 0F 84 50 04 00 00")) {
        st.result = PatchResult::NotFound; return;
    }
    uint8_t* call = anchor + 16;
    if (!g_cave || g_cave + 16 > g_caveEnd) { st.result = PatchResult::ProtectFailed; return; }
    uint8_t* relay = g_cave;
    const int64_t distance = reinterpret_cast<int64_t>(relay) - reinterpret_cast<int64_t>(call + 5);
    if (distance < INT32_MIN || distance > INT32_MAX) { st.result = PatchResult::ProtectFailed; return; }
    const auto detour = &Hook_TerminalBranch;
    relay[0] = 0xFF; relay[1] = 0x25; memset(relay + 2, 0, 4);
    memcpy(relay + 6, &detour, 8);
    FlushInstructionCache(GetCurrentProcess(), relay, 14);
    g_cave += 16;
    const int32_t rel32 = static_cast<int32_t>(distance);
    uint8_t patch[6] = { 0xE8, 0, 0, 0, 0, 0x90 };
    memcpy(patch + 1, &rel32, 4);
    g_terminalBranchFlag = flagTest + 7 + Rel32(flagTest + 2);
    if (!WriteCode(call, patch, sizeof(patch), st.err)) { st.result = PatchResult::ProtectFailed; return; }
    st.result = PatchResult::Applied;
    st.at = call;
    g_hooksInstalled = true;
}

static FleetStageFn g_origTerminalOpen = nullptr;
static PatchStatus g_terminalContinuationStatus;
static void InstallTerminalContinuation(const Section& text) {
    PatchStatus& st = g_terminalContinuationStatus;
    st.expected = 1;
    // Experimental local-host continuation, verified against CL12660092.
    // Keep the authority-side handle/channel setup, then run the existing
    // client-flag and local-player checks. Do not falsify the virtual query:
    // forcing its result would skip the channel write needed by the client.
    if (!g_origTerminalOpen || g_terminalBranchStatus.result != PatchResult::Applied) {
        st.result = PatchResult::NotFound; return;
    }
    uint8_t* site = FindUniquePattern(text,
        "89 BE E8 09 00 00 E9 5D 04 00 00 80 3D AB 2C 21 05 00 0F 84 50 04 00 00 48 8D 8E 78 02 00 00", st.sites);
    if (!site) { st.result = st.sites ? PatchResult::WrongMatchCount : PatchResult::NotFound; return; }
    // Cross-check this is the same flag operand validated by the branch trace.
    if (site + 18 + Rel32(site + 13) != g_terminalBranchFlag) {
        st.result = PatchResult::NotFound; return;
    }
    // The authority branch overwrites EDI with the channel. The client branch
    // still needs the ATC handle originally loaded into RDI from [RBP+0xD8].
    // asop1 fell through with the channel as a pointer and crashed. Restore the
    // original handle before entering the client branch, without replaying calls.
    if (site < text.base + 0x342 ||
        !BytesMatch(site - 0x342, "48 8B BD D8 00 00 00") ||
        !BytesMatch(site - 0x2DC, "8B BD D0 00 00 00 85 FF 0F 85 94 02 00 00")) {
        st.result = PatchResult::NotFound; return;
    }
    if (!g_cave || g_cave + 16 > g_caveEnd) { st.result = PatchResult::ProtectFailed; return; }
    uint8_t* relay = g_cave;
    const int64_t toRelay = reinterpret_cast<int64_t>(relay) - reinterpret_cast<int64_t>(site + 11);
    const int64_t toClient = reinterpret_cast<int64_t>(site + 11) - reinterpret_cast<int64_t>(relay + 12);
    if (toRelay < INT32_MIN || toRelay > INT32_MAX || toClient < INT32_MIN || toClient > INT32_MAX) {
        st.result = PatchResult::ProtectFailed; return;
    }
    uint8_t code[12] = { 0x48, 0x8B, 0xBD, 0xD8, 0, 0, 0, 0xE9, 0, 0, 0, 0 };
    const int32_t back = static_cast<int32_t>(toClient);
    memcpy(code + 8, &back, sizeof(back));
    memcpy(relay, code, sizeof(code));
    FlushInstructionCache(GetCurrentProcess(), relay, sizeof(code));
    g_cave += 16;
    uint8_t jump[5] = { 0xE9, 0, 0, 0, 0 };
    const int32_t forward = static_cast<int32_t>(toRelay);
    memcpy(jump + 1, &forward, sizeof(forward));
    if (!WriteCode(site + 6, jump, sizeof(jump), st.err)) {
        st.result = PatchResult::ProtectFailed; return;
    }
    st.result = PatchResult::Applied;
    st.at = site + 6;
}
static volatile LONG g_terminalOpens = 0;
static void TraceTerminalState(uintptr_t self, uintptr_t request, LONG n, const char* phase) {
    bool readable = false;
    bool requestHandle = false;
    bool atcLink = false;
    __try {
        if (self && request) {
            requestHandle = Rd<uint64_t>(request) != 0;
            atcLink = Rd<uint64_t>(self + 0x9F8) != 0;
            readable = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    Log("[terminal-trace] open #%ld %s: readable=%d request_handle_nonzero=%d atc_link_nonzero=%d (nonzero is not validity)",
        n, phase, readable ? 1 : 0, requestHandle ? 1 : 0, atcLink ? 1 : 0);
}
static uintptr_t __fastcall Hook_TerminalOpen(uintptr_t self, uintptr_t request) {
    Bridge_Refresh(); // HTTP runs on the bridge worker, never the engine thread.
    const LONG n = InterlockedIncrement(&g_terminalOpens);
    if (n <= 20) TraceTerminalState(self, request, n, "entry");
    const LONG previousTrace = t_terminalTrace;
    uintptr_t result = 0;
    t_terminalTrace = n <= 20 ? n : 0;
    __try { result = g_origTerminalOpen(self, request); }
    __finally { t_terminalTrace = previousTrace; }
    // Do not dereference engine objects after the call: it may invalidate them.
    if (n <= 20) Log("[terminal-trace] open #%ld returned (not async completion)", n);
    return result;
}
static bool PrepareTerminalOpen(uint8_t* target) {
    const uint8_t* msg = FindCString(g_rdata, "[NO ATC] $$ [$$] - NO LINK TO ATC - Verify and export OC: $$");
    return msg && BytesMatch(target + 0x1A9, "4C 8D 0D")
        && target + 0x1B0 + Rel32(target + 0x1AC) == msg;
}
static FleetStageFn g_origFleetFetch = nullptr;
static FleetStageFn g_origFleetBuild = nullptr;
static volatile LONG g_fleetFetches = 0;
static volatile LONG g_fleetBuilds = 0;
static uintptr_t __fastcall Hook_FleetFetch(uintptr_t self, uintptr_t request) {
    const LONG n = InterlockedIncrement(&g_fleetFetches);
    int type = -1;
    __try { if (request) type = Rd<int>(request + 0x10); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (n <= 20) Log("[fleet-trace] fetch entry #%ld: type=%d (unreadable=-1)", n, type);
    const uintptr_t result = g_origFleetFetch(self, request);
    if (n <= 20) Log("[fleet-trace] fetch returned #%ld (not async completion)", n);
    return result;
}
static uintptr_t __fastcall Hook_FleetBuild(uintptr_t self, uintptr_t response) {
    const LONG n = InterlockedIncrement(&g_fleetBuilds);
    if (n <= 20) Log("[fleet-trace] list builder entry #%ld", n);
    const uintptr_t result = g_origFleetBuild(self, response);
    if (n <= 20) Log("[fleet-trace] list builder returned #%ld", n);
    return result;
}
static bool PrepareFleetFetch(uint8_t* target) {
    const uint8_t* msg = FindCString(g_rdata, "Fetching player vehicle list");
    return msg && BytesMatch(target + 0x2D, "4C 8D 0D")
        && target + 0x34 + Rel32(target + 0x30) == msg;
}
static bool PrepareFleetBuild(uint8_t* target) {
    const uint8_t* msg = FindCString(g_rdata, "Building ASOP Vehicle List");
    return msg && BytesMatch(target + 0x123E, "4C 8D 05")
        && target + 0x1245 + Rel32(target + 0x1241) == msg;
}

struct HookSpec {
    const char*   name;
    const char*   pattern;
    size_t      stolen;
    void*       detour;
    void**      original;
    bool      (*prepare)(uint8_t* target);
};

// Exact client-build helper signatures; RIP-relative bytes are matched, not relocated.
static const HookSpec kHooks[] = {
    { "terminal player resolution trace",
      "40 53 57 48 83 EC 28 48 8B D9 48 8B 0D F7 8D 9C 09 48 8B 01 FF 90 20 01 00 00 48 8B F8 48 85 C0 74 70 48 8B 10 B8 FF FF 00 00 0F B7 0D C7 9C 6F",
      7, reinterpret_cast<void*>(&Hook_PlayerResolve), reinterpret_cast<void**>(&g_origPlayerResolve), nullptr },
    { "terminal ATC resolution trace",
      "40 53 57 48 83 EC 28 48 8B D9 48 8B 0D E7 52 0E 07 48 8B 01 FF 90 20 01 00 00 48 8B F8 48 85 C0 74 70 48 8B 10 B8 FF FF 00 00 0F B7 0D 6F 52 EE",
      7, reinterpret_cast<void*>(&Hook_ATCResolve), reinterpret_cast<void**>(&g_origATCResolve), nullptr },
    { "terminal player channel trace",
      "48 89 5C 24 08 55 56 57 48 83 EC 30 48 8B E9 48 8B 0D 82 37 D1 04 48 8B 01 FF 90 20 01 00 00 48 8B D8 48 85",
      5, reinterpret_cast<void*>(&Hook_ChannelLookup), reinterpret_cast<void**>(&g_origChannelLookup), nullptr },
    { "ship terminal open trace",
      "48 89 54 24 10 55 53 56 57 41 54 41 55 41 57 48 8D 6C 24 80 48 81 EC 80 01 00 00 45 33 ED 48 8B F1",
      5, reinterpret_cast<void*>(&Hook_TerminalOpen), reinterpret_cast<void**>(&g_origTerminalOpen), &PrepareTerminalOpen },
    { "fleet fetch request trace",
      "48 89 4C 24 08 55 53 57 41 54 41 56 48 8D AC 24 00 FD FF FF 48 81 EC 00 04 00 00",
      5, reinterpret_cast<void*>(&Hook_FleetFetch), reinterpret_cast<void**>(&g_origFleetFetch), &PrepareFleetFetch },
    { "fleet list builder trace",
      "48 8B C4 48 89 50 10 48 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 28 F7 FF FF 48 81 EC 98 09 00 00 33 DB",
      7, reinterpret_cast<void*>(&Hook_FleetBuild), reinterpret_cast<void**>(&g_origFleetBuild), &PrepareFleetBuild },
    { "inventory filter validator",
      "48 89 5C 24 18 48 89 74 24 20 57 48 83 EC 20 49 89 50 28 49 8B F8 41 8B 41 1C 48 8B DA",
      5, reinterpret_cast<void*>(&Hook_ValidateFilter), reinterpret_cast<void**>(&g_origValidateFilter), nullptr },
    { "inventory projection validator",
      "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 48 8D 05 ?? ?? ?? ?? 49 8B F0",
      5, reinterpret_cast<void*>(&Hook_ValidateProjection), reinterpret_cast<void**>(&g_origValidateProjection), nullptr },
    { "hangar elevator crash guard",
      "48 89 5C 24 08 48 89 74 24 18 48 89 7C 24 20 55 48 8D AC 24 70 FE FF FF 48 81 EC 90 02 00 00 49 8B 00 49 8B F8",
      5, reinterpret_cast<void*>(&Hook_InstanceGroupQuery), reinterpret_cast<void**>(&g_origInstanceGroupQuery),
      &PrepareInstanceGroupQuery },
    { "fleet manager ship list (offline)",
      "40 55 56 41 56 48 8D AC 24 00 FF FF FF 48 81 EC 00 02 00 00 4C 8B F1 48 8B F2 48 83 C1 08 E8",
      5, reinterpret_cast<void*>(&Hook_EntitlementsResult), reinterpret_cast<void**>(&g_origEntitlementsResult),
      &PrepareEntitlementsResult },
    { "fleet manager retrieve -> spaceport ATC",
      "48 89 54 24 10 55 53 41 55 41 57 48 8D AC 24 A8 FE FF FF 48 81 EC 68 02 00 00 4C 8B FA 4C 8B E9 48 8B 51 08 48 8D 8D 80 01 00 00 E8",
      5, reinterpret_cast<void*>(&Hook_RetrieveVehicle), reinterpret_cast<void**>(&g_origRetrieveVehicle),
      &PrepareRetrieveVehicle },
    { "fleet manager deliver -> reserved spaceport ATC",
      "48 8B C4 55 48 8D A8 78 FD FF FF 48 81 EC 80 03 00 00 48 89 58 08 48 89 70 F0 48 8B F2 4C 89 60 E0 4C 89 70 D0 4C 8B F1",
      11, reinterpret_cast<void*>(&Hook_DeliverVehicle), reinterpret_cast<void**>(&g_origDeliverVehicle),
      &PrepareDeliverVehicle },
};
static PatchStatus g_hookStatus[sizeof(kHooks) / sizeof(kHooks[0])];

void InstallHooks(const Section& text) {
    if (!AllocCaveNear(text.base)) {
        for (PatchStatus& st : g_hookStatus) st.result = PatchResult::ProtectFailed;
        return;
    }
    for (size_t i = 0; i < sizeof(kHooks) / sizeof(kHooks[0]); ++i) {
        const HookSpec& h = kHooks[i];
        PatchStatus& st = g_hookStatus[i];
        st.expected = 1;
        uint8_t* target = FindUniquePattern(text, h.pattern, st.sites);
        if (!target) { st.result = st.sites ? PatchResult::WrongMatchCount : PatchResult::NotFound; continue; }
        if (h.prepare && !h.prepare(target)) { st.result = PatchResult::NotFound; continue; }
        if (!InstallDetour(target, h.stolen, h.detour, h.original, st.err)) { st.result = PatchResult::ProtectFailed; continue; }
        st.result = PatchResult::Applied;
        st.at = target;
        g_hooksInstalled = true;
    }
    InstallTerminalBranchTrace(text);
    InstallTerminalContinuation(text);
}

void LogHooks() {
    LogPatch("terminal branch result trace", g_terminalBranchStatus);
    LogPatch("ASOP local-host client continuation (experimental)", g_terminalContinuationStatus);
    for (size_t i = 0; i < sizeof(kHooks) / sizeof(kHooks[0]); ++i)
        LogPatch(kHooks[i].name, g_hookStatus[i]);
}

bool HookFunction(uint8_t* target, size_t stolen, void* detour, void** original) {
    DWORD err = 0;
    if (!target || !InstallDetour(target, stolen, detour, original, err)) return false;
    g_hooksInstalled = true;
    return true;
}

uint8_t* NearData(size_t n) {
    if (!g_text.base || !AllocCaveNear(g_text.base) || g_cave + n > g_caveEnd) return nullptr;
    uint8_t* p = g_cave;
    g_cave += n;
    g_cave += (16 - reinterpret_cast<uintptr_t>(g_cave) % 16) % 16;
    return p;
}

constexpr size_t   kEntitlementSize = 0x150;
constexpr size_t   kSlotUrn = 0x1698;
constexpr size_t   kAsopAtcId = 0x9F8;

struct FleetShip { char name[201]; uint64_t guid[2]; char serverId[121]; };
// Same bound as the spawn menu (spawner.cpp kMaxMenuShips): ships.txt holds 1102 entries, and the
// old 1024 here dropped the last 78 from the fleet manager without saying so.
constexpr int kMaxFleetShips = 2048;

static FleetShip* g_ships = nullptr;
static int        g_shipCount = 0;
static uint8_t*   g_fakeEntitlements = nullptr;
static SRWLOCK    g_shipsLock = SRWLOCK_INIT;
static bool       g_shipsBuilt = false;
static bool       g_fleetAttempted = false;
static DWORD      g_lastFleetAttempt = 0;
static volatile LONG g_fleetCallbacks = 0;
static volatile LONG g_fleetFallbacks = 0;
static volatile LONG g_fleetRetrieves = 0;

using StrCtorFn          = void*(__fastcall*)(void* out, const char* s);
using StrDtorFn          = void(__fastcall*)(void* s);
using GetATCCompFn       = uint64_t*(__fastcall*)(uint64_t* out, uint64_t entityId);
using RequestTakingOffFn = void(__fastcall*)(uintptr_t atc, uint64_t player, void* location, uint64_t vehicle,
                                             bool spawnVehicle, void* archetype, void* padFilter);
static StrCtorFn          g_strCtor = nullptr;

bool MakeCryString(void* out, const char* s) {
    if (!g_strCtor) return false;
    g_strCtor(out, s);
    return true;
}
static StrDtorFn          g_strDtor = nullptr;

void FreeCryString(void* s) {
    if (g_strDtor) g_strDtor(s);
}
static GetATCCompFn       g_getATCComp = nullptr;
static RequestTakingOffFn g_requestTakingOff = nullptr;

static bool PrepareEntitlementsResult(uint8_t* target) {
    const uint8_t* msg = FindCString(g_rdata, "QueryEntitlements failed with error: $$");
    return msg && BytesMatch(target + 0x55, "4C 8D 0D") && target + 0x55 + 7 + Rel32(target + 0x58) == msg;
}

static bool PrepareRetrieveVehicle(uint8_t* target) {
    if (!BytesMatch(target + 0xC7, "49 8B 95 F8 09 00 00") || !BytesMatch(target + 0xDD, "E8")) return false;
    const uint8_t* usage = FindCString(g_rdata,
        "Invalid arguments. Usage: g_ATC_requestTakeOff <atc_name (autocompletable)> [<ship_archetype>] [<pad_name_filter>]");
    const uint8_t* lea = usage ? FindRipLea(g_text, 0x48, 0x8D, 0x15, usage) : nullptr;
    if (!lea) return false;
    const uint8_t* cmd = lea - 0x1CD;
    if (!BytesMatch(cmd, "40 55 53 48 8B EC 48 83 EC 78") || !BytesMatch(cmd + 0x72, "E8")
        || !BytesMatch(cmd + 0xA9, "E8") || !BytesMatch(cmd + 0x190, "E8"))
        return false;
    g_getATCComp       = reinterpret_cast<GetATCCompFn>(target + 0xDD + 5 + Rel32(target + 0xDE));
    g_strCtor          = reinterpret_cast<StrCtorFn>(cmd + 0x72 + 5 + Rel32(cmd + 0x73));
    g_strDtor          = reinterpret_cast<StrDtorFn>(cmd + 0xA9 + 5 + Rel32(cmd + 0xAA));
    g_requestTakingOff = reinterpret_cast<RequestTakingOffFn>(cmd + 0x190 + 5 + Rel32(cmd + 0x191));
    return true;
}

static bool PrepareDeliverVehicle(uint8_t* target) {
    // Cross-check the event registration -> thunk -> handler chain, not just
    // a prologue match. The stolen 11 bytes contain no relative instructions.
    const uint8_t* name = FindCString(g_rdata, "SCEvtControl_ShipSelectorDeliverVehicle");
    const uint8_t* lea = name ? FindRipLea(g_text, 0x48, 0x8D, 0x05, name) : nullptr;
    if (!lea || !BytesMatch(lea - 0x43, "48 8D 05")) return false;
    const uint8_t* thunk = lea - 0x43 + 7 + Rel32(lea - 0x40);
    return BytesMatch(thunk, "E9") && thunk + 5 + Rel32(thunk + 1) == target
        && BytesMatch(target + 0x101, "48 8B 16 4D 8B BE F0 00 00 00 48 69 CA C0 16 00 00 49 81 C7 98 16 00 00");
}

static uint64_t LocalPlayerId() {
    const uintptr_t mgr = *g_tp.clientMgr;
    const uintptr_t sub = mgr ? Rd<uintptr_t>(mgr + 0xE0) : 0;
    const uintptr_t info = sub ? VCall<uintptr_t>(sub, 0x2E0) : 0;
    return info ? Rd<uint64_t>(info + 8) : 0;
}

static void FindClassGuids(uintptr_t registry, const uintptr_t* classes) {
    const uintptr_t head = Rd<uintptr_t>(registry + 0x48);
    uintptr_t stack[256];
    int sp = 0;
    const uintptr_t root = Rd<uintptr_t>(head + 0x08);
    if (root && !Rd<uint8_t>(root + 0x19)) stack[sp++] = root;
    while (sp) {
        const uintptr_t n = stack[--sp];
        const uintptr_t cls = Rd<uintptr_t>(n + 0x30);
        for (int i = 0; i < g_shipCount; ++i)
            if (classes[i] == cls) { g_ships[i].guid[0] = Rd<uint64_t>(n + 0x20); g_ships[i].guid[1] = Rd<uint64_t>(n + 0x28); }
        for (size_t off : { size_t(0x00), size_t(0x10) }) {
            const uintptr_t c = Rd<uintptr_t>(n + off);
            if (c && !Rd<uint8_t>(c + 0x19) && sp < 256) stack[sp++] = c;
        }
    }
}

static int BuildFleetShips() {
    if (!g_tp.entitySystem || !*g_tp.entitySystem) return 0;
    BridgeShip owned[256];
    const int count = Bridge_OwnedFleet(owned, 256);
    if (count < 0) { Log("[fleet] server fleet not loaded; close ASOP and reopen after bridge connects"); return 0; }
    static FleetShip ships[kMaxFleetShips];
    static uintptr_t classes[kMaxFleetShips];
    g_ships = ships;
    int missing = 0;
    const uintptr_t registry = VCall<uintptr_t>(*g_tp.entitySystem, 0xC0);
    for (int j = 0; j < count; ++j) {
        const char* name = owned[j].cls;
        const uintptr_t cls = VCall<uintptr_t>(registry, 0x20, static_cast<const char*>(name));
        if (!cls) { if (++missing <= 5) Log("[fleet] unknown ship class '%s' (skipped)", name); continue; }
        strncpy_s(ships[g_shipCount].name, name, _TRUNCATE);
        strncpy_s(ships[g_shipCount].serverId, owned[j].id, _TRUNCATE);
        classes[g_shipCount++] = cls;
    }
    FindClassGuids(registry, classes);

    int kept = 0;
    for (int i = 0; i < g_shipCount; ++i) {
        uint64_t guid[2] = { ships[i].guid[0], ships[i].guid[1] };
        if ((guid[0] | guid[1]) && VCall<uintptr_t>(registry, 0x18, guid) == classes[i]) ships[kept++] = ships[i];
        else Log("[fleet] no class GUID for '%s' (skipped)", ships[i].name);
    }
    g_shipCount = kept;

    if (count > 0 && kept == 0) return 0; // Class registry may still be loading.

    g_fakeEntitlements = static_cast<uint8_t*>(calloc(kept ? kept : 1, kEntitlementSize));
    for (int i = 0; g_fakeEntitlements && i < kept; ++i) {
        uint8_t* e = g_fakeEntitlements + i * kEntitlementSize;
        offlineurn::Encode(e, static_cast<uint32_t>(i));
        e[0x50] = 3;
        *reinterpret_cast<uint32_t*>(e + 0x54) = 1;
        memcpy(e + 0x58, ships[i].guid, 16);
    }
    if (!g_fakeEntitlements) g_shipCount = 0;
    Log("[fleet] %d server-owned ships mapped for this session (%d unknown classes); no catalogue fallback", g_shipCount, missing);
    return g_shipCount;
}

static int EnsureFleetShips() {
    AcquireSRWLockExclusive(&g_shipsLock);
    const DWORD now = GetTickCount();
    // An early callback can arrive before entity classes exist. Do not cache failure forever.
    if (!g_shipsBuilt && (!g_fleetAttempted || now - g_lastFleetAttempt >= 3000)) {
        g_fleetAttempted = true;
        g_lastFleetAttempt = now;
        g_shipCount = 0;
        if (g_fakeEntitlements) { free(g_fakeEntitlements); g_fakeEntitlements = nullptr; }
        __try { BuildFleetShips(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            g_shipCount = 0;
            Log("[fleet] fault building offline ship list; eligible to retry on a later response");
        }
        g_shipsBuilt = g_fakeEntitlements != nullptr;
        if (!g_shipsBuilt) Log("[fleet] ship list not ready; next returned query can retry after 3 seconds");
    }
    const int n = g_shipCount;
    ReleaseSRWLockExclusive(&g_shipsLock);
    return n;
}

static void __fastcall Hook_EntitlementsResult(uintptr_t self, uintptr_t result) {
    const LONG callback = InterlockedIncrement(&g_fleetCallbacks);
    bool readable = false;
    uint8_t status = 0;
    uintptr_t begin = 0, end = 0;
    __try {
        if (result) {
            status = Rd<uint8_t>(result);
            // Only success replies have the vector layout already used by this hook's fallback.
            if (status == 1) {
                begin = Rd<uintptr_t>(result + 8);
                end = Rd<uintptr_t>(result + 16);
            }
            readable = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    const auto response = fleetpolicy::Classify(readable, status, begin, end);
    if (callback <= 20) Log("[fleet] entitlement callback #%ld: readable=%d status=%u category=%d (0 unreadable, 1 failed, 2 empty, 3 populated, 4 invalid range)",
        callback, readable ? 1 : 0, static_cast<unsigned>(status), static_cast<int>(response));
    const int n = fleetpolicy::NeedsFallback(response) ? EnsureFleetShips() : 0;
    if (!fleetpolicy::NeedsFallback(response) || !g_shipsBuilt) { g_origEntitlementsResult(self, result); return; }
    struct { uint8_t ok; uint8_t pad[7]; uint8_t* begin; uint8_t* end; uint8_t* cap; uint8_t spare[64]; } fake = {};
    fake.ok = 1;
    fake.begin = g_fakeEntitlements;
    fake.end = fake.cap = g_fakeEntitlements + n * kEntitlementSize;
    const LONG fallback = InterlockedIncrement(&g_fleetFallbacks);
    if (fallback <= 20) Log("[fleet] returned %s list -> supplying %d server-owned ships",
        response == fleetpolicy::Response::Empty ? "empty" : "failed", n);
    g_origEntitlementsResult(self, reinterpret_cast<uintptr_t>(&fake));
}

void ProcessFleetDiagnostics(DWORD now) {
    static DWORD last = 0;
    static bool started = false;
    static unsigned samples = 0;
    if (samples >= 40 || (started && now - last < 15000)) return;
    started = true;
    last = now;
    ++samples;
    Log("[fleet-diag] sample %u: entitlement_callbacks=%ld fallback_replies=%ld retrieve_callbacks=%ld fetch_entries=%ld list_builds=%ld terminal_opens=%ld",
        samples, InterlockedCompareExchange(&g_fleetCallbacks, 0, 0),
        InterlockedCompareExchange(&g_fleetFallbacks, 0, 0), InterlockedCompareExchange(&g_fleetRetrieves, 0, 0),
        InterlockedCompareExchange(&g_fleetFetches, 0, 0), InterlockedCompareExchange(&g_fleetBuilds, 0, 0), InterlockedCompareExchange(&g_terminalOpens, 0, 0));
    if (samples == 1) Log("[fleet-diag] trace observes fetch entry, response, and list builder separately; fetch return is not async completion");
}

static int SlotShipIndex(uintptr_t slot) {
    __try {
        const uintptr_t urn = slot + kSlotUrn;
        return offlineurn::Index(reinterpret_cast<const void*>(urn), static_cast<uint32_t>(g_shipCount));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

const char* RequestShipFromAtc(uint64_t atcEntity, uint64_t player, const char* shipClass) {
    if (!g_getATCComp || !g_requestTakingOff) return "the ATC functions weren't found";
    __try {
        uint64_t handle[2] = {};
        g_getATCComp(handle, atcEntity);
        const uintptr_t atc = handle[0] & kPtrMask;
        if (!atc) return "this terminal has no spaceport ATC";
        if (!player) return "player not spawned";
        alignas(16) uint8_t location[32] = {};
        void* archetype = nullptr;
        void* padFilter = nullptr;
        g_strCtor(location, "");
        g_strCtor(&archetype, shipClass);
        g_strCtor(&padFilter, "");
        g_requestTakingOff(atc, player, location, 0, true, &archetype, &padFilter);
        g_strDtor(&padFilter);
        g_strDtor(&archetype);
        g_strDtor(location);
        return nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "fault while asking the ATC";
    }
}

static void QueueOwnedAtc(uintptr_t asop, int i) {
    // Validate the immutable session binding against the latest server snapshot.
    // Never reinterpret an old UI slot as another ship after a refresh.
    BridgeShip current[256];
    const int count = Bridge_OwnedFleet(current, 256);
    int selected = -1;
    for (int j = 0; j < count; ++j)
        if (!strcmp(current[j].id, g_ships[i].serverId) && !strcmp(current[j].cls, g_ships[i].name))
            if (!strcmp(current[j].state, "stored")) selected = j;
    if (selected < 0) { Log("[delivery] blocked: ship unavailable or server fleet missing; refresh the bridge"); return; }
    uint64_t atc = 0, player = 0;
    __try { atc = Rd<uint64_t>(asop + kAsopAtcId); player = LocalPlayerId(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (!g_getATCComp || !g_requestTakingOff || !atc || !player) {
        Log("[delivery] blocked before reservation: native ATC/player context unavailable"); return;
    }
    if (Bridge_RequestAtc(current[selected], atc, player))
        Log("[delivery] owned ship queued for server reservation; no ATC request or deployment confirmed yet");
    else Log("[delivery] blocked: bridge busy, unresolved journal, or reservation could not start");
}

static void __fastcall Hook_RetrieveVehicle(uintptr_t asop, uintptr_t slot) {
    InterlockedIncrement(&g_fleetRetrieves);
    const int i = SlotShipIndex(slot);
    if (i < 0) { g_origRetrieveVehicle(asop, slot); return; }
    QueueOwnedAtc(asop, i);
}

static uintptr_t DeliverEventSlot(uintptr_t asop, uintptr_t event) {
    __try {
        const uint64_t index = Rd<uint64_t>(event);
        // Session fleet size is an upper bound, not an assumption about the UI's
        // filtered row count. SlotShipIndex separately validates the actual URN.
        if (index >= static_cast<uint64_t>(g_shipCount)) return 0;
        const uintptr_t slots = Rd<uintptr_t>(asop + 0xF0);
        if (!slots || slots > UINTPTR_MAX - (index + 1) * 0x16C0) return 0;
        return slots + index * 0x16C0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static void __fastcall Hook_DeliverVehicle(uintptr_t asop, uintptr_t event) {
    const uintptr_t slot = DeliverEventSlot(asop, event);
    if (!slot) { Log("[delivery] blocked invalid or unavailable Deliver event slot"); return; }
    const int i = SlotShipIndex(slot);
    if (i < 0) { g_origDeliverVehicle(asop, event); return; }
    QueueOwnedAtc(asop, i);
}

void ProcessAtcDelivery() {
    char cls[201] = {};
    uint64_t atc = 0, player = 0, currentPlayer = 0;
    if (!Bridge_TakeAtc(cls, sizeof(cls), atc, player)) return;
    __try { currentPlayer = LocalPlayerId(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (!currentPlayer || currentPlayer != player)
        Log("[delivery] ATC dispatch blocked: player changed while reservation was pending");
    else if (const char* err = RequestShipFromAtc(atc, player, cls))
        Log("[delivery] reserved ATC request failed or uncertain: %s", err);
    else Log("[delivery] %s: native ATC request issued; hangar and ship placement UNCONFIRMED", cls);
    // No completion adapter exists yet. Preserve the server reservation/journal
    // even after a successful native call; never synthesize a deployed entity.
    Bridge_AtcUnconfirmed();
}
