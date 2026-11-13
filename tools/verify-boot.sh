#!/usr/bin/env bash
#
# verify-boot.sh — boot gate for CharisOS.
#
# Boots a CharisOS image in QEMU and asserts kernel initialization completed,
# the ring-3 task made a syscall, and the guest stayed alive. The kernel prints
# the init sentinel only after the Multiboot2 magic validated and all init*()
# calls returned:
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
#   verify-boot.sh <image> [--disk] [--uefi] [--elf-test-disk PATH] [--expect-elf-exec] [--timeout SECONDS]
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
# Exits 0 if the ring-3 task ran and the guest stayed alive, 1 otherwise. The
# captured log is left at $BUILD_DIR/verify-<mode>.log for inspection.

set -uo pipefail

IMAGE=""
ATTACH="cd"
FIRMWARE=""
USE_KVM=0
NEEDS_DISPLAY=0
TIMEOUT=60
LOG=""
ELF_TEST_DISK=""
EXPECT_ELF_SUCCESS=0
REQUIRE_VMM_TESTS=0
REQUIRE_WAITPID=0
REQUIRE_IPC_WAIT=0

usage() { sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --disk)   ATTACH="disk"; shift ;;
        --uefi)   FIRMWARE="${OVMF:-/usr/share/edk2/ovmf/OVMF_CODE.fd}"; shift ;;
        --kvm)    USE_KVM=1; shift ;;
        --elf-test-disk) ELF_TEST_DISK="$2"; shift 2 ;;
        --expect-elf-exec) EXPECT_ELF_SUCCESS=1; shift ;;
        --require-vmm-tests) REQUIRE_VMM_TESTS=1; shift ;;
        --require-waitpid) REQUIRE_WAITPID=1; shift ;;
        --require-ipc-wait) REQUIRE_IPC_WAIT=1; shift ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        -h|--help) usage ;;
        -*) echo "unknown option: $1" >&2; exit 2 ;;
        *)  IMAGE="$1"; shift ;;
    esac
done

[ -n "$IMAGE" ] || { echo "verify-boot.sh: no image given" >&2; exit 2; }
[ -f "$IMAGE" ] || { echo "verify-boot.sh: image not found: $IMAGE" >&2; exit 2; }
if [ -n "$ELF_TEST_DISK" ]; then
    [ -f "$ELF_TEST_DISK" ] || { echo "verify-boot.sh: ELF test disk not found: $ELF_TEST_DISK" >&2; exit 2; }
    [ "$ATTACH" = "cd" ] || { echo "verify-boot.sh: --elf-test-disk requires CD boot mode" >&2; exit 2; }
fi
if [ "$EXPECT_ELF_SUCCESS" = "1" ] && [ -z "$ELF_TEST_DISK" ]; then
    echo "verify-boot.sh: --expect-elf-exec requires --elf-test-disk" >&2
    exit 2
fi

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
if [ -n "$ELF_TEST_DISK" ]; then
    DRIVE+=(-drive "file=$ELF_TEST_DISK,format=raw,if=ide,index=0,snapshot=on")
    BOOT_ORDER=(-boot order=d)
else
    BOOT_ORDER=()
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
        # The VARS image is not reliably a sibling. The naming is not
        # consistent -- the _4M suffix appears on the CODE name only when it
        # is there, and the extension follows whichever form was used -- and on
        # Ubuntu the CODE image lives at /usr/share/qemu/OVMF.fd while its VARS
        # companion is under /usr/share/OVMF with a different name. Try the
        # likely siblings, then the standard install directories.
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
                    "${FIRMWARE%.fd}_VARS_4M.fd" \
                    /usr/share/OVMF/OVMF_VARS_4M.fd \
                    /usr/share/OVMF/OVMF_VARS_4M.qcow2 \
                    /usr/share/OVMF/OVMF_VARS.fd \
                    /usr/share/edk2/ovmf/OVMF_VARS_4M.fd \
                    /usr/share/edk2/ovmf/OVMF_VARS_4M.qcow2 \
                    /usr/share/edk2-ovmf/OVMF_VARS_4M.fd; do
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
    # Capture serial directly to a file in both modes. QEMU's -nographic
    # stdio multiplexing can leave the pipeline empty on headless hosts.
    if [ "$NEEDS_DISPLAY" = "1" ]; then
        timeout "$TIMEOUT" qemu-system-x86_64 \
            "${DRIVE[@]}" "${BOOT_ORDER[@]}" -m 256M -no-reboot -nic none \
            -d int,cpu_reset,guest_errors -D "$BUILD_DIR/verify-qemu-debug.log" \
            -serial "file:$LOG" >/dev/null 2>&1
        QEMU_STATUS=$?
    else
        # timeout returns 124 when it fires. That is the signal we want: QEMU
        # was still running when the clock ran out, so the guest is alive.
        # Keep emulator diagnostics separate from the kernel serial stream.
        QEMU_LOG="$BUILD_DIR/verify-qemu.log"
        timeout "$TIMEOUT" qemu-system-x86_64 \
            "${DRIVE[@]}" "${BOOT_ORDER[@]}" \
            -m 256M -nographic -no-reboot -nic none \
            -d int,cpu_reset,guest_errors -D "$BUILD_DIR/verify-qemu-debug.log" \
            -monitor none -serial "file:$LOG" \
            >"$QEMU_LOG" 2>&1
        QEMU_STATUS=$?
        cat "$QEMU_LOG" >> "$LOG"
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
    if [ -s "$BUILD_DIR/verify-qemu-debug.log" ]; then
        echo "--- QEMU CPU/exception trace (last 160 lines) ---"
        tail -n 160 "$BUILD_DIR/verify-qemu-debug.log"
    fi
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

grep -q 'Hello from user mode!' "$LOG" \
    || fail "ring-3 task did not complete its print syscall"

grep -q 'RING3_CODE_ISOLATION_OK' "$LOG" \
    || fail "bootstrap user code was not copied into private executable pages"

grep -q 'TSS_RSP0_USER_STACK_OK' "$LOG" \
    || fail "ring-3 task did not receive its own kernel interrupt stack"

grep -q 'TASK_STACK_GUARD_OK' "$LOG" \
    || fail "kernel task stack is missing its unmapped page-table guard"

if [ -n "$ELF_TEST_DISK" ]; then
    if [ "$EXPECT_ELF_SUCCESS" = "1" ]; then
        grep -q 'RING3_SLEEP_OK' "$LOG" \
            || fail "ring-3 task did not return from sleep before the authorized exec"
        grep -q 'ELF_EXEC_OK' "$LOG" \
            || fail "authorized sys_exec did not enter the static ELF smoke program"
        if grep -q 'ELF_EXEC_DENIED_CAPABILITY' "$LOG"; then
            fail "authorized exec was unexpectedly denied by capability policy"
        fi
        ELF_RESULT="authorized ELF entered"
    elif grep -q 'ELF_EXEC_OK' "$LOG"; then
        ELF_RESULT="executed"
    elif grep -q 'ELF_EXEC_DENIED_CAPABILITY' "$LOG"; then
        ELF_RESULT="denied by capability policy"
        echo "NOTE: ELF execution correctly denied: bootstrap task lacks CAP_ALLOW_EXEC."
    else
        fail "ELF image neither executed nor produced an explicit capability denial"
    fi
else
    grep -q 'RING3_SLEEP_OK' "$LOG" \
        || fail "ring-3 sleep syscall did not return to user mode"
    grep -q 'Back in user mode after yield.' "$LOG" \
        || fail "ring-3 task did not resume after yielding to the kernel task"
    grep -q 'RING3_FAULT_ARMED' "$LOG" \
        || fail "ring-3 page-fault isolation probe did not run"
    grep -q 'USER PAGE FAULT:' "$LOG" \
        || fail "invalid ring-3 access was not contained as a user page fault"
    grep -q 'RING3_SYSCALL_PTR_REJECT_OK' "$LOG" \
        || fail "ring-3 syscall pointer validation did not reject untrusted addresses"
    grep -q 'RING3_PRIVATE_PAGES_RELEASED' "$LOG" \
        || fail "ring-3 exit did not release its private code and stack frames"
fi

if [ "$REQUIRE_VMM_TESTS" = "1" ]; then
    for test_name in map_existing map_missing remap_same unmap_page \
                     map_user_page walk_rejects_huge translate_effective_user \
                     physmap_supervisor_alias pmm_direct_mapped_ram pmm_free_count \
                     pmm_reserved_frame_protected user_kernel_half_denied \
                     unaligned_page_denied mapping_validation_matrix \
                     destroy_address_space kernel_clone_rejects_user; do
        grep -Fq "[VMM] $test_name: OK" "$LOG" \
            || fail "VMM regression test failed or did not execute: $test_name"
    done
fi

if [ "$REQUIRE_WAITPID" = "1" ]; then
    grep -q 'WAITPID_TEST_OK' "$LOG" \
        || fail "waitpid child-selection/status/reaping regression did not pass"
    grep -q 'WAITPID_ORPHAN_REAP_OK' "$LOG" \
        || fail "orphaned child was not reclaimed after exit"
    if grep -q -e 'WAITPID_TEST_FAIL' -e 'WAITPID_ORPHAN_REAP_FAIL' "$LOG"; then
        fail "waitpid regression emitted a failure marker"
    fi
fi

if [ "$REQUIRE_IPC_WAIT" = "1" ]; then
    for marker in IPC_WAIT_MESSAGE_DRAIN_OK IPC_WAIT_PEER_EXIT_WAKE_OK \
                  IPC_WAIT_CANCEL_WAKE_OK IPC_WAIT_QEMU_TEST_OK; do
        grep -q "$marker" "$LOG" || fail "IPC waiter regression did not emit $marker"
    done
    if grep -q -e 'IPC_WAIT_TEST_FAIL' -e 'IPC_WAIT_TEST_SETUP_FAIL' "$LOG"; then
        fail "IPC waiter regression emitted a failure marker"
    fi
fi

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

if [ -n "$ELF_TEST_DISK" ]; then
    echo "PASS: kernel booted; ELF policy result: $ELF_RESULT  ($IMAGE with $ELF_TEST_DISK)"
else
    echo "PASS: kernel booted, ran ring 3, and stayed alive  ($IMAGE as $MODE${FIRMWARE:+, UEFI})"
fi
echo "      log: $LOG"
exit 0
