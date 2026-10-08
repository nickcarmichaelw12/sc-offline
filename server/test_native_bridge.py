"""Invoke the actual Windows bridge transport with simulated game acknowledgement."""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

def main():
    exe=str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as tmp:
        root=Path(tmp)
        with socket.socket() as sock:
            sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        process=subprocess.Popen([sys.executable,str(Path(__file__).with_name('server.py')),'--data-dir',tmp,'--port',str(port)])
        try:
            url=f'http://127.0.0.1:{port}'
            for _ in range(100):
                try:
                    with urllib.request.urlopen(url+'/health',timeout=1) as r: assert json.load(r)['status']=='ok'
                    break
                except OSError: time.sleep(.1)
            else: raise RuntimeError('Test server startup timeout')
            token=(root/'api-token.txt').read_text().strip()
            (root/'bridge.ini').write_text(str(port)+'\n'+token+'\n',encoding='ascii')
            req=urllib.request.Request(url+'/api/v1/demo',b'{}',headers={'Authorization':'Bearer '+token,'Content-Type':'application/json','Idempotency-Key':'seed'})
            with urllib.request.urlopen(req,timeout=3) as r: assert json.load(r)['demo']
            subprocess.run([exe,tmp],check=True,timeout=40)
            req=urllib.request.Request(url+'/api/v1/state',headers={'Authorization':'Bearer '+token})
            with urllib.request.urlopen(req,timeout=3) as r: state=json.load(r)
            assert state['ships'][0]['state']=='deployed'
            assert state['bridge_operations'][0]['status']=='confirmed'
            assert state['bridge_operations'][0]['entity_id']=='12345'
        finally:
            process.terminate();process.wait(timeout=10)

if __name__=='__main__': main()
