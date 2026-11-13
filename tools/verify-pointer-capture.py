#!/usr/bin/env python3
"""Verify real QEMU PS/2 pointer movement and framebuffer save-under restore."""
from pathlib import Path
import sys
from PIL import Image, ImageChops


def fail(message: str) -> None:
    raise SystemExit(f"pointer capture verification failed: {message}")


def load(path: str):
    try:
        with Image.open(path) as image:
            return image.convert("RGB")
    except Exception as exc:
        fail(f"could not read {path}: {exc}")


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: verify-pointer-capture.py initial.ppm moved.ppm restored.ppm"
        )

    initial, moved, restored = (load(p) for p in sys.argv[1:])
    if not (initial.size == moved.size == restored.size):
        fail("screen dimensions changed during pointer movement")

    width, height = initial.size
    if width < 100 or height < 160:
        fail("framebuffer is too small for a meaningful pointer test")

    # Exclude the bottom taskbar, whose live clock is intentionally refreshed
    # during this test. The workspace above it should be otherwise static.
    workspace = (0, 0, width, height - 100)
    before = initial.crop(workspace)
    after_move = moved.crop(workspace)
    after_restore = restored.crop(workspace)

    movement = ImageChops.difference(before, after_move)
    if movement.getbbox() is None:
        fail("the guest framebuffer did not change after HMP mouse_move")

    restored_diff = ImageChops.difference(before, after_restore)
    if restored_diff.getbbox() is not None:
        bbox = restored_diff.getbbox()
        fail(
            "moving the PS/2 pointer away and back left framebuffer trails "
            f"or changed the desktop outside the taskbar (diff bounds {bbox})"
        )

    print(
        "QEMU PS/2 pointer movement and save-under restoration: PASS "
        f"({width}x{height})"
    )


if __name__ == "__main__":
    main()
