"""Read the kernel's framebuffer straight out of guest memory.

QEMU's `screendump` only understands the legacy VGA framebuffer, so under a
UEFI/GOP boot it reports black even when the kernel has drawn correctly. The
monitor's `xp` command reads physical memory directly, which is the only
reliable way to confirm what the kernel actually rendered.

Usage: readfb.py [--uefi] [--settle N]
"""

import os
import socket
import subprocess
import sys
import time

REPO = "/home/charischara/Documents/charis_OS/charis__OS"
OVMF = os.environ.get("OVMF", "/usr/share/edk2/ovmf/OVMF_CODE.fd")
SOCK = "/tmp/readfb-monitor.sock"
USE_UEFI = "--uefi" in sys.argv
SETTLE = 9.0
if "--settle" in sys.argv:
    SETTLE = float(sys.argv[sys.argv.index("--settle") + 1])


def monitor(cmd, sock, wait=1.2):
    """Send a monitor command and collect whatever comes back.

    The socket gets a short timeout: QEMU's monitor keeps the connection open
    after answering, so looping on recv() until EOF would block forever.
    """
    sock.settimeout(1.0)
    try:
        sock.sendall((cmd + "\n").encode())
    except OSError:
        return ""
    time.sleep(wait)
    out = b""
    deadline = time.time() + 2.0
    while time.time() < deadline:
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


def main():
    if os.path.exists(SOCK):
        os.unlink(SOCK)

    cmd = [
        "qemu-system-x86_64",
        "-cdrom", os.path.join(REPO, "build/charisos.iso"),
        "-m", "256M", "-vga", "std", "-display", "none", "-nic", "none",
        "-serial", "file:/tmp/readfb.serial",
        "-monitor", f"unix:{SOCK},server,nowait",
    ]
    if USE_UEFI:
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

    # Ask the guest where its framebuffer is.
    info = monitor("info registers", sock, 0.5)
    print("--- framebuffer location, as reported by the kernel on serial ---")
    try:
        with open("/tmp/readfb.serial") as fh:
            for line in fh:
                if "FB:" in line:
                    print("   ", line.strip())
    except OSError:
        pass

    # Read the GOP buffer. 0x80000000 is what the GFX tag reports under OVMF.
    print("--- sampling guest memory at 0x80000000 ---")
    raw = monitor("xp /64xw 0x80000000", sock, 1.5)
    colours = {}
    for tok in raw.replace("\n", " ").split():
        t = tok.strip()
        if t.startswith("0x") and len(t) >= 6:
            try:
                v = int(t, 16)
            except ValueError:
                continue
            colours[v] = colours.get(v, 0) + 1
    if not colours:
        print("   no values returned; monitor said:")
        print("   ", raw[:400])
    else:
        for v, n in sorted(colours.items(), key=lambda kv: -kv[1])[:8]:
            b, g, r = v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF
            print(f"    0x{v:08x}  = rgb({r:3d},{g:3d},{b:3d})  x{n}")

    proc.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
