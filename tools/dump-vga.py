"""Dump the VGA text buffer from a running CharisOS VM.

Used to verify that kernel/logo.c actually renders the CP437 banner in VGA
text mode. vga_putchar() writes one byte per cell, so the banner is only
correct if the kernel emitted raw CP437 code points -- a UTF-8 source string
would render as mojibake.

QEMU exits a few seconds after the kernel faults, so the monitor has to be
attached and read within that window.
"""

import os
import socket
import subprocess
import sys
import time

REPO = "/home/charischara/Documents/charis_OS/charis__OS"
SOCK = "/tmp/vga-monitor.sock"
SETTLE = float(sys.argv[1]) if len(sys.argv) > 1 else 5.0

# 0xDB is the CP437 full block; 0xC9/0xCD/0xBB/0xBA/0xC8/0xBC are the box.
GLYPH = {0xC9: "+-", 0xCD: "=", 0xBB: "+", 0xBA: "|", 0xC8: "+", 0xBC: "+", 0xDB: "#"}


def connect():
    if os.path.exists(SOCK):
        os.unlink(SOCK)
    proc = subprocess.Popen(
        [
            "qemu-system-x86_64",
            "-cdrom", os.path.join(REPO, "build/charisos-vm.iso"),
            "-m", "256M",
            "-display", "none",
            "-nic", "none",
            "-monitor", f"unix:{SOCK},server,nowait",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
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
    return proc, sock


def read_cells(sock, proc):
    """Poll the monitor for the VGA buffer.

    The banner only exists for a moment: the kernel prints it at the end of
    init and then faults, which makes QEMU exit. So keep the last read that
    actually contained banner glyphs rather than simply the most recent one --
    a later read taken after the screen was overwritten would otherwise win.
    """
    last = ""
    banner = ""
    for _ in range(40):
        try:
            sock.sendall(b"xp /2000xh 0xb8000\n")
            time.sleep(0.25)
            data = sock.recv(500000).decode("latin-1")
        except OSError:
            break
        if not data.strip():
            break
        last = data
        # 0xC9 is the top-left box corner and only the banner emits it.
        if "0xc9" in data:
            banner = data
            break
        time.sleep(0.15)
    return banner or last


def render(raw):
    cells = []
    for tok in raw.replace("\n", " ").split():
        try:
            cells.append(int(tok, 16) & 0xFF)
        except ValueError:
            pass
    print(f"[{len(cells)} VGA cells read]\n")
    for row in range(min(25, len(cells) // 80)):
        line = "".join(
            GLYPH.get(c, chr(c) if 32 <= c < 127 else (" " if c in (0, 32) else "·"))
            for c in cells[row * 80:(row + 1) * 80]
        ).rstrip()
        if line:
            print(f"{row:2d}|{line}")


def main():
    proc, sock = connect()
    if sock is None:
        print("could not attach to the QEMU monitor")
        proc.kill()
        return 1
    time.sleep(0.4)
    sock.recv(65536)
    time.sleep(SETTLE)
    raw = read_cells(sock, proc)
    proc.kill()
    render(raw)
    return 0


if __name__ == "__main__":
    sys.exit(main())
