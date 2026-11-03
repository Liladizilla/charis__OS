#!/usr/bin/env python3
"""Package a validated x86-64 ELF executable as a CharisOS CEX container."""
from __future__ import annotations

import argparse
import os
import struct
import sys
import tempfile
from pathlib import Path

HEADER = struct.Struct("<4sHHIIQII")
MAGIC = b"CEX1"
VERSION = 1
MACHINE_X86_64 = 62
FNV_OFFSET = 2166136261
FNV_PRIME = 16777619


def fnv1a(data: bytes) -> int:
    value = FNV_OFFSET
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & 0xFFFFFFFF
    return value


def validate_elf64_x86_64(payload: bytes) -> None:
    if len(payload) < 64:
        raise ValueError("input is shorter than an ELF64 header")
    ident = payload[:16]
    if ident[:4] != b"\x7fELF" or ident[4] != 2 or ident[5] != 1 or ident[6] != 1:
        raise ValueError("input is not a little-endian ELF64 executable")
    elf_type, machine = struct.unpack_from("<HH", payload, 16)
    if elf_type != 2 or machine != MACHINE_X86_64:
        raise ValueError("input must be an x86-64 ET_EXEC image")


def package(payload: bytes) -> bytes:
    validate_elf64_x86_64(payload)
    header = HEADER.pack(
        MAGIC, VERSION, MACHINE_X86_64, HEADER.size, 0,
        len(payload), fnv1a(payload), 0,
    )
    return header + payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="ELF64 x86-64 ET_EXEC input")
    parser.add_argument("output", type=Path, help="output .CEX file")
    parser.add_argument("--force", action="store_true", help="replace an existing output")
    args = parser.parse_args()

    try:
        if args.output.exists() and not args.force:
            raise FileExistsError(f"{args.output} already exists (pass --force to replace it)")
        payload = args.input.read_bytes()
        result = package(payload)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        fd, temp_name = tempfile.mkstemp(prefix=args.output.name + ".", dir=args.output.parent)
        try:
            with os.fdopen(fd, "wb") as output:
                output.write(result)
                output.flush()
                os.fsync(output.fileno())
            os.replace(temp_name, args.output)
        except Exception:
            try:
                os.unlink(temp_name)
            except OSError:
                pass
            raise
        print(f"Created {args.output}: {len(payload)}-byte ELF payload, FNV-1a {fnv1a(payload):08x}")
        return 0
    except (OSError, ValueError) as exc:
        print(f"mkcex: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
