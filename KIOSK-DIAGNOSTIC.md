# Kiosk diagnostic 2

Version 0.3.0-kiosk-diag2. Adds observation-only hooks to the vehicle fetch handler and ASOP list builder, retaining diagnostic 1 response handling. Hangars and inventory are not fixed. No request completion is fabricated.

Both new signatures are unique in the supplied StarCitizen.exe and checked against their log-string references. Trampoline sizes cover complete instructions without RIP-relative operands. The fetch handler reads a request field at +0x10 that the original function checks; -1 means unreadable. It logs entry and return separately. Return does not imply asynchronous completion.

Run from a separate folder. Open a ship kiosk for 45 seconds, close and reopen it once, then exit. Send data/mod.log and Game.log. Confirm version diag2 and both fleet trace hook installation messages. Counters distinguish fetch entry, entitlement response, and list builder entry.

The policy test passed locally; Windows build status is recorded in GitHub Actions. In-game behavior remains untested until the user test.
