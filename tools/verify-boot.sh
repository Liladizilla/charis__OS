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
    # OVMF ships in two incompatible shapes:
    #   * the classic ~2MB CODE/VARS pair, passed with -bios
    #   * the "4M" split builds (.fd or .qcow2), which must be mapped as pflash
    #     alongside a writable VARS region, and which QEMU rejects outright
    #     with "could not load PC BIOS" if handed to -bios
    # Pick the invocation that matches the file rather than assuming.
    case "$FIRMWARE" in
        *4M*)
            VARS="${FIRMWARE%_CODE*}_VARS${FIRMWARE#*_CODE}"
            [ -f "$VARS" ] || {
                echo "verify-boot.sh: 4M firmware needs a VARS image, none found for $FIRMWARE" >&2
                exit 2
            }
            # VARS is written by firmware; work on a copy so a real install
            # on the host is never modified.
            cp -f "$VARS" "$BUILD_DIR/ovmf-vars.img"
            case "$FIRMWARE" in
                *.qcow2) FMT=qcow2 ;;
                *)       FMT=raw   ;;
            esac
            DRIVE+=(-drive "if=pflash,format=$FMT,readonly=on,file=$FIRMWARE"
                    -drive "if=pflash,format=$FMT,file=$BUILD_DIR/ovmf-vars.img")
            ;;
        *)
            DRIVE+=(-bios "$FIRMWARE")
            ;;
    esac
fi

# Remember the image digest BEFORE booting, so we can prove the boot did not
# alter it. UEFI firmware writes back to the medium it booted from.
IMAGE_SHA_BEFORE=""
if command -v sha256sum >/dev/null 2>&1; then
    IMAGE_SHA_BEFORE="$(sha256sum "$IMAGE" | cut -d' ' -f1)"
fi

# -nographic puts the serial console on stdout. The VGA text console is not
# captured, so anything the kernel prints only via vga_puts is invisible here.
#
# -nic none matters for more than tidiness. With a NIC attached, SeaBIOS hands
# control to iPXE, which runs a full DHCP + network boot before falling through
# to the next device. That costs tens of seconds, is a source of run-to-run
# flakiness, and buys nothing: the kernel has no network support yet. Removing
# the device makes the boot short and deterministic.
boot_once() {
    timeout "$TIMEOUT" qemu-system-x86_64 \
        "${DRIVE[@]}" \
        -m 256M -nographic -no-reboot \
        -nic none \
        2>&1 | tr -d '\r' > "$LOG" || true

    # Strip terminal control sequences before asserting on the text.
    #
    # This is not cosmetic. GRUB and SeaBIOS redraw parts of the line with
    # cursor positioning, so a phrase can arrive with escapes wedged into the
    # middle of it -- "Boo" <ESC>[04;03H "ting kernel..." -- which breaks a
    # grep for a contiguous string even though the kernel booted correctly. A
    # CI run failed exactly this way while booting a perfectly good image.
    sed -i -e 's/\x1b\[[0-9;?]*[ -\/]*[@-~]//g' \
           -e 's/\x1b[()][A-Z0-9]//g' \
           -e 's/\x1b[@-Z\\-_]//g' \
           -e 's/\r//g' "$LOG"
}

# A QEMU-level failure (firmware not found, resource clash, bad pflash pair)
# is transient often enough to be worth one retry. A kernel that hangs in
# initialisation survives the retry, so this does not mask real regressions.
ATTEMPTS=$(( ${VERIFY_RETRIES:-1} + 1 ))
attempt=1
while : ; do
    boot_once
    # Distinguish "the emulator failed" from "the kernel did not boot".
    if grep -q '^qemu:' "$LOG" || grep -qi 'could not load\|failed to initialize\|cannot find' "$LOG"; then
        QEMU_ERROR=1
    else
        QEMU_ERROR=0
    fi
    if grep -q '\[BOOT\] init complete, entering scheduler' "$LOG" \
       && ! grep -q 'KERNEL PANIC' "$LOG" \
       && ! grep -q 'Invalid Multiboot2 magic' "$LOG"; then
        break
    fi
    # A definitive "nothing bootable" verdict will not change on a retry, and
    # the firmware just sits there until the timeout expires. Fail now.
    if grep -qi 'no bootable device\|nothing to boot' "$LOG"; then
        break
    fi
    [ "$attempt" -lt "$ATTEMPTS" ] || break
    attempt=$((attempt + 1))
    echo "note: boot attempt $((attempt - 1)) of $ATTEMPTS did not reach the scheduler, retrying..."
done

fail() {
    echo "FAIL: $1"
    if [ "${QEMU_ERROR:-0}" = "1" ]; then
        echo "      (QEMU itself reported an error -- this looks emulator-side,"
        echo "       not a kernel fault; see the qemu: line below)"
    fi
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
