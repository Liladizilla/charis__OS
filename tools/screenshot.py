"""Capture a screenshot of the emulated display.

Used to check whether the graphics stack is live. Under a BIOS/SeaBIOS boot
GRUB stays in 80x25 VGA text mode and the framebuffer tag comes back as type 2
pointing at 0xB8000, so the screendump is 720x400 and nothing can be drawn.
Under UEFI, OVMF provides a GOP framebuffer that GRUB inherits, and the
screendump is a real 1024x768 surface.

Usage: screenshot.py <out.ppm> [settle_seconds] [--uefi]
"""

import os
import socket
import subprocess
import sys
import time

REPO = "/home/charischara/Documents/charis_OS/charis__OS"
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/shot.ppm"
SETTLE = float(sys.argv[2]) if len(sys.argv) > 2 and not sys.argv[2].startswith("-") else 6.0
USE_UEFI = "--uefi" in sys.argv
OVMF = os.environ.get("OVMF", "/usr/share/edk2/ovmf/OVMF_CODE.fd")
SOCK = "/tmp/shot-monitor.sock"


def main():
    if os.path.exists(SOCK):
        os.unlink(SOCK)
    if os.path.exists(OUT):
        os.unlink(OUT)

    cmd = [
        "qemu-system-x86_64",
        "-cdrom", os.path.join(REPO, "build/charisos.iso"),
        "-m", "256M",
        "-vga", "std",
        "-display", "none",
        "-nic", "none",
        "-serial", f"file:{OUT}.serial",
        "-monitor", f"unix:{SOCK},server,nowait",
    ]
    if USE_UEFI:
        if not os.path.exists(OVMF):
            print(f"OVMF not found at {OVMF}")
            return 1
        cmd += ["-bios", OVMF]

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
    try:
        sock.sendall(f"screendump {OUT}\n".encode())
        time.sleep(2.0)
        sock.recv(65536)
    except OSError as exc:
        print(f"screendump failed: {exc}")

    proc.kill()
    time.sleep(0.3)

    if os.path.exists(OUT):
        size = os.path.getsize(OUT)
        with open(OUT, "rb") as fh:
            head = fh.read(32)
        dims = head.split(b"\n")[1].decode() if b"\n" in head else "?"
        print(f"screenshot: {OUT}  {size} bytes  {dims}")
        return 0

    print("no screenshot produced")
    return 1


if __name__ == "__main__":
    sys.exit(main())
