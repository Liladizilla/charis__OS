#include <assert.h>
#include <stdio.h>
#include <kernel/task_id.h>

int main(void) {
    u64 next_pid = 1;
    assert(task_pid_allocate(NULL) == 0);
    assert(task_pid_allocate(&next_pid) == 1);
    assert(task_pid_allocate(&next_pid) == 2);
    assert(next_pid == 3);

    next_pid = 0xFFFFFFFEULL;
    assert(task_pid_allocate(&next_pid) == 0xFFFFFFFEU);
    assert(task_pid_allocate(&next_pid) == 0xFFFFFFFFU);
    assert(next_pid == 0x100000000ULL);

    /* Exhaustion is sticky: no later task can reuse a previous PID. */
    assert(task_pid_allocate(&next_pid) == 0);
    assert(task_pid_allocate(&next_pid) == 0);
    assert(next_pid == 0x100000000ULL);

    next_pid = 0;
    assert(task_pid_allocate(&next_pid) == 0);
    assert(next_pid == 0);
    puts("PASS: task PID allocation is monotonic and fails closed at exhaustion");
    return 0;
}
