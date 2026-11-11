#!/usr/bin/env bash
# Boot the dedicated guard-fault image and prove #PF was delivered on IST1.
set -uo pipefail

IMAGE="${1:-}"
[ -n "$IMAGE" ] && [ -f "$IMAGE" ] || {
    echo "usage: $0 <stack-guard-test.iso>" >&2
    exit 2
}

BUILD_DIR="${BUILD_DIR:-build/stack-guard-test}"
TIMEOUT="${STACK_GUARD_TIMEOUT:-20}"
LOG="$BUILD_DIR/stack-guard-qemu-serial.log"
QEMU_LOG="$BUILD_DIR/stack-guard-qemu.log"
mkdir -p "$BUILD_DIR"
rm -f "$LOG" "$QEMU_LOG"

set +e
timeout "$TIMEOUT" qemu-system-x86_64 \
    -cdrom "$IMAGE" -m 256M -nographic -no-reboot -no-shutdown -nic none \
    -monitor none -serial "file:$LOG" >"$QEMU_LOG" 2>&1
status=$?
set -e

# The expected kernel panic leaves the guest halted, so timeout status 124 is
# required. An early QEMU exit is not accepted as a contained kernel fault.
if [ "$status" -ne 124 ]; then
    echo "FAIL: expected the contained guard fault to halt the guest (status $status)"
    cat "$LOG" 2>/dev/null || true
    cat "$QEMU_LOG" 2>/dev/null || true
    exit 1
fi

for marker in \
    "[BOOT] init complete, entering scheduler" \
    "KERNEL_STACK_GUARD_TEST_ARMED" \
    "PAGE_FAULT_IST_FRAME_OK" \
    "KERNEL_STACK_GUARD_FAULT_CONTAINED" \
    "KERNEL_STACK_GUARD_TEST_PASS" \
    "KERNEL PANIC: unhandled page fault"; do
    if ! grep -Fq "$marker" "$LOG"; then
        echo "FAIL: missing expected marker: $marker"
        cat "$LOG"
        exit 1
    fi
done

if grep -Fq "PAGE_FAULT_IST_FRAME_INVALID" "$LOG"; then
    echo "FAIL: exception frame did not land on IST1"
    cat "$LOG"
    exit 1
fi

echo "PASS: kernel stack guard fault was contained on the dedicated IST1 stack"
echo "      log: $LOG"
