#!/usr/bin/env python3
"""Host-side format checks for the CharisOS CEX executable wrapper."""
from __future__ import annotations

import struct
import mkcex

def main() -> int:
    elf = bytearray(64)
    elf[:16] = b"\x7fELF" + bytes((2, 1, 1)) + bytes(9)
    struct.pack_into("<HH", elf, 16, 2, 62)

    wrapped = mkcex.package(bytes(elf))
    magic, version, machine, header_size, flags, payload_size, checksum, reserved = (
        mkcex.HEADER.unpack_from(wrapped)
    )
    assert magic == b"CEX1"
    assert version == 1
    assert machine == 62
    assert header_size == mkcex.HEADER.size
    assert flags == 0 and reserved == 0
    assert payload_size == len(elf)
    assert checksum == mkcex.fnv1a(bytes(elf))
    assert wrapped[header_size:] == bytes(elf)

    damaged = bytearray(wrapped)
    damaged[-1] ^= 1
    assert mkcex.fnv1a(bytes(damaged[header_size:])) != checksum

    try:
        mkcex.package(b"not an executable")
    except ValueError:
        pass
    else:
        raise AssertionError("invalid input was accepted")

    print("CEX format tests passed")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
