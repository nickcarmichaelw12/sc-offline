# Hangar contract investigation after asop6

This checkpoint documents static analysis of the locally supplied CL12660092
client. It introduces a repeatable compatibility check, not a new working
hangar implementation. Addresses use preferred image base 0x140000000.

## Two different continuations

RequestHangarInstance (0x1451e0720) calls the instance manager at 0x143976f70.
The continuation it binds, 0x14509a9e0 -> 0x145043d80, inspects the tagged result
and logs success/failure. The reviewed body does not create a hangar or place
a ship. Its success branch must not be treated as proof of the entire delivery
lifecycle. In particular, no deployment confirmation adapter was added here.

The underlying instance manager has a separate asynchronous path. Its request
implementation at 0x143a3b860 takes a manager, numeric owner/entity ID and request
record. It obtains the service hub, selects vtable +0x50, then calls that
service's +0x10 method. The downstream diagnostic strings identify this request
as FindHangarsAtLocation. Both missing hub and missing service have native null
branches; the existing fabricated service is not a functioning backend.

The request moves two shared-state/control pairs out of the request record at
+0x30/+0x38 and +0x40/+0x48. These are owned asynchronous objects, not POD buffers
that can be replaced with zeroed memory. Existing placeholder future storage
must not be used as a template for a real result producer.

## The world setup lives deeper

The FindHangarsAtLocation continuation is constructed by 0x1438c6ce0, which binds
0x14397d5d0 -> 0x14395d480. That final function is split across eight chained
Windows unwind entries and extends through 0x143961895. Reading only its first
entry omits nearly all the meaningful work.

The full continuation checks request ownership/lifetime, the query result,
hangar class/component parameters and size, duplicate hangars at a location,
stowing state, existing instances, gateway selection and zone-host/replication
requirements. It also carries owner/location/manager metadata into subsequent
asynchronous operations. A service response listing an arbitrary entity or a
successful empty list cannot establish that those requirements are satisfied.

The next implementation target is the FindHangarsAtLocation result contract and
its creation/unstow dependencies. A usable adapter must supply the correct
owned record and preserve native async ownership, then observe real instance
readiness, elevator registration and ship placement. The Python reservation
remains reserved until that evidence exists. Door control remains downstream.

## Repeatable QA

Run `python3 tools/test_native_hangar_contract.py /path/to/StarCitizen.exe`.
It verifies the relevant relative-call/thunk chains, service slots, the complete
reviewed log observer fingerprint, all eight continuation unwind roots, and the
asop6 Deliver hook's source signature/registration chain. It only reads the
locally supplied executable. It does not execute the game, change it, upload it,
or claim runtime functionality. Keep using the separate native URN regression
for actual isolated execution of identity/deliverability logic.

The compatibility checks passed against the complete supplied archive member
(175324160 bytes), read in memory. A truncated scratch copy was rejected before
analysis. Synthetic parser tests cover chained unwind padding/root resolution,
function gaps, range bounds, truncation, wrong opcodes and cyclic/unsorted
metadata; CI runs those without any proprietary executable.
