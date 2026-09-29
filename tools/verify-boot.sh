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
#   --kvm      use KVM hardware acceleration when the host allows it
#
# KVM is not optional coverage. TCG (pure emulation) tolerates things real
# hardware does not: it accepts writes to reserved MSRs and ignores reserved
# bits in EFER, among others. Every bug of that shape passes the whole suite
# under TCG and then faults the instant anyone runs the image in GNOME Boxes,
# VirtualBox or any accelerated VM -- which is how the misnumbered SYSCALL MSRs
# shipped in every release up to and including v1.1.0.
#
# Note: --kvm is applied to the BIOS path only. UEFI+KVM is a separate,
# not-yet-diagnosed failure and is not part of the gate.
#
# Exits 0 if the kernel reached the scheduler, 1 otherwise. The captured log is
# left at $BUILD_DIR/verify-<mode>.log (or verify.log) for inspection.

set -uo pipefail

IMAGE=""
ATTACH="cd"
FIRMWARE=""
USE_KVM=0
NEEDS_DISPLAY=0
TIMEOUT=60
LOG=""

usage() { sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --disk)   ATTACH="disk"; shift ;;
        --uefi)   FIRMWARE="${OVMF:-/usr/share/edk2/ovmf/OVMF_CODE.fd}"; shift ;;
        --kvm)    USE_KVM=1; shift ;;
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
[ "$USE_KVM" = "1" ] && LOG="$LOG-kvm"

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

# Only add -enable-kvm if the host really offers it; otherwise QEMU fails to
# start and the run is wasted.
if [ "$USE_KVM" = "1" ]; then
    if [ -w /dev/kvm ] && [ -e /dev/kvm ]; then
        DRIVE+=(-enable-kvm)
    else
        echo "note: --kvm requested but /dev/kvm is unavailable; running under TCG" >&2
    fi
fi

if [ -n "$FIRMWARE" ]; then
    [ -f "$FIRMWARE" ] || { echo "verify-boot.sh: OVMF firmware not found at $FIRMWARE" >&2; exit 2; }
    # OVMF ships in two incompatible shapes:
    #   * the classic ~2MB CODE/VARS pair, passed with -bios
    #   * the "4M" split builds (.fd or .qcow2), which must be mapped as pflash
    #     alongside a writable VARS region, and which QEMU rejects outright
    # Choose the invocation from the file's actual size, not its name. Some
    # distributions ship the 4MB build as a plain "OVMF_CODE.fd", which is
    # loadable only via pflash; handing that to -bios gives
    # "could not load PC BIOS", which reads like a kernel fault and is not one.
    FIRMWARE_BYTES=$(wc -c < "$FIRMWARE")
    FIRMWARE_4M=0
    [ "$FIRMWARE_BYTES" -gt 3000000 ] && FIRMWARE_4M=1
    case "$FIRMWARE" in *4M*) FIRMWARE_4M=1 ;; esac

    if [ "$FIRMWARE_4M" = "1" ]; then
        # The VARS image is a sibling, but the naming is not consistent: it
        # carries the _4M suffix only when the CODE file does, and the
        # extension follows whichever form was used. Try the likely names
        # rather than deriving exactly one.
        VARS=""
        # Some builds are named OVMF.fd with no _CODE component at all, in
        # which case the suffix-strip forms above yield nonsense
        # ("OVMF.fd_VARS"). Strip the extension as well.
        for cand in "${FIRMWARE%_CODE*}_VARS${FIRMWARE#*_CODE}" \
                    "${FIRMWARE%_CODE*}_VARS.fd" \
                    "${FIRMWARE%_CODE*}_VARS.qcow2" \
                    "${FIRMWARE%_CODE*}_VARS_4M.fd" \
                    "${FIRMWARE%_CODE*}_VARS_4M.qcow2" \
                    "${FIRMWARE%.fd}_VARS.fd" \
                    "${FIRMWARE%.qcow2}_VARS.qcow2" \
                    "${FIRMWARE%.fd}_VARS_4M.fd"; do
            if [ -f "$cand" ]; then VARS="$cand"; break; fi
        done
        if [ -z "$VARS" ]; then
            echo "verify-boot.sh: 4M firmware needs a VARS image, none found beside $FIRMWARE" >&2
            exit 2
        fi
        # Firmware writes to VARS; work on a copy so a real host install is
        # never modified.
        cp -f "$VARS" "$BUILD_DIR/ovmf-vars.img"
        case "$FIRMWARE" in
            *.qcow2) FMT=qcow2 ;;
            *)       FMT=raw   ;;
        esac
        DRIVE+=(-drive "if=pflash,format=$FMT,readonly=on,file=$FIRMWARE"
                -drive "if=pflash,format=$FMT,file=$BUILD_DIR/ovmf-vars.img")
    else
        DRIVE+=(-bios "$FIRMWARE")
    fi

    # A UEFI guest needs a display device the firmware can actually drive.
    # With no display at all, OVMF under KVM never gets past "starting
    # Boot000N" -- the firmware stops before the kernel entry point is ever
    # reached, which looks exactly like a kernel fault and is not one.
    NEEDS_DISPLAY=1
    DRIVE+=(-display egl-headless)
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
    # With a display the serial port cannot be -nographic (that implies no
    # display), so capture it to the log file directly instead of a pipe.
    if [ "$NEEDS_DISPLAY" = "1" ]; then
        timeout "$TIMEOUT" qemu-system-x86_64 \
            "${DRIVE[@]}" -m 256M -no-reboot -nic none \
            -serial "file:$LOG" >/dev/null 2>&1
        QEMU_STATUS=$?
    else
    # timeout returns 124 when it fires. That is the signal we want: QEMU was
    # still running when the clock ran out, so the guest is alive and did not
    # reset. Any other status means QEMU exited early, which for this kernel
    # means the CPU took a triple fault and rebooted.
    timeout "$TIMEOUT" qemu-system-x86_64 \
        "${DRIVE[@]}" \
        -m 256M -nographic -no-reboot \
        -nic none \
        2>&1 | tr -d '\r' > "$LOG"
    QEMU_STATUS=${PIPESTATUS[0]}
    fi

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
# Three attempts by default. The firmware occasionally stalls under KVM before
# ever reaching the kernel -- it is a QEMU/OVMF hiccup, not a kernel fault --
# so a single retry still produced occasional false failures in the gate.
ATTEMPTS=$(( ${VERIFY_RETRIES:-2} + 1 ))
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

# NOTE: assertions below match only strings the KERNEL writes to the serial
# port. Do not assert on GRUB's own output, for two reasons both learned the
# hard way here:
#   * GRUB suppresses its menu rendering when the runner has no TTY;
#   * grub.cfg sets `terminal_output gfxterm`, so GRUB's `echo` lines go to the
#     graphical terminal and never reach the serial log at all.
# Kernel serial output is deterministic however the image was booted.

grep -q 'Built:' "$LOG" \
    || fail "kernel_main never reached its banner"

grep -q '\[BOOT\] init complete, entering scheduler' "$LOG" \
    || fail "kernel did not complete initialisation — it hung or halted"

grep -q 'Invalid Multiboot2 magic' "$LOG" \
    && fail "Multiboot2 magic was rejected"

grep -q 'KERNEL PANIC' "$LOG" \
    && fail "kernel reported a panic"

# LIVENESS. Reaching the scheduler is not the same as surviving it. A triple
# fault after the init sequence reboots the guest, so the kernel can print
# "[BOOT] init complete" and still never run a single process. A second
# firmware banner in the log is the tell, and so is QEMU exiting before the
# timeout fired.
if grep -q 'SeaBIOS (version' "$LOG" && [ "$(grep -c 'SeaBIOS (version' "$LOG")" -gt 1 ]; then
    fail "guest rebooted after init -- the kernel faults instead of staying up"
fi
if [ "${QEMU_STATUS:-124}" != "124" ]; then
    fail "QEMU exited early (status $QEMU_STATUS) -- the guest did not stay alive
      status 124 means 'timeout fired, still running', which is what we want.
      Anything else means the guest shut down or reset."
fi

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
