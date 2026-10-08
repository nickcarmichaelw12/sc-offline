#!/usr/bin/env python3
"""Linux x86-64 regression using locally supplied CL12660092 code; no game upload.

Usage: python3 tools/test_native_urn.py /path/to/StarCitizen.exe
Calls only the checked URN validation/equality/copy paths with POD UUID payloads.
This does not launch the game, contact services, or test UI/hangar behavior.
"""
import ctypes
import mmap
from pathlib import Path
import platform
import struct
import subprocess
import sys
import tempfile

assert platform.system() == 'Linux' and platform.machine() == 'x86_64'
root = Path(__file__).resolve().parents[1]
b = Path(sys.argv[1]).read_bytes()
pe = struct.unpack_from('<I', b, 60)[0]
assert b[pe:pe+4] == b'PE\0\0'
assert struct.unpack_from('<H', b, pe+4)[0] == 0x8664
opt = struct.unpack_from('<H', b, pe+20)[0]
sections = []
for i in range(struct.unpack_from('<H', b, pe+6)[0]):
    p = pe+24+opt+i*40
    _, va, size, off = struct.unpack_from('<IIII', b, p+8)
    sections.append((b[p:p+8].rstrip(b'\0'), va, size, off))
_, va, size, off = next(s for s in sections if s[0] == b'.text')
assert off+size <= len(b)
code = mmap.mmap(-1, size, prot=mmap.PROT_READ | mmap.PROT_WRITE | mmap.PROT_EXEC)
code.write(b[off:off+size])
base = ctypes.addressof(ctypes.c_char.from_buffer(code))
wrappers = []

def native(address, expected, result=ctypes.c_bool):
    offset = address - 0x140000000 - va
    signature = bytes.fromhex(expected)
    assert code[offset:offset+len(signature)] == signature, hex(address)
    # SysV RDI/RSI -> Windows RCX/RDX; 32-byte shadow area + alignment.
    adapter = bytes.fromhex('48 89 F9 48 89 F2 48 83 EC 28 48 B8')
    adapter += struct.pack('<Q', base+offset)
    adapter += bytes.fromhex('FF D0 48 83 C4 28 C3')
    mem = mmap.mmap(-1, len(adapter), prot=mmap.PROT_READ | mmap.PROT_WRITE | mmap.PROT_EXEC)
    mem.write(adapter)
    wrappers.append(mem)
    return ctypes.CFUNCTYPE(result, ctypes.c_void_p, ctypes.c_void_p)(
        ctypes.addressof(ctypes.c_char.from_buffer(mem)))

valid = native(0x140441BA0, '80 39 11 74 0F 80 79 01 1E 74 09 80 79 08 05 74 03 B0 01 C3 32 C0 C3')
equal = native(0x14042C130, '48 83 EC 28 0F B6 01 4C 8B C2 38 02')
copy = native(0x14042B020, '4C 8B DC 49 89 5B 10 49 89 73 18 49 89 7B 20 41 56', ctypes.c_void_p)
with tempfile.TemporaryDirectory() as temp:
    exe = str(Path(temp)/'fixture')
    subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror',str(root/'tests/urn_fixture.cpp'),'-o',exe],check=True)
    data = subprocess.check_output([exe])
assert len(data) == 120
a, other, legacy = [ctypes.create_string_buffer(data[i:i+40],40) for i in range(0,120,40)]
assert not valid(legacy,None)
assert not equal(legacy,legacy), 'Legacy discriminator unexpectedly compares equal'
assert valid(a,None) and valid(other,None)
assert equal(a,a) and not equal(a,other)
first = ctypes.create_string_buffer(40)
second = ctypes.create_string_buffer(40)
copy(first,a)
copy(second,first)
assert valid(first,None) and valid(second,None)
assert equal(a,first) and equal(first,second) and not equal(other,second)
copy(first,other)  # Exercise reassignment of a UUID-bearing destination too.
assert equal(first,other) and not equal(first,a)
print('PASS: native rejection of legacy URN; new validity, identity, distinctness, repeated copy and reassignment')
