#!/usr/bin/env python3
"""Validate the boot emblem's compact 2-bit RLE payload."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "include/kernel/charis_logo_bitmap.h").read_text(encoding="utf-8")
width = int(re.search(r"#define CHARIS_LOGO_WIDTH (\d+)u", source).group(1))
height = int(re.search(r"#define CHARIS_LOGO_HEIGHT (\d+)u", source).group(1))
tokens = [int(value, 16) for value in re.findall(r"\\x([0-9A-Fa-f]{2})", source)]

assert width == 128 and height == 133
assert tokens, "empty emblem stream"
runs = [token >> 2 for token in tokens]
palette = {token & 3 for token in tokens}
assert all(1 <= run <= 63 for run in runs), "invalid or zero-length RLE token"
assert sum(runs) == width * height, "RLE stream does not cover exactly one image"
assert palette == {0, 1, 2, 3}, "transparent, silver, gold and shade palette entries required"

print(f"CharisOS emblem OK: {width}x{height}, {len(tokens)} RLE tokens")
