import concurrent.futures
from contextlib import closing
import http.client
import json
from pathlib import Path
import sqlite3
import tempfile
import threading
import unittest

from server import Store, Server, Problem


class StateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / 'offline.sqlite3'
        self.store = Store(self.path)
        self.key = 0

    def tearDown(self):
        self.temp.cleanup()

    def change(self, path, body):
        self.key += 1
        return self.store.mutate(path, body, str(self.key))

    def test_restart_preserves_every_kind_of_record(self):
        self.change('/demo', {})
        self.change('/profile', {'name': 'Local Pilot'})
        self.change('/wallet/adjust', {'delta': -50, 'reason': 'purchase'})
        self.change('/inventory/adjust', {'item_class': 'demo_supply_crate', 'location_id': 'offline-home', 'delta': -2})
        self.assertEqual(self.store.snapshot(), Store(self.path).snapshot())
        snapshot = Store(self.path).snapshot()
        self.assertEqual(snapshot['profile']['credits'], 99950)
        self.assertEqual(snapshot['inventory'][0]['quantity'], 3)
        self.assertEqual(snapshot['ships'][0]['hangar_id'], 'demo-hangar')

    def test_exact_retry_credits_once_and_conflicting_retry_rejected(self):
        request = {'delta': 123, 'reason': 'test'}
        a = self.store.mutate('/wallet/adjust', request, 'retry')
        self.assertEqual(a, self.store.mutate('/wallet/adjust', request, 'retry'))
        with self.assertRaises(Problem) as err:
            self.store.mutate('/wallet/adjust', {**request, 'delta': 124}, 'retry')
        self.assertEqual(err.exception.status, 409)
        self.assertEqual(self.store.snapshot()['profile']['credits'], 123)
        self.assertEqual(len(self.store.snapshot()['events']), 1)

    def test_parallel_mutations_do_not_lose_updates(self):
        def change(i):
            return self.store.mutate('/wallet/adjust', {'delta': 1, 'reason': 'parallel'}, f'p{i}')
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            list(pool.map(change, range(40)))
        self.assertEqual(self.store.snapshot()['profile']['credits'], 40)

    def test_invalid_mutation_rolls_back(self):
        for path, body in [('/wallet/adjust', {'delta': -1, 'reason': 'no funds'}),
                           ('/inventory/adjust', {'item_class': 'x', 'location_id': 'offline-home', 'delta': -1}),
                           ('/ships', {'class_name': 'x', 'name': 'ship', 'location_id': 'missing'})]:
            before = self.store.snapshot()
            with self.assertRaises(Problem):
                self.change(path, body)
            self.assertEqual(self.store.snapshot(), before)

    def test_ship_versions_and_exclusive_hangar(self):
        self.change('/demo', {})
        one = self.change('/ships/demo-gladius/transition', {'action': 'deploy', 'expected_version': 1})
        self.assertIsNone(one['hangar_id'])
        self.assertIsNone(one['location_id'])
        with self.assertRaises(Problem):
            self.change('/ships/demo-gladius/transition', {'action': 'store', 'hangar_id': 'demo-hangar', 'expected_version': 1})
        second = self.change('/ships', {'class_name': 'AEGS_Gladius', 'name': 'Second', 'location_id': 'offline-home'})
        self.change('/ships/'+second['id']+'/transition', {'action': 'deploy', 'expected_version': 1})
        self.change('/ships/'+second['id']+'/transition', {'action': 'store', 'hangar_id': 'demo-hangar', 'expected_version': 2})
        with self.assertRaises(Problem):
            self.change('/ships/demo-gladius/transition', {'action': 'store', 'hangar_id': 'demo-hangar', 'expected_version': 2})
        ship = next(s for s in self.store.snapshot()['ships'] if s['id'] == 'demo-gladius')
        self.assertEqual(ship['state'], 'deployed')
        self.assertEqual(ship['version'], 2)

    def test_store_uses_hangar_location(self):
        self.change('/demo', {})
        self.change('/locations', {'id': 'second-place', 'name': 'Second place'})
        hangar = self.change('/hangars', {'name': 'Second hangar', 'location_id': 'second-place'})
        self.change('/ships/demo-gladius/transition', {'action': 'deploy', 'expected_version': 1})
        ship = self.change('/ships/demo-gladius/transition', {'action': 'store', 'expected_version': 2, 'hangar_id': hangar['id']})
        self.assertEqual(ship['location_id'], 'second-place')

    def test_demo_never_overwrites_edits(self):
        self.change('/profile', {'name': 'Keep me'})
        before = self.store.snapshot()
        with self.assertRaises(Problem):
            self.change('/demo', {})
        self.assertEqual(self.store.snapshot(), before)

    def test_backup_is_restorable_and_contains_idempotency_history(self):
        self.change('/demo', {})
        copy = Path(self.temp.name) / 'restored.sqlite3'
        copy.write_bytes(self.store.backup())
        restored = Store(copy)
        self.assertEqual(restored.snapshot(), self.store.snapshot())
        restored.mutate('/demo', {}, '1')
        self.assertEqual(restored.snapshot(), self.store.snapshot())

    def test_future_schema_is_rejected_without_rewrite(self):
        with closing(self.store.connect()) as db:
            db.execute('PRAGMA user_version=2')
        with self.assertRaises(RuntimeError):
            Store(self.path)
        with closing(self.store.connect()) as db:
            self.assertEqual(db.execute('PRAGMA user_version').fetchone()[0], 2)

    def test_strict_integer_and_limits(self):
        for value in [True, '1', 0.5, 9000000000001]:
            with self.assertRaises(Problem):
                self.change('/wallet/adjust', {'delta': value, 'reason': 'invalid'})
        self.assertEqual(self.store.snapshot()['events'], [])


class HttpTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.store = Store(Path(self.temp.name) / 'state.db')
        page = (Path(__file__).parent / 'dashboard.html').read_text(encoding='utf-8')
        self.server = Server(('127.0.0.1', 0), self.store, 'a'*64, page)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.port = self.server.server_address[1]

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.temp.cleanup()

    def request(self, path, method='GET', body=None, headers=None):
        connection = http.client.HTTPConnection('127.0.0.1', self.port, timeout=3)
        base = {'Authorization': 'Bearer '+'a'*64, 'Content-Type': 'application/json', 'Idempotency-Key': 'http-test'}
        base.update(headers or {})
        connection.request(method, path, body, base)
        response = connection.getresponse()
        result = response.status, response.read(), dict(response.getheaders())
        connection.close()
        return result

    def test_page_health_and_authenticated_state(self):
        status, page, headers = self.request('/')
        self.assertEqual(status, 200)
        self.assertNotIn(b'__LOCAL_TOKEN__', page)
        self.assertIn(b'a'*64, page)
        self.assertIn('no-store', headers['Cache-Control'])
        self.assertFalse(json.loads(self.request('/health')[1])['game_connected'])
        self.assertEqual(self.request('/api/v1/state')[0], 200)
        self.assertEqual(self.request('/api/v1/state', headers={'Authorization': ''})[0], 401)

    def test_origin_host_and_token_protection(self):
        for headers in [{'Host': 'evil.example'}, {'Origin': 'https://evil.example'}, {'Authorization': 'Bearer wrong'}]:
            self.assertIn(self.request('/api/v1/demo', 'POST', '{}', headers)[0], (401, 403))
        self.assertEqual(self.store.snapshot()['events'], [])

    def test_json_validation_and_unknown_route(self):
        for body in ['{bad', '[]', '{"delta":NaN,"reason":"x"}']:
            self.assertEqual(self.request('/api/v1/wallet/adjust', 'POST', body)[0], 400)
        self.assertEqual(self.request('/api/v1/demo', 'POST', '{}', {'Idempotency-Key': ''})[0], 400)
        self.assertEqual(self.request('/api/v1/demo', 'POST', '{}', {'Content-Type': 'text/plain'})[0], 415)
        self.assertEqual(self.request('/api/v1/missing')[0], 404)
        self.assertEqual(self.request('/api/v1/demo', 'POST', 'x'*65537)[0], 413)

    def test_http_write_retry_backup(self):
        self.assertEqual(self.request('/api/v1/demo', 'POST', '{}')[0], 200)
        self.assertEqual(self.request('/api/v1/demo', 'POST', '{}')[0], 200)
        self.assertEqual(len(self.store.snapshot()['events']), 1)
        status, body, _ = self.request('/api/v1/backup')
        self.assertEqual(status, 200)
        self.assertTrue(body.startswith(b'SQLite format 3'))


if __name__ == '__main__':
    unittest.main()
