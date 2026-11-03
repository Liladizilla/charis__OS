#!/usr/bin/env python3
"""Boot CharisOS under QEMU, drive first-run setup through QMP, and capture desktop.

This is a host-side CI harness. The guest OS remains C and Assembly.
"""
from __future__ import annotations

import argparse
import json
import os
import socket
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


class QMP:
    def __init__(self, path: str, timeout: float = 20.0) -> None:
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        deadline = time.monotonic() + timeout
        while True:
            try:
                self.sock.connect(path)
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic() >= deadline:
                    raise TimeoutError("QEMU QMP socket did not become available")
                time.sleep(0.1)
        self.sock.settimeout(15.0)
        self.stream = self.sock.makefile("rwb", buffering=0)
        greeting = self._read_message()
        if "QMP" not in greeting:
            raise RuntimeError(f"unexpected QMP greeting: {greeting!r}")
        self.sequence = 0
        self.command("qmp_capabilities")

    def _read_message(self) -> dict:
        line = self.stream.readline()
        if not line:
            raise RuntimeError("QMP connection closed unexpectedly")
        return json.loads(line.decode("utf-8"))

    def command(self, name: str, arguments: dict | None = None) -> dict:
        self.sequence += 1
        command_id = self.sequence
        payload = {"execute": name, "id": command_id}
        if arguments is not None:
            payload["arguments"] = arguments
        self.stream.write((json.dumps(payload) + "\r\n").encode("utf-8"))
        while True:
            response = self._read_message()
            if response.get("id") != command_id:
                continue
            if "error" in response:
                raise RuntimeError(f"QMP {name} failed: {response['error']}")
            return response.get("return", {})

    def close(self) -> None:
        try:
            self.stream.close()
        finally:
            self.sock.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--ovmf-code", type=Path, required=True)
    parser.add_argument("--ovmf-vars", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--serial-log", type=Path, required=True)
    parser.add_argument("--qemu-log", type=Path, required=True)
    parser.add_argument("--steps", type=int, default=8)
    parser.add_argument("--startup-wait", type=float, default=8.0)
    args = parser.parse_args()

    for path in (args.iso, args.ovmf_code, args.ovmf_vars):
        if not path.is_file():
            raise FileNotFoundError(f"required boot image does not exist: {path}")
    if args.steps < 1 or args.steps > 16:
        raise ValueError("--steps must be between 1 and 16")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.serial_log.parent.mkdir(parents=True, exist_ok=True)
    args.qemu_log.parent.mkdir(parents=True, exist_ok=True)

    fd, qmp_path = tempfile.mkstemp(prefix="charisos-", suffix=".qmp")
    os.close(fd)
    os.unlink(qmp_path)
    # OVMF's packaged VARS image lives under /usr/share and is read-only.
    # Copy it into the writable artifact directory, never beside the firmware.
    vars_fd, vars_path = tempfile.mkstemp(
        prefix="charisos-ovmf-vars-", suffix=".fd", dir=args.output.parent
    )
    os.close(vars_fd)
    shutil.copyfile(args.ovmf_vars, vars_path)

    command = [
        "qemu-system-x86_64",
        "-machine", "q35",
        "-m", "512M",
        "-vga", "std",
        "-nic", "none",
        "-no-reboot",
        "-drive", f"if=pflash,format=raw,readonly=on,file={args.ovmf_code}",
        "-drive", f"if=pflash,format=raw,file={vars_path}",
        "-cdrom", str(args.iso),
        "-boot", "menu=off",
        "-serial", f"file:{args.serial_log}",
        "-qmp", f"unix:{qmp_path},server=on,wait=off",
        "-monitor", "none",
        "-display", "none",
    ]

    qmp: QMP | None = None
    with args.qemu_log.open("wb") as qemu_log:
        process = subprocess.Popen(command, stdout=qemu_log, stderr=subprocess.STDOUT)
        try:
            qmp = QMP(qmp_path)
            status = qmp.command("query-status")
            if status.get("status") != "running":
                raise RuntimeError(f"guest did not enter running state: {status!r}")

            time.sleep(args.startup_wait)
            for index in range(args.steps):
                qmp.command(
                    "send-key",
                    {
                        "keys": [{"type": "qcode", "data": "ret"}],
                        "hold-time": 120,
                    },
                )
                time.sleep(0.8)
                print(f"sent setup confirmation {index + 1}/{args.steps}", flush=True)

            time.sleep(2.0)
            qmp.command("screendump", {"filename": str(args.output.resolve())})
            if not args.output.is_file() or args.output.stat().st_size < 2_000_000:
                raise RuntimeError("QEMU did not produce a complete 1024x768 PPM screenshot")

            qmp.command("quit")
            process.wait(timeout=10)
        finally:
            if qmp is not None:
                qmp.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            for temporary_path in (qmp_path, vars_path):
                try:
                    os.unlink(temporary_path)
                except FileNotFoundError:
                    pass

    serial = args.serial_log.read_text(errors="replace") if args.serial_log.exists() else ""
    marker = "[WIZ] setup finished"
    if marker not in serial:
        raise RuntimeError(
            "the guest did not finish first-run setup; refusing to publish a "
            "welcome-screen screenshot. Inspect the serial log and input counters."
        )

    print(f"Desktop capture written to {args.output}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, TimeoutError, ValueError) as exc:
        print(f"desktop capture failed: {exc}", file=sys.stderr)
        raise SystemExit(2)
