#!/usr/bin/env bash
#
# verify-boot.sh — boot gate for CharisOS.
#
# Boots a CharisOS image in QEMU and asserts the kernel actually reached the
# scheduler. The kernel prints this line on the serial console only after the
# Multiboot2 magic validated, every init*() returned, and the shell task was
# created:
#
#     [BOOT] init complete, entering scheduler
#
# A hang anywhere in initialisation (the pci_scan loop, the fs_open FAT walk, a
# rejected magic, a missing device) means the line never appears.
#
# Grepping for the boot banner or for GRUB's menu text is NOT sufficient:
# the banner is printed before the magic is validated, and GRUB suppresses its
# own output on a runner with no TTY.
#
# Usage:
#   verify-boot.sh <image> [--disk] [--uefi] [--timeout SECONDS]
#
#   --disk     attach the image as a raw hard disk (a flashed USB stick)
#              instead of a CD-ROM
#   --uefi     boot with OVMF instead of legacy BIOS
#
# Exits 0 if the kernel reached the scheduler, 1 otherwise. The captured log is
# left at $BUILD_DIR/verify-<mode>.log (or verify.log) for inspection.

set -uo pipefail

IMAGE=""
ATTACH="cd"
FIRMWARE=""
TIMEOUT=60
LOG=""

usage() { sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --disk)   ATTACH="disk"; shift ;;
        --uefi)   FIRMWARE="${OVMF:-/usr/share/edk2/ovmf/OVMF_CODE.fd}"; shift ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        -h|--help) usage ;;
        -*) echo "unknown option: $1" >&2; exit 2 ;;
        *)  IMAGE="$1"; shift ;;
    esac
done

[ -n "$IMAGE" ] || { echo "verify-boot.sh: no image given" >&2; exit 2; }
[ -f "$IMAGE" ] || { echo "verify-boot.sh: image not found: $IMAGE" >&2; exit 2; }

BUILD_DIR="${BUILD_DIR:-build}"
mkdir -p "$BUILD_DIR"

MODE="cd"
[ "$ATTACH" = "disk" ] && MODE="disk"
LOG="$BUILD_DIR/verify-$MODE.log"
[ -n "$FIRMWARE" ] && LOG="$BUILD_DIR/verify-$MODE-uefi.log"

# Build the QEMU argument list.
if [ "$ATTACH" = "disk" ]; then
    # snapshot=on is essential, not a nicety: UEFI firmware writes back to the
    # medium it booted from (GPT/boot-entry state), so without it a UEFI test
    # run silently mutates the artifact and the published checksum stops
    # matching. Firmware touching a real USB stick is normal and harmless;
    # corrupting the build output is not.
    DRIVE=(-drive "file=$IMAGE,format=raw,if=ide,snapshot=on")
else
    DRIVE=(-cdrom "$IMAGE")
fi

if [ -n "$FIRMWARE" ]; then
    [ -f "$FIRMWARE" ] || { echo "verify-boot.sh: OVMF firmware not found at $FIRMWARE" >&2; exit 2; }
    DRIVE+=(-bios "$FIRMWARE")
fi

# Remember the image digest BEFORE booting, so we can prove the boot did not
# alter it. UEFI firmware writes back to the medium it booted from.
IMAGE_SHA_BEFORE=""
if command -v sha256sum >/dev/null 2>&1; then
    IMAGE_SHA_BEFORE="$(sha256sum "$IMAGE" | cut -d' ' -f1)"
fi

# -nographic puts the serial console on stdout. The VGA text console is not
# captured, so anything the kernel prints only via vga_puts is invisible here.
timeout "$TIMEOUT" qemu-system-x86_64 \
    "${DRIVE[@]}" \
    -m 256M -nographic -no-reboot \
    2>&1 | tr -d '\r' > "$LOG" || true

fail() {
    echo "FAIL: $1"
    echo "--- captured output ($LOG) ---"
    cat "$LOG"
    echo "--------------------------------"
    exit 1
}

grep -q 'Booting kernel' "$LOG" \
    || fail "GRUB never handed off to the kernel (image or firmware problem)"

grep -q 'Built:' "$LOG" \
    || fail "kernel_main never reached its banner"

grep -q '\[BOOT\] init complete, entering scheduler' "$LOG" \
    || fail "kernel did not complete initialisation — it hung or halted"

grep -q 'Invalid Multiboot2 magic' "$LOG" \
    && fail "Multiboot2 magic was rejected"

grep -q 'KERNEL PANIC' "$LOG" \
    && fail "kernel reported a panic"

# A test run must never modify the artifact it is testing.
if command -v sha256sum >/dev/null 2>&1; then
    AFTER="$(sha256sum "$IMAGE" | cut -d' ' -f1)"
    if [ -n "${IMAGE_SHA_BEFORE:-}" ] && [ "$AFTER" != "$IMAGE_SHA_BEFORE" ]; then
        echo "FAIL: booting modified the image"
        echo "      before: $IMAGE_SHA_BEFORE"
        echo "      after:  $AFTER"
        echo "      UEFI firmware writes back to the medium it booted from;"
        echo "      the disk path must use snapshot=on."
        exit 1
    fi
fi

echo "PASS: kernel booted and entered the scheduler  ($IMAGE as $MODE${FIRMWARE:+, UEFI})"
echo "      log: $LOG"
exit 0
