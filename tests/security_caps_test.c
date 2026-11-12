#include <assert.h>
#include <stdio.h>
#include <kernel/security.h>

static void test_single_capability_checks(void) {
    task_t task = {0};
    task.capabilities = CAP_FS_READ | CAP_NET_RAW;

    assert(security_check_capability(&task, CAP_FS_READ));
    assert(security_check_capability(&task, CAP_NET_RAW));
    assert(!security_check_capability(&task, CAP_FS_WRITE));
    assert(!security_check_capability(NULL, CAP_FS_READ));
}

static void test_multi_capability_checks_require_every_bit(void) {
    task_t task = {0};
    task.capabilities = CAP_SPAWN;

    /* Regression: the old implementation accepted this because one bit
     * matched, even though CAP_ALLOW_EXEC was absent. */
    assert(!security_check_capability(&task, CAP_SPAWN | CAP_ALLOW_EXEC));

    task.capabilities |= CAP_ALLOW_EXEC;
    assert(security_check_capability(&task, CAP_SPAWN | CAP_ALLOW_EXEC));

    task.capabilities = CAP_FS_READ | CAP_FS_WRITE;
    assert(security_check_capability(&task, CAP_FS_READ | CAP_FS_WRITE));
    assert(!security_check_capability(&task, CAP_FS_READ | CAP_FS_DELETE));
}

static void test_empty_and_unknown_masks_fail_closed(void) {
    task_t task = {0};
    task.capabilities = CAP_ALL;

    assert(!security_check_capability(&task, 0));
    assert(!security_check_capability(&task, 1U << 31));
    assert(!security_check_capability(&task, CAP_FS_READ | (1U << 31)));
}

static void test_unknown_grants_are_removed(void) {
    task_t task = {0};
    task.capabilities = CAP_FS_READ | CAP_NET_RAW | (1U << 31);
    security_enforce_capabilities(&task);

    assert(task.capabilities == (CAP_FS_READ | CAP_NET_RAW));
    assert(security_check_capability(&task, CAP_FS_READ | CAP_NET_RAW));
    assert(!security_check_capability(&task, 1U << 31));

    security_enforce_capabilities(NULL);
}

int main(void) {
    test_single_capability_checks();
    test_multi_capability_checks_require_every_bit();
    test_empty_and_unknown_masks_fail_closed();
    test_unknown_grants_are_removed();
    puts("PASS: capability conjunction, validation and grant sanitization");
    return 0;
}
