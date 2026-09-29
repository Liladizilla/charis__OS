"""Dump the kernel's framebuffer out of guest memory as a PNG.

QEMU's `screendump` only reads the legacy VGA framebuffer, so under a UEFI/GOP
boot it reports black no matter what the kernel drew. This pulls the GFX-tag
framebuffer out through the monitor instead, so the actual rendered output can
be looked at.

Usage: dumpfb.py [--uefi] [--settle N] [--out file.png]
"""

import os
import socket
import subprocess
import sys
import time

REPO = "/home/charischara/Documents/charis_OS/charis__OS"
OVMF = os.environ.get("OVMF", "/usr/share/edk2/ovmf/OVMF_CODE.fd")
SOCK = "/tmp/dumpfb-monitor.sock"

USE_UEFI = "--uefi" in sys.argv
SETTLE = 9.0
if "--settle" in sys.argv:
    SETTLE = float(sys.argv[sys.argv.index("--settle") + 1])
OUT = "/tmp/framebuffer.png"
if "--out" in sys.argv:
    OUT = sys.argv[sys.argv.index("--out") + 1]

W, H, PITCH = 1024, 768, 4096
FB_BASE = int(os.environ.get("FB_BASE", "0x80000000"), 16)


def monitor(cmd, sock, wait=0.35, budget=6.0):
    sock.settimeout(1.0)
    try:
        sock.sendall((cmd + "\n").encode())
    except OSError:
        return ""
    time.sleep(wait)
    out = b""
    deadline = time.time() + budget
    while time.time() < deadline:
        try:
            chunk = sock.recv(500000)
            if not chunk:
                break
            out += chunk
        except socket.timeout:
            if out:
                break
        except OSError:
            break
    return out.decode("latin-1", "replace")


def main():
    if os.path.exists(SOCK):
        os.unlink(SOCK)

    cmd = [
        "qemu-system-x86_64",
        "-cdrom", os.path.join(REPO, "build/charisos.iso"),
        "-m", "256M", "-vga", "std", "-display", "none", "-nic", "none",
        "-serial", "file:/tmp/dumpfb.serial",
        "-monitor", f"unix:{SOCK},server,nowait",
    ]
    if USE_UEFI:
        cmd += ["-bios", OVMF]
    elif "--bios" in sys.argv:
        pass  # default: SeaBIOS, no -bios

    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    sock = None
    for _ in range(150):
        if os.path.exists(SOCK):
            try:
                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                sock.connect(SOCK)
                break
            except OSError:
                sock = None
        time.sleep(0.1)
    if sock is None:
        print("could not attach to the monitor")
        proc.kill()
        return 1

    time.sleep(0.4)
    try:
        sock.recv(65536)
    except OSError:
        pass
    time.sleep(SETTLE)

    # Stop the CPU before reading. The desktop repaints continuously on the
    # timer tick, so dumping a running guest catches a half-finished frame --
    # which looks exactly like a rendering bug and is not one.
    monitor("stop", sock, wait=0.5)

    words_per_row = PITCH // 4          # 1024 pixels
    total_words = words_per_row * H     # 786432

    pixels = bytearray(W * H * 3)
    print(f"reading {total_words} words ({W}x{H}) in chunks...")

    CHUNK = 16384
    for start in range(0, total_words, CHUNK):
        count = min(CHUNK, total_words - start)
        addr = FB_BASE + start * 4
        raw = monitor(f"xp /{count}xw 0x{addr:x}", sock, wait=0.5, budget=4.0)

        # Lines look like: "0x00000000ffffa00000000000: 00000000 DEADBEEF ..."
        for line in raw.splitlines():
            if ":" not in line:
                continue
            head, _, rest = line.partition(":")
            try:
                base = int(head.strip(), 16)
            except ValueError:
                continue
            idx = (base - FB_BASE) // 4
            for tok in rest.split():
                if idx >= total_words:
                    break
                try:
                    val = int(tok, 16)
                except ValueError:
                    continue
                if idx < words_per_row:
                    px = idx
                    o = (px * 3)
                    pixels[o]     = (val >> 16) & 0xFF   # R at bit 16
                    pixels[o + 1] = (val >> 8) & 0xFF
                    pixels[o + 2] = val & 0xFF
                else:
                    row = idx // words_per_row
                    col = idx % words_per_row
                    o = (row * W + col) * 3
                    pixels[o]     = (val >> 16) & 0xFF
                    pixels[o + 1] = (val >> 8) & 0xFF
                    pixels[o + 2] = val & 0xFF
                idx += 1

        if (start // CHUNK) % 12 == 0:
            print(f"  {start}/{total_words} words")

    proc.kill()

    nonblack = sum(1 for i in range(0, len(pixels), 3)
                   if pixels[i] or pixels[i + 1] or pixels[i + 2])
    print(f"non-black pixels: {nonblack} of {W * H} ({100.0 * nonblack / (W * H):.1f}%)")

    try:
        from PIL import Image
        img = Image.frombytes("RGB", (W, H), bytes(pixels))
        img.save(OUT)
        print(f"wrote {OUT}")
    except ImportError:
        with open(OUT + ".ppm", "wb") as fh:
            fh.write(b"P6\n%d %d\n255\n" % (W, H))
            fh.write(bytes(pixels))
        print(f"wrote {OUT}.ppm")
    return 0


if __name__ == "__main__":
    sys.exit(main())
