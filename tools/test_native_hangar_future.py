#!/usr/bin/env python3
"""Execute isolated native ready-future binding/destruction, not world creation.

Uses a locally supplied CL12660092 executable. Only thread identity is stubbed;
result movement, callback dispatch and shared ownership run native code. No
game content is saved/uploaded. Requires Linux x86-64 with AVX and g++.
"""
import ctypes
import hashlib
import mmap
from pathlib import Path
import platform
import struct
import subprocess
import sys
import tempfile
from test_native_hangar_contract import Image


def verify(img):
    assert platform.system() == 'Linux' and platform.machine() == 'x86_64'
    # Check entire routines before executing code from the supplied image.
    expected = {
        0x1439fbca0: (0x50b, 'ef1d893090bb7795af3acf27862b020b99dc7c29ef64607437df4bf7d2c99eba'),
        0x14038f580: (0x89, '337ba570bd5f0e251fcab3f43b934f4abc50c25917eadb45db59e30988006739'),
    }
    for address, (length, digest) in expected.items():
        assert hashlib.sha256(img.read(address, length)).hexdigest() == digest
    text = next(s for s in img.sections if s[0] == 0x1000)
    rdata = next(s for s in img.sections if s[0] == 0x7e7b000)
    size = rdata[0]+rdata[1]
    with mmap.mmap(-1, size, prot=mmap.PROT_READ | mmap.PROT_WRITE | mmap.PROT_EXEC) as code:
        for rva, length, offset in (text, rdata):
            code[rva:rva+length] = img.data[offset:offset+length]
        address = ctypes.addressof(ctypes.c_char.from_buffer(code))
        thread = ctypes.create_string_buffer(0x20)
        struct.pack_into('<I', thread, 0x18, 7)
        assert img.relative(0x1439fbce2, 'E8') == 0x1402db340
        stub = b'\x48\xb8'+struct.pack('<Q', ctypes.addressof(thread))+b'\xc3'
        code[0x2db340:0x2db340+len(stub)] = stub
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp:
            output = str(Path(temp)/'fixture.so')
            subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-shared', '-fPIC', str(root/'tests/hangar_future_fixture.cpp'),
                            '-o', output], check=True)
            lib = ctypes.CDLL(output)
            lib.exercise.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
            lib.exercise.restype = None
            lib.exercise(address+0x39fbca0, address+0x38f580)
    print('PASS: 1000 native ready-empty callbacks, 1000 discarded futures, '
          'balanced destruction; thread identity isolated. No world/elevator test.')


if __name__ == '__main__':
    verify(Image(Path(sys.argv[1]).read_bytes()))
