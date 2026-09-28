#!/usr/bin/env python3
"""Drive the setup wizard with real key events and capture each step.

The wizard is only meaningful if it responds to the keyboard, so this sends
actual scancodes through the QEMU monitor rather than poking kernel state, and
captures the framebuffer after each one. Pausing the CPU before each read
matters: the desktop repaints on the timer tick, and dumping a running guest
catches a half-drawn frame that looks like a rendering bug.
"""

import os
import re
import socket
import subprocess
import sys
import time

REPO = "/home/charischara/Documents/charis_OS/charis__OS"
OVMF = os.environ.get("OVMF", "/usr/share/edk2/ovmf/OVMF_CODE.fd")
SOCK = "/tmp/wiz-monitor.sock"
FB_BASE = 0x80000000
W, H, PITCH = 1024, 768, 4096


class VM:
    def __init__(self, uefi=True):
        if os.path.exists(SOCK):
            os.unlink(SOCK)
        cmd = [
            "qemu-system-x86_64",
            "-cdrom", f"{REPO}/build/charisos.iso",
            "-m", "256M", "-vga", "std", "-display", "none", "-nic", "none",
            "-serial", "file:/tmp/wiz.serial",
            "-monitor", f"unix:{SOCK},server,nowait",
        ]
        if uefi:
            cmd += ["-bios", OVMF]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)
        self.sock = None
        for _ in range(150):
            if os.path.exists(SOCK):
                try:
                    self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    self.sock.connect(SOCK)
                    break
                except OSError:
                    self.sock = None
            time.sleep(0.1)
        if self.sock is None:
            raise SystemExit("could not attach to the monitor")
        time.sleep(0.4)
        try:
            self.sock.recv(65536)
        except OSError:
            pass

    def cmd(self, text, wait=0.8):
        self.sock.settimeout(1.0)
        try:
            self.sock.sendall((text + "\n").encode())
        except OSError:
            return ""
        time.sleep(wait)
        out = b""
        deadline = time.time() + 3.0
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(400000)
                if not chunk:
                    break
                out += chunk
            except socket.timeout:
                if out:
                    break
            except OSError:
                break
        return out.decode("latin-1", "replace")

    def key(self, name):
        self.cmd(f"sendkey {name}", wait=0.5)

    def shot(self, path):
        """Pause, read the framebuffer, resume."""
        self.cmd("stop", wait=0.4)
        pixels = bytearray(W * H * 3)
        per_row = PITCH // 4
        CHUNK = 16384
        for start in range(0, per_row * H, CHUNK):
            count = min(CHUNK, per_row * H - start)
            raw = self.cmd(f"xp /{count}xw 0x{FB_BASE + start * 4:x}", wait=0.45)
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
                    if idx >= per_row * H:
                        break
                    try:
                        val = int(tok, 16)
                    except ValueError:
                        continue
                    row, col = divmod(idx, per_row)
                    o = (row * W + col) * 3
                    pixels[o]     = (val >> 16) & 0xFF
                    pixels[o + 1] = (val >> 8) & 0xFF
                    pixels[o + 2] = val & 0xFF
                    idx += 1
        self.cmd("cont", wait=0.3)
        try:
            from PIL import Image
            Image.frombytes("RGB", (W, H), bytes(pixels)).save(path)
        except ImportError:
            with open(path + ".ppm", "wb") as fh:
                fh.write(b"P6\n%d %d\n255\n" % (W, H))
                fh.write(bytes(pixels))

    def close(self):
        self.proc.kill()


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/wiz"
    vm = VM(uefi=True)
    try:
        # Let it boot and reach the wizard.
        time.sleep(12)
        vm.shot(f"{out}-0-welcome.png")
        print("captured welcome")

        # Begin setup -> language
        vm.key("ret"); time.sleep(1.5)
        vm.shot(f"{out}-1-language.png")
        print("captured language")

        # Down twice, to move the selection
        vm.key("down"); time.sleep(0.8)
        vm.key("down"); time.sleep(0.8)
        vm.shot(f"{out}-2-language-moved.png")
        print("captured language after two Down")

        # Next -> timezone
        vm.key("ret"); time.sleep(1.5)
        vm.shot(f"{out}-3-timezone.png")
        print("captured timezone")

        # Next -> network (the disk step is skipped unless installing)
        vm.key("ret"); time.sleep(1.5)
        vm.shot(f"{out}-4-network.png")
        print("captured network")

        # Next -> wallpaper, then cycle the styles
        vm.key("ret"); time.sleep(1.5)
        vm.shot(f"{out}-5-wallpaper.png")
        print("captured wallpaper")

        vm.key("right"); time.sleep(1.2)
        vm.shot(f"{out}-6-wallpaper-ember.png")
        print("captured wallpaper after Right")

        # Next -> done
        vm.key("ret"); time.sleep(1.5)
        vm.shot(f"{out}-7-done.png")
        print("captured done")

        vm.key("ret"); time.sleep(3.0)
        vm.shot(f"{out}-8-desktop.png")
        print("captured desktop after finishing")
    finally:
        vm.close()


if __name__ == "__main__":
    main()
