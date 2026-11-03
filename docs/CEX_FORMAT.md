# CharisOS CEX executable format

CEX is the native executable container used by CharisOS. Version 1 wraps a
little-endian x86-64 ELF64 executable and records the target architecture,
payload length and a checksum before the ELF payload.

## Version 1 header

All integer fields are little-endian. The packed header is exactly 32 bytes.

| Offset | Size | Field | Required value |
| ---: | ---: | --- | --- |
| 0 | 4 | Magic | ASCII `CEX1` |
| 4 | 2 | Version | `1` |
| 6 | 2 | Machine | `62` (ELF EM_X86_64) |
| 8 | 4 | Header size | `32` |
| 12 | 4 | Flags | `0` |
| 16 | 8 | Payload size | Exact number of bytes after the header |
| 24 | 4 | Payload checksum | FNV-1a 32-bit over the complete ELF payload |
| 28 | 4 | Reserved | `0` |

The payload must be a little-endian ELF64 `ET_EXEC` image for x86-64. The
kernel validates the container header and checksum before passing the payload
offset and bounded payload size to the ELF loader. Raw ELF files remain
supported for compatibility.

## Build a CEX file

Build a freestanding, statically linked x86-64 executable first, then package it:

```sh
python3 tools/mkcex.py program.elf program.cex
```

The packer refuses non-ELF64, non-little-endian, non-`ET_EXEC`, non-x86-64
inputs. It writes through a temporary file and atomically replaces the target
only when `--force` is supplied.

Run the format checks and the kernel smoke test with:

```sh
make cex-format-test
make elf-test
```

The FAT32 smoke-test image contains `TEST.CEX`; the ring-3 bootstrap loads it
through the normal exec syscall. This verifies the current CEX-to-ELF loading
path, including argument and environment setup.

## Security properties and current limits

The checksum detects accidental corruption and malformed/truncated payloads.
FNV-1a is **not cryptographic authentication**. It does not prove who created
an executable and does not prevent an attacker from replacing both the file
and checksum. Publisher signatures, a cryptographic hash, key management,
package installation policy and rollback protection are not implemented.

Version 1 supports statically linked ELF64 `ET_EXEC` payloads. PIE, ELF
relocations, dynamic linking, shared libraries, executable signing, package
repositories, GUI installation and a general-purpose graphical executable
launcher are not complete. Do not treat CEX as a trusted package format yet.
