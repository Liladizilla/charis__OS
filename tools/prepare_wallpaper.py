#!/usr/bin/env python3
"""Prepare a pinned CC0 desktop photograph as a bounded CWP1 RGB565 module."""
from pathlib import Path
import struct
import sys
from PIL import Image

MAGIC = b"CWP1"
VERSION = 1
WIDTH = 2560
HEIGHT = 1440


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: prepare_wallpaper.py source.jpg output.cwp")
    source = Path(sys.argv[1])
    output = Path(sys.argv[2])

    with Image.open(source) as image:
        image = image.convert("RGB")
        src_w, src_h = image.size
        if src_w < 2 or src_h < 2 or src_w > 16384 or src_h > 16384:
            raise SystemExit(f"refusing implausible source dimensions: {src_w}x{src_h}")

        # Center-crop to 16:9 without stretching the original photograph.
        if src_w * HEIGHT > src_h * WIDTH:
            crop_w = src_h * WIDTH // HEIGHT
            left = (src_w - crop_w) // 2
            image = image.crop((left, 0, left + crop_w, src_h))
        else:
            crop_h = src_w * HEIGHT // WIDTH
            top = (src_h - crop_h) // 2
            image = image.crop((0, top, src_w, top + crop_h))

        image = image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
        pixels = bytearray()
        for red, green, blue in image.getdata():
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            pixels.extend(struct.pack("<H", value))

    stride = WIDTH * 2
    header = struct.pack("<4s5I", MAGIC, VERSION, WIDTH, HEIGHT, stride, len(pixels))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + pixels)
    print(f"CWP1: {WIDTH}x{HEIGHT}, RGB565, {len(pixels)} pixel bytes, {output.stat().st_size} total bytes")


if __name__ == "__main__":
    main()
