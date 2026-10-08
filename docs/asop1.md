# ASOP local-host continuation experiment

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
