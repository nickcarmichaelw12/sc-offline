"""Local single-player state service. No game networking or engine integration."""
from __future__ import annotations

import argparse
from contextlib import closing
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import secrets
import sqlite3
import sys
import tempfile
import uuid
import webbrowser

VERSION = '0.1.0'
LIMIT = 64 * 1024
MAX_CREDITS = 9_000_000_000_000


class Problem(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message


def text(data, key, limit=120):
    value = data.get(key)
    if not isinstance(value, str) or not value.strip() or len(value) > limit or any(ord(c) < 32 for c in value):
        raise Problem(400, f'{key} must be nonempty text, at most {limit} characters.')
    return value.strip()


def integer(data, key, low, high):
    value = data.get(key)
    if type(value) is not int or not low <= value <= high:
        raise Problem(400, f'{key} must be an integer from {low} to {high}.')
    return value


class Store:
    def __init__(self, path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with closing(self.connect()) as db:
            version = db.execute('PRAGMA user_version').fetchone()[0]
            if version not in (0, 1):
                raise RuntimeError('Database schema is newer than this server. Keep the database and use the matching server.')
            db.execute('PRAGMA journal_mode=WAL')
            db.executescript('''
                BEGIN IMMEDIATE;
                CREATE TABLE IF NOT EXISTS profile (
                    id INTEGER PRIMARY KEY CHECK(id=1), name TEXT NOT NULL,
                    credits INTEGER NOT NULL CHECK(credits BETWEEN 0 AND 9000000000000));
                INSERT OR IGNORE INTO profile VALUES(1, 'Offline Pilot', 0);
                CREATE TABLE IF NOT EXISTS locations(id TEXT PRIMARY KEY, name TEXT NOT NULL);
                INSERT OR IGNORE INTO locations VALUES('offline-home', 'Offline home (unmapped)');
                CREATE TABLE IF NOT EXISTS hangars(
                    id TEXT PRIMARY KEY, name TEXT NOT NULL,
                    location_id TEXT NOT NULL REFERENCES locations(id));
                CREATE TABLE IF NOT EXISTS ships(
                    id TEXT PRIMARY KEY, class_name TEXT NOT NULL, name TEXT NOT NULL,
                    state TEXT NOT NULL CHECK(state IN ('stored','deployed')),
                    location_id TEXT REFERENCES locations(id),
                    hangar_id TEXT UNIQUE REFERENCES hangars(id), version INTEGER NOT NULL DEFAULT 1,
                    CHECK((state='stored' AND location_id IS NOT NULL) OR
                          (state='deployed' AND location_id IS NULL AND hangar_id IS NULL)));
                CREATE TABLE IF NOT EXISTS inventory(
                    item_class TEXT NOT NULL, location_id TEXT NOT NULL REFERENCES locations(id),
                    quantity INTEGER NOT NULL CHECK(quantity BETWEEN 0 AND 1000000),
                    PRIMARY KEY(item_class, location_id));
                CREATE TABLE IF NOT EXISTS events(
                    sequence INTEGER PRIMARY KEY AUTOINCREMENT,
                    created_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')),
                    operation TEXT NOT NULL, details TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS requests(
                    key TEXT PRIMARY KEY, digest TEXT NOT NULL, response TEXT NOT NULL);
                PRAGMA user_version=1;
                COMMIT;
            ''')

    def connect(self):
        db = sqlite3.connect(self.path, timeout=10, isolation_level=None)
        db.row_factory = sqlite3.Row
        db.execute('PRAGMA foreign_keys=ON')
        return db

    @staticmethod
    def need(db, table, identifier):
        # Only caller-owned constant table names, never request-supplied SQL.
        row = db.execute(f'SELECT * FROM {table} WHERE id=?', (identifier,)).fetchone()
        if row is None:
            raise Problem(404, f'{table}: record not found.')
        return dict(row)

    def snapshot(self):
        with closing(self.connect()) as db:
            db.execute('BEGIN')
            result = {'server_version': VERSION, 'schema_version': 1, 'game_connected': False}
            result['profile'] = dict(db.execute('SELECT name, credits FROM profile WHERE id=1').fetchone())
            for table in ('locations', 'hangars', 'ships', 'inventory'):
                result[table] = [dict(r) for r in db.execute(f'SELECT * FROM {table} ORDER BY rowid')]
            result['events'] = [dict(r) for r in db.execute('SELECT * FROM events ORDER BY sequence DESC LIMIT 100')]
            db.execute('COMMIT')
            return result

    def backup(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'offline.sqlite3'
            with closing(self.connect()) as src, closing(sqlite3.connect(path)) as dst:
                src.backup(dst)
            return path.read_bytes()

    def mutate(self, operation, data, key):
        if not isinstance(data, dict):
            raise Problem(400, 'JSON body must be an object.')
        if not isinstance(key, str) or not 1 <= len(key) <= 100 or not key.isascii() or any(ord(c) < 33 for c in key):
            raise Problem(400, 'Provide an Idempotency-Key header (1-100 printable ASCII characters).')
        digest = hashlib.sha256((operation + '\n' + json.dumps(data, sort_keys=True, separators=(',', ':'), allow_nan=False)).encode()).hexdigest()
        with closing(self.connect()) as db:
            db.execute('BEGIN IMMEDIATE')
            try:
                previous = db.execute('SELECT digest,response FROM requests WHERE key=?', (key,)).fetchone()
                if previous:
                    if previous['digest'] != digest:
                        raise Problem(409, 'Idempotency key was already used with a different request.')
                    db.execute('ROLLBACK')
                    return json.loads(previous['response'])
                result = self.apply(db, operation, data)
                db.execute('INSERT INTO events(operation,details) VALUES(?,?)', (operation, json.dumps(result)))
                db.execute('INSERT INTO requests VALUES(?,?,?)', (key, digest, json.dumps(result)))
                db.execute('COMMIT')
                return result
            except Exception:
                if db.in_transaction:
                    db.execute('ROLLBACK')
                raise

    def apply(self, db, op, data):
        if op == '/profile':
            name = text(data, 'name', 80)
            db.execute('UPDATE profile SET name=? WHERE id=1', (name,))
            return {'name': name}
        if op == '/wallet/adjust':
            delta = integer(data, 'delta', -MAX_CREDITS, MAX_CREDITS)
            reason = text(data, 'reason', 200)
            balance = db.execute('SELECT credits FROM profile WHERE id=1').fetchone()[0] + delta
            if not 0 <= balance <= MAX_CREDITS:
                raise Problem(409, 'Insufficient credits or balance limit exceeded.')
            db.execute('UPDATE profile SET credits=? WHERE id=1', (balance,))
            return {'credits': balance, 'delta': delta, 'reason': reason}
        if op == '/locations':
            identifier, name = text(data, 'id'), text(data, 'name')
            db.execute('INSERT INTO locations VALUES(?,?)', (identifier, name))
            return {'id': identifier, 'name': name}
        if op == '/hangars':
            loc = text(data, 'location_id')
            self.need(db, 'locations', loc)
            identifier, name = str(uuid.uuid4()), text(data, 'name')
            db.execute('INSERT INTO hangars VALUES(?,?,?)', (identifier, name, loc))
            return {'id': identifier, 'name': name, 'location_id': loc}
        if op == '/ships':
            loc = text(data, 'location_id')
            self.need(db, 'locations', loc)
            identifier = str(uuid.uuid4())
            db.execute('INSERT INTO ships(id,class_name,name,state,location_id) VALUES(?,?,?,\'stored\',?)',
                       (identifier, text(data, 'class_name', 200), text(data, 'name'), loc))
            return self.need(db, 'ships', identifier)
        parts = op.strip('/').split('/')
        if len(parts) == 3 and parts[0] == 'ships' and parts[2] == 'transition':
            ship = self.need(db, 'ships', parts[1])
            version = integer(data, 'expected_version', 1, 2**53-1)
            if version != ship['version']:
                raise Problem(409, 'Ship changed since this request was prepared. Refresh and retry.')
            action = text(data, 'action')
            if action == 'deploy' and ship['state'] == 'stored':
                db.execute("UPDATE ships SET state='deployed',location_id=NULL,hangar_id=NULL,version=version+1 WHERE id=?", (ship['id'],))
            elif action == 'store' and ship['state'] == 'deployed':
                hangar = self.need(db, 'hangars', text(data, 'hangar_id'))
                if db.execute('SELECT 1 FROM ships WHERE hangar_id=?', (hangar['id'],)).fetchone():
                    raise Problem(409, 'Hangar already has a stored ship.')
                db.execute("UPDATE ships SET state='stored',location_id=?,hangar_id=?,version=version+1 WHERE id=?",
                           (hangar['location_id'], hangar['id'], ship['id']))
            else:
                raise Problem(409, 'Allowed transitions: stored -> deploy, deployed -> store.')
            return self.need(db, 'ships', ship['id'])
        if op == '/inventory/adjust':
            item, loc = text(data, 'item_class', 200), text(data, 'location_id')
            self.need(db, 'locations', loc)
            delta = integer(data, 'delta', -1_000_000, 1_000_000)
            old = db.execute('SELECT quantity FROM inventory WHERE item_class=? AND location_id=?', (item, loc)).fetchone()
            quantity = (old[0] if old else 0) + delta
            if not 0 <= quantity <= 1_000_000:
                raise Problem(409, 'Insufficient items or quantity limit exceeded.')
            db.execute('INSERT INTO inventory VALUES(?,?,?) ON CONFLICT(item_class,location_id) DO UPDATE SET quantity=excluded.quantity', (item, loc, quantity))
            return {'item_class': item, 'location_id': loc, 'quantity': quantity}
        if op == '/demo':
            if db.execute('SELECT count(*) FROM events').fetchone()[0]:
                raise Problem(409, 'Demo setup is available only before your first edit. Existing data was kept.')
            db.execute("UPDATE profile SET credits=100000 WHERE id=1")
            db.execute("INSERT INTO hangars VALUES('demo-hangar','Demo hangar','offline-home')")
            db.execute("INSERT INTO ships VALUES('demo-gladius','AEGS_Gladius','Demo Gladius','stored','offline-home','demo-hangar',1)")
            db.execute("INSERT INTO inventory VALUES('demo_supply_crate','offline-home',5)")
            return {'demo': True, 'note': 'Sample server records only. No in-game assets created.'}
        raise Problem(404, 'Unknown API operation.')


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def __init__(self, address, store, token, page):
        self.store, self.token, self.page = store, token, page
        super().__init__(address, Handler)


class Handler(BaseHTTPRequestHandler):
    server_version = 'SC-Local/' + VERSION

    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def log_message(self, fmt, *args):
        # Do not log request URLs, credentials, or state payloads.
        pass

    def send(self, status, data, kind='application/json; charset=utf-8', extra=None):
        payload = json.dumps(data, allow_nan=False).encode() if kind.startswith('application/json') else data
        self.send_response(status)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(payload)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('X-Frame-Options', 'DENY')
        self.send_header('Referrer-Policy', 'no-referrer')
        self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(payload)

    def guard(self, authenticate=True):
        port = self.server.server_address[1]
        hosts = {f'127.0.0.1:{port}', f'localhost:{port}'}
        if self.headers.get('Host') not in hosts:
            raise Problem(403, 'Use the local server address shown in its window.')
        origin = self.headers.get('Origin')
        if origin is not None and origin not in {f'http://{host}' for host in hosts}:
            raise Problem(403, 'Cross-origin requests are not allowed.')
        if authenticate:
            supplied = self.headers.get('Authorization', '')
            if not secrets.compare_digest(supplied.encode(), ('Bearer ' + self.server.token).encode()):
                raise Problem(401, 'Local API token required.')

    def do_GET(self):
        self.handle_request(False)

    def do_POST(self):
        self.handle_request(True)

    def handle_request(self, write):
        try:
            public = not write and self.path in ('/', '/health')
            self.guard(not public)
            if not write:
                if self.path == '/':
                    page = self.server.page.replace('__LOCAL_TOKEN__', self.server.token)
                    return self.send(200, page.encode(), 'text/html; charset=utf-8')
                if self.path == '/health':
                    with closing(self.server.store.connect()) as db:
                        db.execute('SELECT 1 FROM profile').fetchone()
                    return self.send(200, {'status': 'ok', 'version': VERSION, 'api_version': 1, 'game_connected': False})
                if self.path == '/api/v1/state':
                    return self.send(200, self.server.store.snapshot())
                if self.path == '/api/v1/backup':
                    return self.send(200, self.server.store.backup(), 'application/octet-stream',
                                     {'Content-Disposition': 'attachment; filename="offline-backup.sqlite3"'})
                raise Problem(404, 'Not found.')
            if not self.path.startswith('/api/v1/'):
                raise Problem(404, 'Not found.')
            if self.headers.get('Transfer-Encoding'):
                raise Problem(400, 'Chunked requests are not supported.')
            if self.headers.get('Content-Type', '').split(';')[0].strip().lower() != 'application/json':
                raise Problem(415, 'Use Content-Type: application/json.')
            try:
                size = int(self.headers.get('Content-Length', ''))
            except ValueError:
                raise Problem(411, 'Content-Length required.')
            if size < 0 or size > LIMIT:
                raise Problem(413, 'Request body exceeds 64 KiB.')
            try:
                raw = self.rfile.read(size)
                if len(raw) != size:
                    raise ValueError('Incomplete body')
                def reject_constant(value):
                    raise ValueError('Non-finite JSON numbers are not allowed')
                data = json.loads(raw, parse_constant=reject_constant)
            except (ValueError, UnicodeError):
                raise Problem(400, 'Invalid JSON body.')
            result = self.server.store.mutate(self.path[len('/api/v1'):], data, self.headers.get('Idempotency-Key'))
            self.send(200, result)
        except Problem as exc:
            self.close_connection = True
            self.send(exc.status, {'error': exc.message})
        except sqlite3.IntegrityError:
            self.send(409, {'error': 'A record already exists or conflicts with the requested state.'})
        except sqlite3.OperationalError:
            logging.exception('Database operation failed')
            self.send(503, {'error': 'Database unavailable or busy. Retry the same request key.'})
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            self.close_connection = True
        except Exception:
            logging.exception('Request failed')
            self.send(500, {'error': 'Internal error. See server.log.'})


def main():
    root = Path(sys.executable).resolve().parent if getattr(sys, 'frozen', False) else Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description='Local offline state service; no game connection yet.')
    parser.add_argument('--port', type=int, default=18870)
    parser.add_argument('--data-dir', type=Path, default=root / 'data')
    parser.add_argument('--open', action='store_true', help='Open control page in your browser')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error('port must be between 1 and 65535')
    args.data_dir.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(level=logging.INFO, handlers=[RotatingFileHandler(args.data_dir / 'server.log', maxBytes=1_000_000, backupCount=2)])
    token_path = args.data_dir / 'api-token.txt'
    try:
        with token_path.open('x', encoding='utf-8') as f:
            token = secrets.token_hex(32)
            f.write(token)
        token_path.chmod(0o600)
    except FileExistsError:
        token = token_path.read_text(encoding='utf-8').strip()
    if len(token) != 64 or any(c not in '0123456789abcdef' for c in token):
        raise RuntimeError('api-token.txt is invalid. Close the server and remove only that token file to regenerate it.')
    page = (Path(__file__).resolve().parent / 'dashboard.html').read_text(encoding='utf-8')
    # Bind before opening SQLite: a second launch must not modify the active database.
    server = Server(('127.0.0.1', args.port), None, token, page)
    try:
        server.store = Store(args.data_dir / 'offline.sqlite3')
        url = f'http://127.0.0.1:{args.port}'
        print(f'SC Local Server {VERSION}\nControl page: {url}\nSaved data: {args.data_dir.resolve()}\nGame bridge: not connected\nKeep this window open. Ctrl+C stops the server.', flush=True)
        if args.open:
            webbrowser.open(url)
        server.serve_forever()
    except KeyboardInterrupt:
        print('\nServer stopped. Your data is saved.')
    finally:
        server.server_close()


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print(f'Cannot start server: {exc}', file=sys.stderr)
        sys.exit(1)
