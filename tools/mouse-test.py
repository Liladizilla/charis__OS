"""Test whether the PS/2 mouse actually reaches the kernel.

The cursor sits at the driver's initial position and never moves, which looks
identical whether the IRQ never fires or nothing is asking the mouse to move.
This injects movement through the QEMU monitor and reads the position back
out of guest memory, so the two are distinguishable.

Usage: tools/mouse-test.py
"""

import os
import re
import socket
import subprocess
import sys
import time

REPO = "/home/charischara/Documents/charis_OS/charis__OS"
SOCK = "/tmp/mouse-monitor.sock"


def attach():
    if os.path.exists(SOCK):
        os.unlink(SOCK)
    proc = subprocess.Popen(
        ["qemu-system-x86_64", "-cdrom", f"{REPO}/build/charisos.iso",
         "-m", "2048", "-enable-kvm", "-vga", "std", "-display", "egl-headless",
         "-nic", "none", "-serial", "file:/tmp/mouse.serial",
         "-monitor", f"unix:{SOCK},server,nowait"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
        proc.kill()
        raise SystemExit("could not attach to the monitor")
    time.sleep(0.4)
    try:
        sock.recv(65536)
    except OSError:
        pass
    return proc, sock


def mon(sock, text, wait=0.5):
    sock.settimeout(1.0)
    try:
        sock.sendall((text + "\n").encode())
    except OSError:
        return ""
    time.sleep(wait)
    out = b""
    end = time.time() + 2.0
    while time.time() < end:
        try:
            chunk = sock.recv(200000)
            if not chunk:
                break
            out += chunk
        except socket.timeout:
            if out:
                break
        except OSError:
            break
    return out.decode("latin-1", "replace")


def find_symbol_offset(sock, symbol):
    """Read the kernel symbol table and return the runtime address of `symbol`."""
    raw = mon(sock, f"info files", wait=2.0)
    # The kernel loads at 0x100000; the ELF section base plus the link-time
    # address of the symbol is its runtime address.
    return None


def main():
    proc, sock = attach()
    try:
        time.sleep(14)
        mon(sock, "stop", wait=0.4)

        # g_mouse is {x, y, buttons, dx, dy}. Scan the kernel image for it by
        # asking the monitor for a symbol address via the ELF, which needs the
        # kernel loaded -- instead just inject movement and look for ANY change
        # in the text-mode log or accept the serial's own report.
        print("injecting mouse movement via the monitor...")
        for _ in range(8):
            mon(sock, "mouse_move 25 25", wait=0.25)
        time.sleep(1.0)
        mon(sock, "cont", wait=0.3)
        time.sleep(1.5)
        mon(sock, "stop", wait=0.4)

        # The shell's `mouse` command is the observable: run it and read the
        # VGA text console. If the driver saw the packets, the coordinates
        # will have moved off the initial (50, 50).
        for key in ("mouse",):
            mon(sock, "cont", wait=0.2)
            for ch in key:
                mon(sock, f"sendkey {ch}", wait=0.12)
            mon(sock, "sendkey ret", wait=0.8)
            time.sleep(1.2)

        mon(sock, "stop", wait=0.4)
        raw = mon(sock, "xp /224xh 0xb8000", wait=1.5)
        cells = []
        for line in raw.splitlines():
            if ":" not in line:
                continue
            head, _, rest = line.partition(":")
            try:
                int(head.strip(), 16)
            except ValueError:
                continue
            for tok in rest.split():
                try:
                    cells.append(int(tok, 16) & 0xFF)
                except ValueError:
                    pass
        print("--- VGA text console ---")
        for r in range(min(14, len(cells) // 80)):
            line = "".join(chr(c) if 32 <= c < 127 else " " for c in cells[r*80:(r+1)*80]).rstrip()
            if line:
                print("  " + line)
        mon(sock, "cont", wait=0.2)
    finally:
        proc.kill()


if __name__ == "__main__":
    main()
