# ASOP local-host continuation experiment

## Status: asop1 withdrawn; asop2 corrects the register transition

The runtime test of asop1 crashed before fleet fetch. Dump analysis identified
an access violation at preferred VA 0x1403D2720 (`mov rax,[rcx+0x18]`). RCX
held the numeric channel, not an object pointer. Stack return addresses matched
0x14511334E and 0x144C1C188: the latter call passes RDI to the ATC helper.
The authority branch writes the channel into EDI at 0x144C1BCCC, destroying
the original RDI handle. The normal client branch bypasses that write.

asop2 replaces the NOP continuation with a near relay that restores RDI from
[RBP+0xD8], matching the original load at 0x144C1BC66, then jumps to the
existing client-flag check. The successful authority branch does not overwrite
that stack slot. Exact-byte checks now also verify both register loads.
This addresses the demonstrated invalid-pointer transition; further engine
dependencies and actual terminal operation still require runtime validation.
Use bridge1 for rollback. Do not use asop1 again. No server update is required.

The original experiment notes below describe the superseded asop1 build.

Version: 0.7.0-asop1. Server 0.2 remains unchanged.

The bridge1 runtime test recorded three terminal opens, each with a nonzero
channel, virtual slot 0x7a8 returning 1, and client-path flag 1. None reached
the fleet-fetch entry. The elevator separately hit the missing-services-hub
crash guard. These are distinct failures.

On the inspected CL12660092 executable, OnRequestOpen's observed branch saves
the channel at component +0x9e8 and jumps to cleanup. The alternative branch
runs client UI initialization and dispatch, guarded by the client flag and a
local-player comparison. This experiment replaces only that five-byte cleanup
jump with NOPs, allowing both halves to run in the offline local host.

The patch requires an exact unique signature, the existing terminal-open hook,
and the branch observer's independently validated flag operand. It leaves the
virtual result, channel assignment, client flag, and local-player check intact.
Unknown builds fail closed. It does not create async results or hangar instances.

Static validation: unique match in the supplied executable and matching branch
observer flag location. Windows CI validates compilation and bridge regression
tests. Actual UI behavior still requires an in-game test; dispatch might expose
another unavailable service or fail to reach fleet fetch.

Test: replace dinput8.dll beside the mod launcher with this build, start the
existing local server, open ASOP, wait 30 seconds, and collect mod.log/Game.log.
Look for the applied continuation patch and fleet-trace fetch/list callbacks.
Keep bridge1 DLL to roll back if opening a terminal crashes or regresses.
The existing offline catalogue fallback is not the server-owned fleet mapping.
Hangar creation/elevator registration remain unimplemented; the crash guard stays.

Also restores the hangar request's thread-local scope on exception and corrects
the stand-in installation log so it no longer implies hangar requests succeed.
