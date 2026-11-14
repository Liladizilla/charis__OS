#!/usr/bin/env python3
"""Host regression for the deterministic CWP1-to-CWP2 wallpaper packer."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PACKER = ROOT / "tools" / "pack_wallpapers.py"
CWP1 = struct.Struct("<4s5I")
CWP2 = struct.Struct("<4s4I")
ENTRY = struct.Struct("<6I")


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(PACKER), *args],
        check=False, text=True, capture_output=True,
    )


def write_cwp1(path: Path, colour: int) -> None:
    pixels = struct.pack("<4H", colour, colour, colour, colour)
    path.write_bytes(CWP1.pack(b"CWP1", 1, 2, 2, 4, len(pixels)) + pixels)


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="charisos-wallpaper-") as directory:
        root = Path(directory)
        alpine = root / "alpine.cwp"
        forest = root / "forest.cwp"
        packed = root / "wallpaper.cwp2"
        write_cwp1(alpine, 0xF800)
        write_cwp1(forest, 0x07E0)

        result = run(str(packed), f"5:{alpine}", f"6:{forest}")
        assert result.returncode == 0, result.stderr
        raw = packed.read_bytes()
        magic, version, count, entry_size, total_size = CWP2.unpack_from(raw)
        assert (magic, version, count, entry_size, total_size) == (
            b"CWP2", 2, 2, ENTRY.size, len(raw)
        )
        first = ENTRY.unpack_from(raw, CWP2.size)
        second = ENTRY.unpack_from(raw, CWP2.size + ENTRY.size)
        assert first == (5, 2, 2, 4, CWP2.size + 2 * ENTRY.size, 8)
        assert second == (6, 2, 2, 4, CWP2.size + 2 * ENTRY.size + 8, 8)
        assert raw[first[4]:first[4] + first[5]] == struct.pack("<4H", 0xF800, 0xF800, 0xF800, 0xF800)
        assert raw[second[4]:second[4] + second[5]] == struct.pack("<4H", 0x07E0, 0x07E0, 0x07E0, 0x07E0)

        duplicate = run(str(packed), f"5:{alpine}", f"5:{forest}")
        assert duplicate.returncode != 0, "duplicate IDs must be rejected"

        broken = root / "broken.cwp"
        broken.write_bytes(b"CWP1")
        invalid = run(str(packed), f"5:{broken}")
        assert invalid.returncode != 0, "truncated CWP1 inputs must be rejected"

    print("CWP2 wallpaper packer tests: PASS")


if __name__ == "__main__":
    main()
