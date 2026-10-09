# Offline hangar, ASOP and ATC investigation

Date: 2026-10-09. Scope: existing DLL, its direct dependencies, local Python server, project documentation and latest local test logs. No gameplay changes, new hooks, builds or tests were performed for this report.

## Evidence and version boundary

The latest logs are `upload/mod.log` and `upload/Game.log` in the investigation workspace. They describe **0.7.0-hangar1**, not every change currently present in the working tree. In particular, the tree already contains an unshipped `hangar_lookup.h` selector and version hangar2 from earlier work. Their presence is not evidence of runtime success. Existing dirty source files were left untouched during this investigation.

Source line references below refer to this working-tree snapshot. Native addresses quoted from existing project investigations describe only supplied client CL12660092 at preferred image base 0x140000000; they are not new hook proposals. The repository does not contain the game's original class definitions. Descriptive native function names below are inferred labels from existing analysis, not recovered source declarations.

## Relevant source and direct dependencies

| File and lines | Functions/types | Role |
|---|---|---|
| `src/hooks.cpp:185–211` | `PrepareInstanceGroupQuery`, `Hook_InstanceGroupQuery` | Hub discovery and crash guard; empty group result offline |
| `src/services.cpp:21–83, 146–169` | `FakeService`, `FakeCall`, `AnswerByValue`, `FindServicesObject`, `SwapHubSlot`, `InstanceRequestHub` | Scoped stand-in hub and typed empty hangar response |
| `src/services.cpp:171–215, 237–265` | `RequestInstanceHook`, `RequestListHook`, `ResolveHangarsApi` | Native creation/list entry points and compatibility gates |
| `src/hangar_future.h`; `src/hangar_lookup.h:15–40` | `hangarfuture::Future/Make`, `hangarlookup::Find` | Owned ready-empty future; pending qualified list-function selection |
| `src/hooks.cpp:251–254, 375–391, 437–471` | `Hook_ATCResolve`, terminal tracing, hook registrations | Existing terminal/ATC discovery diagnostics |
| `src/hooks.cpp:542–599` | ATC ABI types, `PrepareRetrieveVehicle`, `PrepareDeliverVehicle`, `LocalPlayerId` | Native takeoff discovery, event validation, player identifier source |
| `src/hooks.cpp:680–716, 731–825` | `Hook_EntitlementsResult`, `SlotShipIndex`, `Hook_DeliverVehicle`, `Hook_RetrieveVehicle`, `QueueOwnedAtc`, `RequestShipFromAtc`, `ProcessAtcDelivery` | Fleet fallback and delivery dispatch |
| `src/teleport.cpp:83–99` | `GetLocalPlayer` | Resolves raw client identity into actor/entity handles |
| `src/spawner.cpp:221–233, 488–549, 1589` | `LocalPlayerEntityId`, `SpawnShipInZone`, `PlayerZonePos`, game-thread processing | Existing entity-ID conversion, working menu spawn, ATC queue consumer |
| `src/bridge.cpp:25–35, 59–118`; `src/bridge.h` | journal, `Worker`, `Request`, `Bridge_RequestAtc`, `Bridge_TakeAtc`, `Bridge_AtcUnconfirmed`, `Bridge_ConfirmedEntity` | HTTP reservation, thread handoff, uncertainty handling |
| `server/server.py:61–85, 105–115, 150–192, 206–211, 328–332` | `Store`, hangars/ships/bridge_ops tables, snapshot, reserve/confirm/recover handlers | Persistent logical state; no native hangar lifecycle |
| `tools/test_native_hangar_contract.py:79–116` | documented contract assertions | Existing list/creation continuation chains and origin checks |
| `docs/hangar-contract.md:10–45`; `docs/hangar1.md`; `docs/asop6.md`; `docs/delivery-investigation.md:10–49` | prior investigations | Native async boundaries and limitations |

Historical delivery-investigation statements that Deliver has no adapter are superseded by current `Hook_DeliverVehicle` and asop6. Its warnings about world readiness and door-control ABI remain applicable.

## VERIFIED: current paths and first failures

### Hangar instance-group lookup

Native lookup → `Hook_InstanceGroupQuery` → services manager getter, vtable +0x18 → real hub if available → original native function. With no hub, or with `StandInHub`, the guard sets `out[1] = out[0]`, producing an empty range. It prevents the missing-hub crash; it does not supply personal hangar records or register destinations. Existing analysis records a caller-side single-owner fallback, but that fallback is not an instantiated hangar.

`mod.log:93–94,135` confirms this unavailable-hub branch ran. The first identified dependency is an absent real services hub; the guard deliberately substitutes an empty group result.

### Elevator discovery and instance creation

The native destination query is separate from instance creation:

1. Native list request `0x143a3d860` → `RequestListHook` → scoped hub adapter → hub +0x50 service → service +0x10 (`FindHangarsAtLocation`).
2. Typed future binder `0x1439fbca0` → list callback `0x14397d600` → continuation `0x1439618a0`, which existing analysis identifies as building choices from local manager/gateway information.
3. Selection/creation uses native manager request `0x143976f70` → implementation `0x143a3b860` → `RequestInstanceHook` → same persistence query → constructor `0x1438c6ce0` → thunk `0x14397d5d0` → continuation `0x14395d480` through `0x143961895`.

The stand-in's `hub.10` slot 2 returns an owned successful **empty** persisted-hangar list. It is not connected to the Python `hangars` table. Other stand-in services can return null, an invalid handle or an abandoned future; these are placeholders, not completed services.

The existing creation hook changes eligible local elevator requests from origin 4 (Elevator) to 1 (InstanceManagerDebug). Prior analysis shows origin 4 refuses new creation after an empty persistence result. Eligibility depends on `owner == LocalPlayerId()`, so identity correctness also affects this branch. Gateway selection occurs later; a null gateway at entry alone is not proof of failure.

**First observed failure:** `mod.log:16` says the hangar ABI was not recognized and the adapter was disabled. Consequently this test did not establish that either request hook ran. The earlier investigation identified a common prologue matching four functions; current unshipped code qualifies candidates by service call and shared binder. That pending selector still requires runtime confirmation.

**Registration boundary:** no DLL implementation explicitly registers an elevator destination. Existing code delegates this to native list/instance continuations. The precise transport registration function, destination record contract and readiness callback are not established in reviewed source/docs. A successful query return cannot be equated with elevator availability.

### ASOP Deliver and Retrieve

Server fleet → bridge snapshot → synthetic owned entitlement URNs → native ASOP rows. Deliver event uses a slot index; Retrieve takes a slot. Both owned paths validate `SlotShipIndex` and converge on `QueueOwnedAtc`. Non-owned native identities pass through original handlers where applicable.

`QueueOwnedAtc` checks the immutable server ship ID/class and stored state, reads terminal ATC identity and `LocalPlayerId`, then calls `Bridge_RequestAtc`. Worker writes the journal and POSTs `/api/v1/bridge/reserve`. A confirmed reservation queues game-thread work. `ProcessAtcDelivery`, called by the existing spawner update, validates unchanged player context, resolves ATC and invokes native takeoff with vehicle 0, selected archetype and empty location/pad-filter strings.

`mod.log:112` confirms a native request was issued; therefore the button is not simply disconnected. `Game.log:4136–4137` reports invalid location configuration and an **Unspawned** request player. That numeric request ID differs from the spawned player's ID at `Game.log:2653`; the spawned player also receives inventory location 810966700 at `Game.log:2668`. Do not conclude the entire world has location zero from this failed request.

**First observed native failure:** player/location inventory configuration during ATC permission processing. **Independent missing completion path:** `ProcessAtcDelivery` always calls `Bridge_AtcUnconfirmed`; there is no ATC completion adapter. `Bridge_ConfirmedEntity` explicitly rejects ATC-route confirmation. Later clicks are blocked by phase 4/journal and the server reservation, consistent with `mod.log:113` and subsequent blocked messages. Issuing takeoff does not prove hangar assignment, delivery or deployment.

### ATC discovery, assignment, zones and doors

Existing terminal tracing observes ATC resolution. `PrepareRetrieveVehicle` derives the ATC component resolver and native takeoff call from the reviewed console-command call chain. `RequestShipFromAtc` resolves the component from the terminal's ATC entity ID before dispatch. The failing test passed that initial nonzero-component gate.

Existing `RequestHangarInstance` analysis locates `0x1451e0720` → instance-manager request and observer `0x14509a9e0` → `0x145043d80`. The observer logs results; it is not proof of ship placement. The deeper instance continuation requires class/size, owner/location, duplicate/stowing checks, gateway and zone-host/replication state (`docs/hangar-contract.md:29–39`). No bridge payload supplies those runtime bindings.

Working menu spawning uses the player's zone, verifies `ZoneId`/`ZoneFromId`, and passes zone plus transform into native spawn parameters (`src/spawner.cpp:488–549`). It does not use an assigned hangar pad, register a transport destination or establish an ATC landing assignment. A zone ID, location ID, instance root, entity ID and Python location string are distinct identifiers.

No reviewed DLL/server function implements landing-clearance completion, arrival routing, instance-root binding or hangar-door commands. Existing documentation mentions `OnHangarDoorStateChange` registration but does not establish a callable open/close ABI. These paths remain unknown rather than implemented by the stand-in hub.

## HYPOTHESIS

1. **Player identity mismatch causes the first ATC failure.** `LocalPlayerId` returns raw `info+8`; `GetLocalPlayer` resolves that value to an actor, while `LocalPlayerEntityId` additionally converts the actor's entity handle to a native ID. The discrepancy in the log strongly supports testing these values side by side. The exact semantics of raw `info+8` must not be assumed from its numeric shape alone.
2. The same mismatch may prevent the creation hook's owner comparison. This is not demonstrated because that hook was disabled in the recorded run.
3. After fixing hook discovery, an empty persistence result may permit native listing/creation to advance, but gateway, size, zone-host or transport readiness may still fail. There is no proof that changing origin alone supplies those dependencies.
4. The takeoff route may ultimately be insufficient to reproduce ASOP Deliver's native asynchronous lifecycle. It is an existing adapter choice, not a verified complete delivery contract.

## UNKNOWN

- Actual destination result count, error and owner in the tested elevator interaction; the adapter never installed.
- Exact elevator destination registration/unregistration API and instance-ready event.
- Local gateway catalogue, suitable hangar prefab/class/size, pad/lift transform, authoritative instance root and zone-host availability at this station.
- Whether correct player identity yields valid ATC location configuration and how terminal location relates to player inventory location.
- Native request correlation, final placement/failure callbacks and reliable cancellation after dispatch.
- Arrival clearance, departure clearance and door controller command/result contracts.
- Whether the first delivery call reserves any native resource despite failing permission. Existing journal retention appropriately treats it as unresolved.

## Smallest instrumentation patch to prove the next dependency

This is a proposal only; no patch was applied.

Use existing hooks and logging, bounded to a few requests. Do not add new binary signatures or modify request state:

1. In `ResolveHangarsApi`, report which existing compatibility predicate failed, candidate counts and whether each hook installed. The pending selector already reports qualified count; retain the distinction between resolution and hook installation.
2. At `RequestListHook` entry, log owner, raw `LocalPlayerId`, converted `LocalPlayerEntityId`, and real/missing/stand-in hub classification. Log invocation/return separately. Do not label function return as future completion or inspect objects after native return if they may be invalidated.
3. At `RequestInstanceHook` entry, log the same identity comparison and already-read origin before existing behavior. Do not infer readiness from gateway state at entry.
4. At `QueueOwnedAtc` and immediately before dispatch, log operation correlation, terminal ATC ID, both player identifiers and component resolution outcome using existing helpers. Preserve the supplied identifier during this diagnostic task. Compare against native ATC and spawn log entries.

This proves hook activation and the identity mismatch without gameplay changes. It cannot prove destination registration or async completion. Instrument those later only after their result/lifetime contract is located; do not propose an unverified callback hook. Reuse native diagnostic logs first. Live server interception is not needed to establish these current failures.

## Proposed local Hangar Manager API

**Design only.** Extend the existing localhost HTTP worker, game-thread queue, SQLite transactions, idempotency keys and journal. No separate online service or authentication changes. Logical server records express intent; only native game-thread observations establish world readiness.

| Proposed operation | State and consumer |
|---|---|
| `GET /api/v1/hangar-manager/context?session_id=…` | Owner, logical home, mapped native location, observed instance/zone/gateway IDs and readiness. Bridge caches; elevator/ATC adapters consume only validated bindings. |
| `POST /api/v1/hangar-manager/ensure` | Idempotent request for owner's hangar at mapped location and required size. Creates pending intent; existing game-thread queue requests native lifecycle. Never declares ready from a database insert. |
| `GET /api/v1/hangar-manager/operations/{id}` | Pending/creating/ready/failed/uncertain plus reason and version. ASOP status and diagnostics consume it. |
| `POST /api/v1/hangar-manager/observations` | Correlated game-thread evidence: instance exists, transport registration, pad transform, ship placement or terminal failure. Readiness requires verified IDs and lifecycle evidence. |
| `GET /api/v1/hangar-manager/destinations?session_id=…` | Owner-visible ready destinations and validated transport bindings; eventual native list adapter consumes with proven ownership/lifetime contract. |
| `POST /api/v1/hangar-manager/release` | Correlated teardown intent; native unregistration/removal must be observed before freeing the assignment. |

Record fields should include logical hangar/owner/location IDs, session generation, operation/version, native owner entity, location, gateway, instance root, zone, hangar entity and transport destination; size/class and pad transform; separate instance/transport/placement states. Native 64-bit IDs should be decimal strings in JSON. Unknown bindings stay absent, not fabricated as valid zero/default values. Do not persist raw pointers across sessions.

The existing `hangars` table stores only ID/name/logical location. It needs a separate session/runtime binding model rather than treating `demo-hangar` or `offline-home` as native instances. Existing bridge reservations should remain the ship exclusion authority. Extend confirmation only after correlated ship-at-assigned-pad evidence; keep working menu spawning's current route intact. Door and clearance APIs should be designed after native contracts are known, not advertised as supported now.

## Recommended sequence, starting with elevator access

1. Establish list-hook activation and owner identity using the bounded diagnostics above. Keep the pending selector change separate from the claim that hangars work.
2. Trace the actual destination result and local manager/gateway inputs. Locate transport registration and instance-ready contracts; establish a mapped station/location before supplying hangar records.
3. Add the smallest Hangar Manager session/ensure/observation path. Demonstrate one owned instance registered as an elevator destination, travel to it, and safe teardown. Preserve native future ownership.
4. Validate the in-hangar ASOP's ATC/player/location context. Resolve the player-ID hypothesis and then any remaining inventory/location dependency.
5. Correlate owned ship reservation → assignment → pad/lift placement → actual entity → server confirmation. Add explicit failure/uncertain outcomes without duplicate spawning or silent journal clearing.
6. Trace and implement arrival/departure permission results, then door control for the assigned hangar. Verify actual door state; event registration alone is insufficient.

The first combined runtime test should stop at the first failed stage: elevator destination → travel → in-hangar owned ASOP delivery → observed placement. ATC/doors follow only after that boundary is proven. This investigation stops here.
