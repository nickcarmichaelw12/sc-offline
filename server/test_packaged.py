"""Windows CI smoke test of the actual packaged executable, including restart."""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

def main():
    exe = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as tmp:
        data = Path(tmp) / 'saved-data'
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        base = f'http://127.0.0.1:{port}'
        for run in range(2):
            process = subprocess.Popen([exe, '--data-dir', str(data), '--port', str(port)], cwd=tmp)
            try:
                for attempt in range(100):
                    if process.poll() is not None:
                        raise RuntimeError('Packaged executable exited during startup')
                    try:
                        with urllib.request.urlopen(base+'/health', timeout=1) as response:
                            assert json.load(response)['status'] == 'ok'
                        break
                    except OSError:
                        time.sleep(.2)
                else:
                    raise RuntimeError('Packaged executable did not become ready')
                token = (data / 'api-token.txt').read_text().strip()
                with urllib.request.urlopen(base+'/', timeout=3) as response:
                    page = response.read()
                    assert b'SC Local Server' in page and b'__LOCAL_TOKEN__' not in page
                if run == 0:
                    request = urllib.request.Request(base+'/api/v1/demo', b'{}',
                        headers={'Authorization': 'Bearer '+token, 'Content-Type': 'application/json', 'Idempotency-Key': 'packaged-demo'})
                    with urllib.request.urlopen(request, timeout=3) as response:
                        assert json.load(response)['demo']
                request = urllib.request.Request(base+'/api/v1/state', headers={'Authorization': 'Bearer '+token})
                with urllib.request.urlopen(request, timeout=3) as response:
                    state = json.load(response)
                    assert state['profile']['credits'] == 100000
                    assert len(state['ships']) == 1
                    assert len(state['events']) == 1
            finally:
                # PyInstaller one-file uses a bootloader parent and a child process.
                # Stop both on Windows so restart genuinely exercises persisted data.
                if sys.platform == 'win32':
                    subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], check=True, capture_output=True)
                else:
                    process.terminate()
                process.wait(timeout=10)
    print('Packaged executable passed HTTP, embedded dashboard, SQLite write, and process-restart persistence checks.')


if __name__ == "__main__":
    main()
