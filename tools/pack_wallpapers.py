#!/usr/bin/env python3
"""Pack validated CWP1 RGB565 images into one bounded CWP2 boot module."""
from pathlib import Path
import struct
import sys

CWP1_HEADER = struct.Struct("<4s5I")
CWP2_HEADER = struct.Struct("<4s4I")
CWP2_ENTRY = struct.Struct("<6I")
CWP1_MAGIC = b"CWP1"
CWP1_VERSION = 1
CWP2_MAGIC = b"CWP2"
CWP2_VERSION = 2
MAX_DIMENSION = 4096
MAX_IMAGE_BYTES = 16 * 1024 * 1024
MAX_MODULE_BYTES = 24 * 1024 * 1024
WALLPAPER_ID_LIMIT = 8


def read_cwp1(path: Path) -> tuple[int, int, int, bytes]:
    raw = path.read_bytes()
    if len(raw) < CWP1_HEADER.size:
        raise ValueError(f"{path}: truncated CWP1 header")
    magic, version, width, height, stride, data_size = CWP1_HEADER.unpack_from(raw)
    expected = width * height * 2
    if magic != CWP1_MAGIC or version != CWP1_VERSION:
        raise ValueError(f"{path}: unsupported CWP1 signature or version")
    if not width or not height or width > MAX_DIMENSION or height > MAX_DIMENSION:
        raise ValueError(f"{path}: dimensions outside the kernel limit")
    if stride != width * 2 or data_size != expected or data_size > MAX_IMAGE_BYTES:
        raise ValueError(f"{path}: invalid stride or RGB565 payload size")
    if len(raw) != CWP1_HEADER.size + data_size:
        raise ValueError(f"{path}: file length does not match its CWP1 header")
    return width, height, stride, raw[CWP1_HEADER.size:]


def main() -> None:
    if len(sys.argv) < 4:
        raise SystemExit(
            "usage: pack_wallpapers.py output.cwp2 id:image.cwp id:image.cwp [...]"
        )

    output = Path(sys.argv[1])
    images: list[tuple[int, int, int, int, bytes]] = []
    seen: set[int] = set()
    for spec in sys.argv[2:]:
        image_id_text, separator, image_path = spec.partition(":")
        if not separator or not image_path:
            raise SystemExit(f"invalid image specification: {spec!r}")
        try:
            image_id = int(image_id_text, 10)
        except ValueError as error:
            raise SystemExit(f"invalid wallpaper ID: {image_id_text!r}") from error
        if image_id < 0 or image_id >= WALLPAPER_ID_LIMIT or image_id in seen:
            raise SystemExit(f"duplicate or unsupported wallpaper ID: {image_id}")
        seen.add(image_id)
        try:
            width, height, stride, pixels = read_cwp1(Path(image_path))
        except (OSError, ValueError) as error:
            raise SystemExit(str(error)) from error
        images.append((image_id, width, height, stride, pixels))

    if not images:
        raise SystemExit("at least one wallpaper is required")

    directory_end = CWP2_HEADER.size + len(images) * CWP2_ENTRY.size
    entries = bytearray()
    payloads = bytearray()
    for image_id, width, height, stride, pixels in images:
        offset = directory_end + len(payloads)
        entries.extend(CWP2_ENTRY.pack(
            image_id, width, height, stride, offset, len(pixels)
        ))
        payloads.extend(pixels)

    total_size = directory_end + len(payloads)
    if total_size > MAX_MODULE_BYTES:
        raise SystemExit(
            f"wallpaper collection is {total_size} bytes; limit is {MAX_MODULE_BYTES}"
        )

    header = CWP2_HEADER.pack(
        CWP2_MAGIC, CWP2_VERSION, len(images), CWP2_ENTRY.size, total_size
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + entries + payloads)
    print(
        f"CWP2: {len(images)} wallpapers, {total_size} bytes, "
        f"{len(payloads)} RGB565 pixel bytes"
    )


if __name__ == "__main__":
    main()
