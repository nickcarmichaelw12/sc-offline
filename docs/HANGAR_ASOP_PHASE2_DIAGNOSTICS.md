# Hangar/ASOP Phase 2 diagnostics

This patch observes the existing working-tree behavior. It does not change player IDs, native request arguments, reservations, fleet mapping, completion policy or existing hangar2 creation behavior. No new hooks or native offsets were added. Version remains `0.7.0-hangar2`; presence of `[hangar-diag]` and `[asop-diag]` distinguishes this build.

## Files

Diagnostic changes: `src/services.cpp`, `src/hooks.cpp`, `src/bridge.cpp`, `src/bridge.h`. `Bridge_DiagnosticOperation` only copies the existing operation ID under a shared lock. This document and `HANGAR_ASOP_PHASE2_DIAGNOSTICS.patch` describe/package the change. The patch is relative to the dirty working tree captured before Phase 2, not to Git HEAD.

The diagnostic build also includes the already-existing unshipped `src/hangar_lookup.h` and `src/version.h` hangar2 changes because services.cpp already depends on them. They are not new Phase 2 fixes. Other pending source/workflow/test changes remain untouched. There are no Python server changes.

## Logging bounds and meaning

Startup compatibility diagnostics report each failed existing predicate, raw/qualified list candidates, service-object candidates and intermediate resolution stages. Dependent byte predicates are skipped when their pointer is absent; the pointer predicate identifies that failure. Installation is logged separately for both existing hooks.

List, instance, queue, dispatch and component-resolution diagnostics each log only their first 20 invocations per process; services-object discovery logs its first five. Counts are atomic and trace IDs are per boundary. `op=` is the existing bridge operation ID, connecting an accepted queue to dispatch/resolution and the journal/server operation. Rejected requests do not receive a new operation ID. Existing non-diagnostic logging is retained.

Observation uses separate local variables; converted player IDs never replace the raw ID or change comparisons. Component resolution is logged from the existing resolver call, without calling it again. Hangar hub classification uses the existing getter and reports its underlying result before entering the scoped adapter; `missing` can therefore coexist with the subsequent stand-in response. Function returns are explicitly not asynchronous completion. No engine objects are inspected after native return.

| Marker | What it distinguishes |
|---|---|
| `[hangar-diag] resolve raw_list_candidates=… qualified_list_candidates=…` | Common-prologue ambiguity versus a unique qualified candidate |
| `services_object_candidates=…` | Missing/ambiguous services-object match |
| `compatibility FAIL <name>` | Exact failed ABI/dependency predicate |
| `hooks instance=not-attempted list=not-attempted reason=compatibility` | Resolution/ABI rejection before detour installation |
| `hooks instance=installed/failed list=installed/failed` | Actual installation outcome, including partial failure |
| `[hangar-diag] list/instance #… entry` | Runtime boundary reached; owner/raw/entity IDs, match flags, origin, hub and read status |
| `list/instance #… returned; not async completion` | Native call returned; destination/creation still unproven |
| `[asop-diag] queue #…` | Selected owned ship, terminal ATC, supplied raw player, converted entity and function availability |
| `queue #… accepted op=…` | Reservation work accepted, with operation correlation |
| `[asop-diag] dispatch #… op=…` | Reservation handed to game thread; supplied/current/converted player comparison |
| `[asop-diag] resolve #… op=… component=resolved/null` | Existing terminal ATC component lookup result |
| `resolve … unavailable` or `request … fault` | Missing functions or fault; a request fault does not prove no native side effects |

Native `Game.log` remains the source for subsequent permission/location errors. `Bridge_AtcUnconfirmed` and journal lockout remain unchanged.

## One in-game test

1. Exit Star Citizen. If the previous run left an unresolved reservation, use the existing `server/Recover-Bridge.cmd` procedure after exit. Do not manually remove only the journal. Back up the installed DLL and replace it with this diagnostic build's `dinput8.dll`. Keep the existing server/config/fleet.
2. Launch offline, confirm version hangar2 and diagnostic startup markers, and open the same station's hangar elevator once. Record whether the personal hangar appears. If it appears, select once and record whether travel finishes.
3. Open one ASOP (inside the hangar if reached; otherwise the same station's main terminal). Select one server-owned stored ship and request delivery once. Wait about 30 seconds; do not retry, use menu spawning or request doors during this test.
4. Exit and send this run's `mod.log` and `Game.log`, plus the furthest successful step. A screenshot is optional. An unavailable elevator destination does not prevent the single main-terminal identity diagnostic.

## VERIFIED / UNKNOWN

VERIFIED from source: unchanged native argument expressions and player comparisons; separate observational entity-ID reads; existing resolver result logged without duplicate resolution; bounded diagnostics; operation snapshot does not advance bridge state. The diagnostic-only patch excludes earlier hangar2 changes.

UNKNOWN until this test: which runtime compatibility predicate fails, hook reachability, identity equality at these boundaries, whether a valid ATC component resolves, and all asynchronous hangar/destination/placement/clearance/door outcomes. No functional changes should be proposed from startup success alone.
