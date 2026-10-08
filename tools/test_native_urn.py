#!/usr/bin/env python3
"""Linux x86-64 regression using locally supplied CL12660092 code; no game upload.

Usage: python3 tools/test_native_urn.py /path/to/StarCitizen.exe
Calls checked URN paths and the native IsDeliverable lookup with POD UUIDs.
The provider test stubs thread identity and logging in its private code mapping;
it does not emulate engine services or change the supplied executable.
This does not launch the game, contact services, or test UI/hangar behavior.
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

# Test the actual provider lookup responsible for the missing-entitlement log.
# Hash the whole function before isolating its two environment dependencies.
# No lookup, comparison, state predicate, or return path is replaced.
lookup_address = 0x1456BEB60
lookup_offset = lookup_address - 0x140000000 - va
assert hashlib.sha256(code[lookup_offset:lookup_offset+0x1E4]).hexdigest() == (
    'f872b507789a2a0e3f325151f9af5c4393126cb26a336764700a103185be19f5')

def isolate_call(address, target, replacement):
    offset = address - 0x140000000 - va
    assert code[offset] == 0xE8
    assert address+5+struct.unpack_from('<i',code,offset+1)[0] == target
    start = target - 0x140000000 - va
    assert 0 <= start <= size-len(replacement)
    code[start:start+len(replacement)] = replacement

thread = ctypes.create_string_buffer(0x20)
struct.pack_into('<I', thread, 0x18, 7)
isolate_call(0x1456BEB8D, 0x1402DB340,
             b'\x48\xB8'+struct.pack('<Q',ctypes.addressof(thread))+b'\xC3')
isolate_call(0x1456BEC65, 0x140318790, bytes.fromhex('31 C0 C3'))
deliverable = native(lookup_address, '4C 8B DC 49 89 5B 10 49 89 6B 18')
provider = ctypes.create_string_buffer(0x90)
rows = ctypes.create_string_buffer(2*0x138)
rows_address = ctypes.addressof(rows)
# A pre-owned recursive lock avoids platform wait primitives in this isolated,
# single-thread test. The native function must balance its recursion depth.
struct.pack_into('<II', provider, 0x68, 1, 7)
struct.pack_into('<QQQ', provider, 0x78, rows_address,
                 rows_address+len(rows), rows_address+len(rows))
copy(rows_address+0xA8, other)
copy(rows_address+0x138+0xA8, a)
struct.pack_into('<I', rows, 0xA0, 1)

def check_lookup(urn, expected):
    before = provider.raw
    rows_before = rows.raw
    assert bool(deliverable(provider,urn)) == expected
    assert provider.raw == before, 'Provider or lock state changed'
    assert rows.raw == rows_before, 'Read-only lookup modified fleet rows'

for state in range(7):
    struct.pack_into('<I', rows, 0x138+0xA0, state)
    check_lookup(a, state == 1)
    check_lookup(other, True)
check_lookup(legacy, False)
# Reproduce the legacy defect even when both the query and row are identical.
ctypes.memmove(rows_address+0xA8, legacy, 40)
check_lookup(legacy, False)
# Exercise an empty fleet without letting the function enter game logging.
struct.pack_into('<Q', provider, 0x80, rows_address)
check_lookup(a, False)
print('PASS: native IsDeliverable finds copied UUID rows, distinguishes ships, '
      'honors state, rejects legacy identity and handles an empty fleet '
      '(thread identity and logging stubbed; UI/services not tested)')
