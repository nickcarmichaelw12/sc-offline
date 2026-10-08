"""Synthetic PE tests for the hangar checker's addressing/unwind safeguards."""
import struct
import unittest
from test_native_hangar_contract import Image


def fixture():
    data = bytearray(0x600)
    struct.pack_into('<I', data, 60, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', data, 0x84, 0x8664, 1)
    struct.pack_into('<H', data, 0x94, 0xF0)
    struct.pack_into('<H', data, 0x98, 0x20B)
    struct.pack_into('<Q', data, 0xB0, 0x140000000)
    struct.pack_into('<II', data, 0x98+112+24, 0x1100, 24)
    struct.pack_into('<IIII', data, 0x188+8, 0x400, 0x1000, 0x400, 0x200)
    struct.pack_into('<III', data, 0x300, 0x1000, 0x1020, 0x1140)
    struct.pack_into('<III', data, 0x30C, 0x1040, 0x1060, 0x1160)
    data[0x340] = 1  # Version 1, no chaining.
    data[0x360] = 1 | (4 << 3)
    data[0x362] = 1  # Odd unwind-code count needs padding before the chain.
    struct.pack_into('<III', data, 0x368, 0x1000, 0x1020, 0x1140)
    data[0x200] = 0xE8
    struct.pack_into('<i', data, 0x201, 0x3B)
    return data


class ParserTests(unittest.TestCase):
    def test_relative_address_and_chained_root(self):
        img = Image(fixture())
        self.assertEqual(img.relative(0x140001000, 'E8'), 0x140001040)
        self.assertEqual(img.root(0x14000105F), 0x140001000)
        self.assertEqual(img.root(0x140001000), 0x140001000)

    def test_unmapped_address_and_function_gap(self):
        img = Image(fixture())
        for address in (0x140001020, 0x140001060, 0x140000FFF):
            with self.assertRaises(ValueError):
                img.root(address)
        with self.assertRaises(ValueError):
            img.read(0x1400013FF, 2)

    def test_truncated_headers_and_sections(self):
        for length in (0, 64, 0x90, 0x5FF):
            with self.assertRaises(ValueError):
                Image(fixture()[:length])

    def test_wrong_instruction(self):
        with self.assertRaises(AssertionError):
            Image(fixture()).relative(0x140001000, 'E9')

    def test_cycle(self):
        data = fixture()
        struct.pack_into('<III', data, 0x368, 0x1040, 0x1060, 0x1160)
        with self.assertRaises(ValueError):
            Image(data).root(0x140001040)

    def test_unsorted_entries(self):
        data = fixture()
        data[0x300:0x318] = data[0x30C:0x318]+data[0x300:0x30C]
        with self.assertRaises(ValueError):
            Image(data)


if __name__ == '__main__':
    unittest.main()
