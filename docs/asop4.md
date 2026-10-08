# asop4: mapped offline identifiers at ASOP action gates

Scope: action eligibility only. Does not implement ship delivery, hangar creation,
elevator registration, or ATC door control. Aurora mapping is intentionally unchanged.

Static tracing found a direct reason the synthetic ownership rows could have no
actions. The native predicate at preferred VA 0x140441BA0 returns false when
either header byte equals 0x11/0x1e or the discriminator at +8 equals 5. All
our synthetic URNs use those values. Three ASOP callers test this predicate
before calling the provider's IsDeliverable function, so our rows cannot reach
that check even though entitlement state +0x54 is already set to 1.

IsDeliverable is at 0x1456BEB60: it looks up the URN in 0x138-byte provider rows
and compares row +0xA0 with 1. The entitlement callback copies entitlement
+0x54 into that row field. This makes identifier rejection a demonstrated gate,
independent of the separately observed delivery-service failure.

The patch redirects only the three ASOP call sites to a wrapper. Recognized
synthetic URNs must match the header, discriminator, payload tag, 64-bit marker,
and a mapped session index with a server ID. Other identifiers go through the
unchanged native predicate. No global validation or service authorization is
disabled. The native IsDeliverable check itself remains unchanged.

Validation: exact unique signatures at all three call sites in CL12660092;
all target the same fully matched predicate. Call relays retain native stack
and register ABI. Windows CI performs compilation and existing bridge tests.
Runtime button visibility remains unverified until the next test.

Next test: open ASOP, select one stored ship, note which buttons appear. If a
Deliver/Retrieve button appears, attempt it once and collect mod.log/Game.log.
Keep asop3 for rollback; capture error.dmp if a crash occurs. Do not infer
successful delivery from button visibility or a request being issued.

Further investigation: the executable's embedded delivery descriptors distinguish
pending/delivered/failed/cancelled records and link them to entity, inventory,
requester URN, and entitlement slot. The gRPC stream handler is separate from
the ownership callback. Native hangar service and world-instance adapters remain
missing. The existing ATC landing/takeoff commands and hangar-door state-change
event are leads, not yet verified door-control APIs.
