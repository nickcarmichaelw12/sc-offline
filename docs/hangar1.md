# 0.7.0-hangar1 — elevator-first test candidate

This is an experimental runtime change, not a confirmed working personal
hangar. The intended test is elevator -> instanced hangar -> owned ship using
the ASOP inside it. Keep the existing server, fleet and bridge configuration.

## What changed

The native destination-list request at 0x143a3d860 calls FindHangarsAtLocation
independently of RequestInstanceImpl. The previous TLS-scoped service adapter
never covered that request: its missing-hub branch returned without resolving
the list. Both native request functions now enter the offline adapter scope.
The real hub still takes precedence. The existing group-query guard recognizes
the stand-in and retains the game's caller-side single-owner fallback.

The completed empty result now has per-request storage and two correctly owned
shared pointers. Native callback consumption/destruction releases the storage;
the old allocation leaked and shared one lock across unrelated requests.
This result means no persisted backend hangars in this local session. It does
not claim an instance exists. The native list continuation still builds the
choices from its local manager/gateway information.

Native elevator origin 4 refuses to create a missing instance; it only reopens
one found by the persistence lookup. For an offline request owned by the local
player, the adapter changes that request's
origin to the native creation-capable InstanceManagerDebug value 1. Owner,
size, gateway selection, callbacks and native creation/registration remain intact.
The continuation selects the gateway later at 0x14395f3e7; requiring a non-null
gateway before invoking it would incorrectly block a first visit. The
request may be erased during its synchronous continuation, so the adapter does
not touch it after returning from native code. Other owners and real-service
requests retain their origin. ABI/continuation guards disable these hooks when
the reviewed layout does not match.

No backend ship deployment is confirmed from a hangar-list or request result.
The asop6 owned-ship/ATC reservation path remains in use. Persisted hangars,
hangar ASOP placement, elevator travel, and doors are still runtime-unverified.

## QA

`tools/test_native_hangar_future.py` executes the supplied CL12660092 native
future binder and destructor against `src/hangar_future.h`, with only thread
identity isolated. It verifies 1000 immediate successful empty-list callbacks,
1000 discarded ready futures, cleared moved-from futures and exactly one
destruction/deallocation per allocation. It does not execute world services.

`tools/test_native_hangar_contract.py` checks the separate elevator request,
shared binder, list callback, origin discriminant/branch and enum strings in
addition to the existing ASOP/hangar contract. Proprietary input remains local.
CI checks source compilation and builds the Windows DLL and launcher.

## One combined in-game test

1. Exit the game. Back up the current DLL and install the hangar1 test DLL in
   the same place. Start the existing local server, then launch offline as usual.
2. Open a spaceport hangar elevator and look for your personal/instanced hangar.
   Select it once and allow loading. Do not use the mod's above-player spawner.
3. If you reach the hangar, open its ASOP, select one server-owned ship and use
   the displayed Deliver/Retrieve action once. Check whether it appears on the
   hangar pad/lift. Wait for the result before another request.
4. Exit and keep this run's mod.log and Game.log. Report the furthest successful
   step and any on-screen error; one screenshot of the failing screen is useful.
   If there is no hangar destination, stop there rather than trying ship delivery.

Look for version `0.7.0-hangar1`, the scoped adapter installation, destination
list return, and (after selection) the offline elevator creation-origin message.
Those messages establish which path ran, not that travel/spawning succeeded.
An uncertain ATC reservation still needs the existing recovery procedure after
the game exits; this build does not silently clear or confirm it.
