# Owned-ASOP entity identity correction

Scope: only `QueueOwnedAtc` and `ProcessAtcDelivery` in `src/hooks.cpp`. The minimal production diff is `OWNED_ASOP_IDENTITY.patch`, relative to the preserved Phase 2 dirty working tree. Added `tools/test_owned_asop_identity.py` exercises the actual queue/dispatch bodies with isolated dependencies and synthetic IDs; it does not execute the game.

## Contract evidence

Reviewed CL12660092 text was re-read locally, without introducing hooks/signatures:

- Existing `PrepareRetrieveVehicle` resolves the console takeoff call at command +0x190. At preferred address `0x1449a5f60` it calls `0x1451e58e0`, with the player value in RDX. The console gets that value into its output slot through `0x142e6c620` before the call.
- Takeoff preserves incoming RDX at `0x1451e590f`, then copies it into R8 at `0x1451e591d` for entity-system lookup via vtable +0x128 at `0x1451e5927`. It therefore consumes an entity identifier, not the raw client-context value.
- The previously reviewed permission chain at `0x1451ef72f–0x1451ef767` sends the request player to `0x14511af70` and to inventory configuration. The location resolver calls the entity-system +0x120 lookup with that player at `0x14511af89`, then resolves the actor and its location. Failure is consistent with an identifier that cannot resolve to the spawned entity.
- Existing `LocalPlayerEntityId` (`src/spawner.cpp`) checks the conversion function, resolves the live actor using `GetLocalPlayer`, converts the actor's entity handle at actor+8 using `g_sp.handleToId`, and returns zero on unavailable conversion, absent player or fault. No new conversion ABI was introduced.

The original client file has a truncated later section; only its fully backed text ranges were used for these reads. A separate recovered scratch file was also truncated and rejected for analysis.

The named `Game(1).log` and `mod(2).log` were not present among available files. The user's quoted Phase 2 agreement between converted entity/elevator/spawn IDs and the raw-value Unspawned rejection is reported evidence, not independently re-read evidence in this task. Earlier available logs show the same category of raw-player/location rejection. The native contract establishes this correction independently of the missing new attachments.

## Behavior

Owned Deliver/Retrieve now queue the nonzero `LocalPlayerEntityId`. Dispatch compares the queued ID with the current ID from that same helper, rejecting changed/unavailable identity and zero queued identity. There is no fallback to `LocalPlayerId`. Raw ID reads remain bounded observations only, isolated from the decision. No separate raw equality gate was required: the existing helper already validates the actor's live context.

The unchanged takeoff call still receives its original location, vehicle, archetype and pad arguments. Global `LocalPlayerId`, elevator owner eligibility/origin handling, non-owned native pass-through, bridge code, reservation/journal/recovery, fleet and menu spawning remain untouched. All dispatched/blocked ATC operations still become unconfirmed; no automatic retry or deployment confirmation was added.

Existing bounded queue/dispatch/resolve markers retain operation correlation. Queue and dispatch now include `identity=entity`; dispatch records `current_entity_player` and observational `raw_player`. `passed_player` is the value sent through the bridge into takeoff. Version remains hangar2; use this marker and the build commit to distinguish it from Phase 2.

## Validation and one runtime test

`python3 tools/test_owned_asop_identity.py` compiles extracted production bodies with strict C++20 warnings and verifies: converted ID queued despite different raw ID; unchanged entity dispatches that exact ID; changed/unavailable current entity blocks; unavailable queue identity does not reserve or use raw fallback; zero queued identity blocks; no queued operation does nothing; uncertainty is retained after dispatch or rejection. This is control-flow coverage with mocked services, not native world validation. The fixture's SEH spelling is mapped to standard exception syntax; real SEH compilation is checked by the Windows DLL build. Bridge protocol tests and the existing Windows native bridge transport check remain separate gates.

1. Exit the game and use existing `Recover-Bridge.cmd` if an unresolved reservation remains. Keep server/config/fleet unchanged; back up and replace the DLL with this build.
2. Launch offline, open one ASOP and request one stored server-owned ship exactly once.
3. Wait 30 seconds, then exit and collect this run's `mod.log` and `Game.log`.
4. Check correlated queue/dispatch/resolve `passed_player` against the native spawned-player entity ID. Compare native permission errors for that operation: does Unspawned/location rejection disappear, or what is the next failure?

Success for this isolated task is carrying the spawned entity ID into native takeoff. Dispatch alone does not prove delivery, placement or hangar readiness. The rejection's disappearance and any next failure are UNKNOWN until these runtime logs arrive. Stop here; do not extend into functional hangar/door work.
