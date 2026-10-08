# Native delivery and hangar integration checkpoint

Scope: locally supplied 4.10.193.11644 / CL12660092 executable. Addresses below
are preferred virtual addresses (image base 0x140000000), not runtime addresses
or portable hook signatures. No game code, account data or logs are included.

## Delivery is distinct from retrieval

The ShipSelectorDeliverVehicle event registration at 0x144c3db80 stores the
callback 0x144c11d40. That callback jumps to 0x144c09c60. The latter reads the
event's 64-bit slot index and selects the entitlement URN using the slot-vector
base at ASOP + 0xf0, stride 0x16c0, URN offset 0x1698. It checks client/player
context and resolves a player component before starting an asynchronous request
through 0x144bc07e0 and binding a continuation through 0x144c1cc00.

The separate retrieval event handler at 0x144c11da0 calls 0x144c2b0f0. Our existing
Hook_RetrieveVehicle covers that path only. It does not cover DeliverVehicle.
Do not assume fixing the URN or intercepting Retrieve also routes Deliver into
the local Python server. Do not substitute above-player spawning for delivery.

The RequestDeliverVehicle string reference at 0x144be44a7 belongs to a structured
logging call. The string alone does not establish an independently callable
delivery ABI. Likewise the OnHangarDoorStateChange registration is not evidence
of a callable door-open function.

## Hangar request is asynchronous

RequestHangarInstance is located at 0x1451e0720. Its signature string identifies
an ATC action request, vehicle class, notification output and request origin.
The function contains invalid-vehicle and unsupported-size failure paths. Near
its end it calls 0x143976f70 and binds callback 0x14509a9e0 using 0x1436c1920.
Further investigation of the request/future contract is needed before invoking
or replacing this path. Issuing a request is not evidence of an instantiated
hangar, assigned pad, elevator destination, ship entity or usable door controller.

The existing services stand-in cannot fulfill that contract. Its returning a
placeholder service pointer must never authorize a deployed server state.

## Next implementation boundary

1. Resolve the Deliver request/continuation ABI and carry the immutable owned
   ship binding into an authoritative local-server reservation.
2. Obtain a real hangar assignment and readiness result, including elevator
   registration and the assigned pad transform.
3. Observe the actual ship entity at that assignment before confirming deployment.
4. Resolve door control for that hangar, with failure/timeout handling.

Keep uncertain operations reserved rather than retrying or claiming success.
The eventual Deliver adapter and the menu spawn path must share reservation
exclusion. No new runtime delivery/hangar hook is installed by this checkpoint.

Offline coverage: tools/test_native_urn.py now executes native IsDeliverable
with controlled thread/logging dependencies and passes. This narrows the
identity defect without requiring another user launch; full UI and world
behavior still require a later combined in-game test.
