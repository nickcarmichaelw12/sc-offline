# SC Local Server 0.2.0 + Bridge 1

This experimental bridge connects our DLL's Vehicles menu to the localhost
backend. It lists owned ships, reserves a ship before spawning, waits for the
game to resolve the new entity, then saves deployment. Entity existence does
not prove every ship component has loaded or that boarding succeeded.

## Install

1. Close the game, mod launcher and old local server. Back up the server's data folder.
2. Extract the Server folder contents over your existing local-server folder;
   keep its data folder. Alternatively, use a fresh server folder for a fresh database.
   Server 0.2.0 migrates schema 1 to 2. Keep the backup: server 0.1.0 cannot reopen schema 2.
3. Back up your mod's existing dinput8.dll, then put the DLL from Mod beside your
   existing sc-offline.exe launcher. Do not put it directly in LIVE/Bin64.
   Your existing 0.6.1 launcher can be kept; the DLL source uses the merged 0.7.0 base.
4. Run Connect-Mod.cmd in the server folder. Choose the folder containing the
   mod launcher. It writes data/bridge.ini there with the localhost port and token.
   If your mod uses a custom SC_OFFLINE_MOD_LOG location, put bridge.ini beside that log instead.
5. Run Start-Server.cmd. Add sample records if this is an empty server.
6. Launch offline as usual. Keep block_network enabled; do not disable offline
   protection to troubleshoot a connection. In game press M, open Vehicles,
   then Local server fleet -> Refresh server fleet.
7. In an open area, use Spawn owned ship. It requests a ship 30 m above you and
   queues the existing pilot-seat/flight-ready behavior. The backend should show
   deployed only after the engine resolves the entity.

Do not use the launcher's upstream Update button with this custom DLL; it may
replace it. Set check_updates = off for this development setup. Preserve your
existing settings, especially the working EAC/hosts configuration.

## Scope and limitations

- Localhost only, bearer-token authenticated, no proxy or redirects, bounded replies.
- HTTP work runs off the game/render thread. Game functions run on the existing game thread.
- Only this new owned-fleet section is connected. The ordinary unrestricted spawn
  list remains separate and does not write to the server.
- Inventory, wallet, kiosks, physical hangars and elevators are NOT connected yet.
- No live storage/despawning, ship damage/cargo persistence, or physical world restoration.
- A reservation is not deployment. A timeout or ambiguous response leaves a
  reservation/journal instead of issuing another spawn.
- Game-thread entity appearance and firewall compatibility still require an
  actual in-game test on your PC. Passing CI is not an in-game test.
- This bridge supports at most 256 ships, ASCII letters/digits/underscore/hyphen
  in ship IDs and class names, and bounded display names. Unsupported fleet data
  fails closed instead of being passed into the game.

## After exiting / interrupted session recovery

Close Star Citizen, then run Recover-Bridge.cmd and select the paired mod folder.
Confirm that the session ended. The server checks that StarCitizen.exe is absent,
returns bridge-managed deployed/reserved ship records to the unmapped offline-home
location, clears unresolved reservations, and archives the selected mod's pending
journal. This is explicit session recovery, NOT in-game storage. It does not restore
cargo or physical entities. Do not delete a pending journal while the game runs.

Recovery affects bridge-managed records in this single-player database, not
unrelated inventory or wallet data. If it fails, keep the pending journal and send
mod.log and server.log. Do not send bridge.ini or api-token.txt: they contain the token.

## Persistence and backups

Database: data/offline.sqlite3 beside the server EXE, independent of working directory.
The control page offers a consistent database backup. Keep data when updating.
Restore only with the server stopped: move the old data folder aside and put your
backup in a fresh data folder as offline.sqlite3. Re-pair the mod after a token change.
Do not mix a restored database with old WAL/journal files.

Dashboard deploy/store buttons edit records, not the game. They cannot alter a ship
with a pending bridge reservation, or store a bridge-deployed ship. Use the bridge
and ended-session recovery instead.

## API and development

Python 3.12+ standard library; run python server.py --open. Tests: python -m unittest -v.
Windows executable is packaged with PyInstaller 6.16.0. No Python install is needed
for the packaged application. Source/tests and GPL-3.0 LICENSE are supplied.

Existing /api/v1/state, /backup, /profile, /wallet/adjust, /locations, /hangars,
/ships, /ships/{id}/transition, /inventory/adjust, and /demo remain available.
All API routes need Authorization: Bearer TOKEN. POSTs require JSON and a unique
Idempotency-Key header. Retry identical requests with the SAME key; different
requests under the same key are rejected. /health is a public local health check.
The legacy game_connected field stays false: continuous game-session presence is
not tracked. The DLL's own connection status is the authoritative bridge indicator.

New authenticated routes:
- GET /api/v1/bridge/fleet: SCBRIDGE1 newline header, then tab-separated lowercase
  UTF-8 hex ID, class, display name, state, and decimal version, one record per line.
- POST /api/v1/bridge/reserve: ship_id, expected_version, operation_id (32 hex chars).
- POST /api/v1/bridge/confirm: operation_id, entity_id (nonzero decimal uint64 string).
- POST /api/v1/bridge/cancel: operation_id, reason. Administrative recovery only;
  do not cancel while an engine operation may still be active.
- POST /api/v1/bridge/recover: confirm_session_ended=true; Windows host process check.

Bridge confirmations record entity existence only. Reservations survive restarts
and do not expire automatically. Ended-session recovery is deliberately explicit.
The game-to-server protocol here is our adapter API, not CIG's native service API.

The asop6 development DLL routes owned ASOP Deliver/Retrieve requests through the
same reservation lane as the menu spawner. It can submit a native ATC request,
but cannot yet confirm hangar creation, placement or doors. Such requests retain
their reservation/journal and do not save a deployed ship. Do not interpret a
successful request as delivery completion; exit the game before recovery.
