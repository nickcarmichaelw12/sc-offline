# SC Local Server 0.1.0

A primitive localhost-only backend for a persistent offline single-player project.
This release stores profile/wallet, owned ships, per-location inventory, hangar
assignments and an event history in SQLite. It is a runnable backend foundation.
**The mod and game do not connect to it yet.** No game executable, logs, personal
account data or network captures are included.

## Windows quick start

1. Extract the entire release ZIP to a writable folder, such as `SC Local Server` on your Desktop.
2. Double-click `Start-Server.cmd`. No Python installation or administrator rights needed.
3. The control page opens at http://127.0.0.1:18870. Keep the server window open.
4. Use **Add sample records** for a demo pilot balance, Gladius record, hangar and sample inventory; or create your own records.
5. Close and reopen the server. The records remain saved.

This runs independently of the offline mod; you do not need a new diagnostic DLL
or a game session to try it. It does not alter LIVE, the launcher, firewall or EAC.
If the default browser does not open, visit the address above manually.
If the server is already running, use its existing control page.

The demo item `demo_supply_crate` and location `offline-home` are illustrative
backend IDs, not verified game records. The ship class field accepts a game
entity-class name such as `AEGS_Gladius`; class existence is not validated yet.
Deploy/store buttons change server records only. No ship spawns, hangar instances,
elevator destinations, inventory items or game credits are created by this release.

## Save data and backups

The `data` folder is created beside the executable, regardless of your working
directory. It contains `offline.sqlite3`, SQLite journal files when active,
`api-token.txt`, and rotating `server.log` files. Keep it when updating.
Use **Download database backup** for a consistent snapshot while the server runs.
To restore: stop the server, move the whole old `data` folder aside, create a new
`data` folder, and put the backup there named `offline.sqlite3`. Starting again
regenerates the local token. Keep your old folder until you verify the restore.
Do not replace a live database or mix it with another copy's WAL/journal files.

Each write commits immediately. Invalid operations roll back. Idempotency keys
prevent duplicate application of a retried request, including across restarts.
Ship versions reject stale transitions, and a hangar can hold only one stored
ship. This model records storage; it is not an engine hangar allocator.

## Developer launch

Uses the Python 3.12+ standard library only; no runtime pip dependencies.

```sh
cd server
python server.py --open
python -m unittest -v
```

Optional: `--port 18870 --data-dir PATH`. Binding is always IPv4 loopback
`127.0.0.1`; there is no remote-host option. The local control page loads no
external scripts. API writes require JSON, a local bearer token and an idempotency
key. Cross-origin requests and unexpected Host headers are rejected. This is a
single-user local development service, not an Internet-facing multiplayer server.

## API v1

Base: `http://127.0.0.1:18870/api/v1`. Read the token from `data/api-token.txt`;
keep it local. All API routes require `Authorization: Bearer TOKEN`.
POSTs also require `Content-Type: application/json`, `Content-Length`, and
`Idempotency-Key: UNIQUE_REQUEST_ID` (1–100 printable ASCII characters).
Reuse the *same* key and identical payload for a retry. A different payload under
the same key returns 409. New intentions need new keys. Bodies are limited to 64 KiB.

| Method / route | JSON body or result |
| --- | --- |
| GET `/health` (outside `/api/v1`, no token) | Status/version; `game_connected: false` |
| GET `/state` | Profile, locations, ships, hangars, inventory and latest 100 events |
| GET `/backup` | Consistent downloadable SQLite snapshot, including event and retry history |
| POST `/profile` | `{"name":"Offline Pilot"}` |
| POST `/wallet/adjust` | `{"delta":1000,"reason":"Initial balance"}` |
| POST `/locations` | `{"id":"my-outpost","name":"My outpost"}` |
| POST `/hangars` | `{"name":"Hangar A","location_id":"offline-home"}` |
| POST `/ships` | `{"class_name":"AEGS_Gladius","name":"My Gladius","location_id":"offline-home"}` |
| POST `/ships/{id}/transition` | `{"action":"deploy","expected_version":1}` or `{"action":"store","hangar_id":"ID","expected_version":2}` |
| POST `/inventory/adjust` | `{"item_class":"ITEM_CLASS","location_id":"offline-home","delta":5}` |
| POST `/demo` | `{}`; only accepted before any successful mutation |

Successful operations return 200 JSON. Errors return `{"error":"message"}` with
400 (validation), 401/403 (access), 404 (missing), 409 (conflict), 411/413/415
(request framing/type), or 503 (database unavailable). Retry a 503 with the same
key. The state endpoint is a consistent read transaction. Wallet range is
0–9,000,000,000,000; item quantities are 0–1,000,000. Zero quantities remain as
records but are hidden by the control page. Negative adjustments remove funds/items.

This is our own documented adapter API, not an implementation of CIG's gRPC
services. The next integration step is an explicit mod-side client that reads
these records and translates verified engine operations. Database state should
only reflect a world operation after the bridge has confirmed its result.
Missions, reputation, NPC simulation and multiplayer are not implemented.

## Build and verification

GitHub Actions runs the state/API tests on Windows, packages with PyInstaller
6.16.0, then launches the resulting EXE, exercises the API, stops it and restarts
it to verify persistence. Source, dashboard and tests are included in the release
under `source/`. The executable is unsigned. GPL-3.0: see the included repository
LICENSE. Packaging dependencies and license notices are under `licenses/`.
