"""Type into the running kernel and read back the text console.

`charisos>` being on screen proves the shell started, not that it is alive: a
hang right after the prompt looks identical. This sends real keystrokes and
then reads the VGA text buffer, so "the prompt is there" and "the prompt still
answers" can be told apart.
"""

import os
import re
import socket
import subprocess
import sys
import time

REPO = "/home/charischara/Documents/charis_OS/charis__OS"
SOCK = "/tmp/shell-monitor.sock"
SETTLE = float(sys.argv[1]) if len(sys.argv) > 1 else 14.0
TYPED = sys.argv[2] if len(sys.argv) > 2 else "help"


def attach():
    if os.path.exists(SOCK):
        os.unlink(SOCK)
    proc = subprocess.Popen(
        ["qemu-system-x86_64", "-cdrom", f"{REPO}/build/charisos.iso",
         "-m", "2048", "-enable-kvm", "-display", "none", "-nic", "none",
         "-serial", "file:/tmp/shell.serial",
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


def mon(sock, text, wait=0.6):
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


def text_console(sock, rows=14):
    """Read the VGA text buffer at 0xB8000."""
    raw = mon(sock, f"xp /{rows * 80}xh 0xb8000", wait=1.2)
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
    out = []
    for r in range(min(rows, len(cells) // 80)):
        line = "".join(chr(c) if 32 <= c < 127 else " " for c in cells[r*80:(r+1)*80])
        if line.strip():
            out.append(line.rstrip())
    return out


def main():
    proc, sock = attach()
    try:
        time.sleep(SETTLE)
        mon(sock, "stop", wait=0.4)
        print("=== before typing ===")
        for line in text_console(sock):
            print("  " + line)
        mon(sock, "cont", wait=0.3)

        for ch in TYPED:
            mon(sock, f"sendkey {ch}", wait=0.12)
        time.sleep(0.6)
        mon(sock, "sendkey ret", wait=0.8)
        time.sleep(1.5)

        mon(sock, "stop", wait=0.4)
        print(f"=== after typing '{TYPED}' + Enter ===")
        for line in text_console(sock):
            print("  " + line)
        mon(sock, "cont", wait=0.2)
    finally:
        proc.kill()


if __name__ == "__main__":
    main()
