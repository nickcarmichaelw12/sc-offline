# asop3: owned fleet and native retrieval investigation

ASOP's failed/empty entitlement fallback now uses the authenticated local server
fleet rather than ships.txt. Opening ASOP starts a worker refresh. If the fleet
is not loaded before the callback, close and reopen the terminal after connection.
An empty owned fleet stays empty; unknown classes are skipped. Each server ship
gets its own slot binding, including multiple ships of the same class.

The slot mapping and entitlement memory remain fixed for the game session to
avoid dangling engine references or remapping an open terminal slot. Restart the
game after changing ownership. Class registry initialization can retry if no
ships could yet be resolved. Current server state is checked again from the
latest bridge snapshot before a native retrieval request. This is not a fresh
transactional reservation and does not synchronize native ATC delivery with the
bridge spawn menu. Do not use the two spawn paths together during this test.

Retrieval calls the existing native ATC path using the terminal's ATC entity,
local player, and selected owned ship class. A one-request-per-session guard
prevents repeated calls while completion remains unknown. A failed or uncertain
request also keeps that guard because native side effects may have occurred.

The hangar instance hook now logs entry explicitly. The service stand-in still
does not implement real hangar allocation, instance creation, elevator destination
registration, pad transform discovery, or ship placement confirmation. These
require verified engine adapters; a database hangar row cannot substitute for
them. ASOP requests do not mark the server ship deployed.

Test: use the existing server, refresh the bridge, open ASOP and check the owned
list, retrieve one stored ship once, then check the elevator. Collect mod.log and
Game.log (and error.dmp if it crashes). This test identifies the next native
request boundary; it is not an end-to-end delivery implementation.

Remaining delivery sequence: reserve owned ship transactionally; request a real
hangar; observe instance and pad readiness; register access/elevator destination;
spawn at verified pad transform; confirm entity placement before committing
deployment; reconcile interrupted operations without duplicate ships.
