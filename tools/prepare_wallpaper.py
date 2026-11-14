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
    if len(sys.argv) not in (3, 5):
        raise SystemExit(
            "usage: prepare_wallpaper.py source.jpg output.cwp [width height]"
        )
    source = Path(sys.argv[1])
    output = Path(sys.argv[2])
    width, height = (WIDTH, HEIGHT)
    if len(sys.argv) == 5:
        width, height = int(sys.argv[3]), int(sys.argv[4])
    if (width < 320 or height < 180 or width > 4096 or height > 4096 or
            width * 9 != height * 16):
        raise SystemExit(f"refusing unsupported output dimensions: {width}x{height}")

    with Image.open(source) as image:
        image = image.convert("RGB")
        src_w, src_h = image.size
        if src_w < 2 or src_h < 2 or src_w > 16384 or src_h > 16384:
            raise SystemExit(f"refusing implausible source dimensions: {src_w}x{src_h}")

        # Center-crop to 16:9 without stretching the original photograph.
        if src_w * height > src_h * width:
            crop_w = src_h * width // height
            left = (src_w - crop_w) // 2
            image = image.crop((left, 0, left + crop_w, src_h))
        else:
            crop_h = src_w * height // width
            top = (src_h - crop_h) // 2
            image = image.crop((0, top, src_w, top + crop_h))

        image = image.resize((width, height), Image.Resampling.LANCZOS)
        pixels = bytearray()
        for red, green, blue in image.getdata():
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            pixels.extend(struct.pack("<H", value))

    stride = width * 2
    header = struct.pack("<4s5I", MAGIC, VERSION, width, height, stride, len(pixels))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + pixels)
    print(f"CWP1: {width}x{height}, RGB565, {len(pixels)} pixel bytes, {output.stat().st_size} total bytes")


if __name__ == "__main__":
    main()
