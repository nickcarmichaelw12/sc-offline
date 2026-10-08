# asop6: reserved ASOP delivery dispatch (incomplete world lifecycle)

Deliver and Retrieve now share the local bridge reservation lane with the menu
spawner. Deliver is matched by its unique handler signature and independently
cross-checked through the ShipSelectorDeliverVehicle event registration/thunk.
Its 11-byte trampoline prologue contains complete non-relative instructions.
Event index arithmetic is bounded by the immutable session fleet limit; the
selected slot must also carry a valid mapped offline entitlement identity.

Both ASOP actions validate the owned ship ID/class against the latest fleet,
require stored state and native ATC/player context, then reserve asynchronously
using the server's optimistic version and operation ID. The journal is durable
before the HTTP call. Only entity IDs and copied class text cross the worker
boundary; no ASOP/player/ATC pointers are retained there.

The game-thread dispatcher rechecks the player identity and resolves ATC again.
ASOP work cannot be consumed by the above-player menu spawner; menu work cannot
be consumed by ATC. Short output buffers do not consume or truncate work.
Duplicates and Refresh cannot replace an active request. The existing entity
confirmation API explicitly rejects ATC work and zero entity IDs.

IMPORTANT: this only submits the existing native ATC request after reservation.
It does not implement delivery-service responses, hangar instantiation,
elevator registration, pad placement or door control. The native call's return
is not completion. The reservation/journal remain unresolved, including on
player changes or uncertain native failure. Recovery still requires game exit.
There is no timeout cancellation or automatic retry. This is a development
checkpoint, not a completed hangar-delivery release.

QA includes the native URN/IsDeliverable regression; the unique hook signature,
registration chain and slot-layout fingerprint against the supplied client;
server and packaging regressions; and expanded Windows bridge tests using the
actual C++ HTTP worker against a temporary Python server. The latter cover menu
confirmation, ATC routing/exclusion, duplicate requests, short buffers, rejected
ATC confirmation, retained journals, refresh while uncertain, and server version
conflicts. No live engine behavior is simulated or claimed by transport tests.
