#!/usr/bin/env python3
"""Read-only CL12660092 ASOP/hangar contract checks; never executes game code.

Usage: python3 tools/test_native_hangar_contract.py /path/to/StarCitizen.exe
No game file, extracted code, account identifier or log is written or uploaded.
These are binary compatibility checks, not evidence of working world services.
"""
import bisect
import hashlib
from pathlib import Path
import re
import struct
import sys


class Image:
    def __init__(self, data):
        self.data = data
        pe = self.unpack('<I', 60)[0]
        if data[pe:pe+4] != b'PE\0\0' or self.unpack('<H', pe+4)[0] != 0x8664:
            raise ValueError('Expected an x64 PE image')
        if self.unpack('<H', pe+24)[0] != 0x20B:
            raise ValueError('Expected PE32+')
        self.base = self.unpack('<Q', pe+48)[0]
        opt = self.unpack('<H', pe+20)[0]
        self.sections = []
        for i in range(self.unpack('<H', pe+6)[0]):
            p = pe+24+opt+i*40
            _, rva, size, offset = self.unpack('<IIII', p+8)
            if offset+size > len(data):
                raise ValueError('Section extends past the file')
            self.sections.append((rva, size, offset))
        exception, length = self.unpack('<II', pe+24+112+3*8)
        records = self.read(self.base+exception, length)
        self.functions = [f for f in struct.iter_unpack('<III', records) if f[0]]
        self.starts = [f[0] for f in self.functions]
        if self.starts != sorted(set(self.starts)):
            raise ValueError('Unsorted or duplicate exception entries')

    def unpack(self, fmt, offset):
        if offset < 0 or offset+struct.calcsize(fmt) > len(self.data):
            raise ValueError('Truncated PE structure')
        return struct.unpack_from(fmt, self.data, offset)

    def read(self, address, size):
        rva = address-self.base
        for start, length, offset in self.sections:
            if size >= 0 and start <= rva and rva+size <= start+length:
                return self.data[offset+rva-start:offset+rva-start+size]
        raise ValueError(f'Address is not backed by file data: {address:#x}')

    def relative(self, address, prefix):
        prefix = bytes.fromhex(prefix)
        raw = self.read(address, len(prefix)+4)
        assert raw[:len(prefix)] == prefix, f'Unexpected instruction at {address:#x}'
        return address+len(raw)+struct.unpack_from('<i', raw, len(prefix))[0]

    def root(self, address):
        rva = address-self.base
        i = bisect.bisect_right(self.starts, rva)-1
        if i < 0 or not self.functions[i][0] <= rva < self.functions[i][1]:
            raise ValueError('Address has no runtime function entry')
        entry = self.functions[i]
        seen = set()
        for _ in range(16):
            if entry in seen:
                raise ValueError('Cyclic chained unwind information')
            seen.add(entry)
            header = self.read(self.base+entry[2], 4)
            if not (header[0] >> 3) & 4:
                return self.base+entry[0]
            tail = self.base+entry[2]+4+((header[2]+1) & ~1)*2
            entry = struct.unpack('<III', self.read(tail, 12))
        raise ValueError('Excessive chained unwind depth')


def verify(img):
    assert img.base == 0x140000000, 'Unexpected preferred image base'
    # The public RequestHangarInstance continuation is a logging observer.
    assert img.relative(0x1451E11D5, 'E8') == 0x143976F70
    assert img.relative(0x1451E1218, '48 8D 05') == 0x14509A9E0
    assert img.read(0x14509A9E0, 4) == bytes.fromhex('48 8B 49 10')
    assert img.relative(0x14509A9E4, 'E9') == 0x145043D80
    assert hashlib.sha256(img.read(0x145043D80, 0x86D)).hexdigest() == (
        '131a891e30502d56549a7363523c743a133e947b9f9580509b6b9d3c5c4468ee')
    # FindHangarsAtLocation service and its real continuation chain.
    assert img.read(0x143A3BBA1, 12) == bytes.fromhex(
        '48 8B 08 48 8B 51 50 48 8B C8 FF D2')
    assert img.read(0x143A3BC0A, 3) == bytes.fromhex('FF 50 10')
    assert img.relative(0x143A3BC64, 'E8') == 0x1438C6CE0
    assert img.relative(0x1438C6D8B, '48 8D 05') == 0x14397D5D0
    assert img.relative(0x14397D5EC, 'E8') == 0x14395D480
    # Do not mistake the first .pdata interval for the whole function. This
    # continuation spans eight chained intervals, including the world setup.
    starts = (0x14395D480, 0x14395D563, 0x14395E524, 0x14395ED00,
              0x14395F224, 0x1439605C4, 0x1439605F2, 0x14396167B)
    assert all(img.root(a) == starts[0] for a in starts)
    assert img.root(0x143961894) == starts[0]
    # The source hook's registration chain and 11-byte stolen prologue.
    source = (Path(__file__).resolve().parents[1]/'src/hooks.cpp').read_text()
    pattern = re.search(r'"fleet manager deliver -> reserved spaceport ATC",\s*'
                        r'"([A-F0-9 ]+)",\s*11,', source)
    assert pattern, 'Deliver hook source contract changed; review the ABI'
    signature = bytes.fromhex(pattern[1])
    assert img.data.count(signature) == 1
    assert img.read(0x144C09C60, len(signature)) == signature
    assert img.relative(0x144C3DB99, '48 8D 05') == 0x144C11D40
    assert img.relative(0x144C11D40, 'E9') == 0x144C09C60


if __name__ == '__main__':
    verify(Image(Path(sys.argv[1]).read_bytes()))
    print('PASS: ASOP registration, hangar request/log observer, service lookup, '
          'real continuation and eight chained function intervals. '
          'Static compatibility only; no world completion tested.')
