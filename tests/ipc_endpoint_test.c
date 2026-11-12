#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kernel/ipc.h>

static task_t task_a, task_b, task_c;
static task_t* active_task;
task_t* scheduler_current(void) { return active_task; }

static void reset_task(task_t* task, u32 pid) {
    *task = (task_t){0};
    task->pid = pid;
    task->state = TASK_STATE_READY;
    task->scheduler_queued = true;
    for (u32 i = 0; i < IPC_MAX_ENDPOINTS; i++)
        task->ipc_endpoint_generations[i] = 1;
}
static void test_pair_validation(void) {
    ipc_handle_t a = 99, b = 99;
    task_t unpublished = {0};
    unpublished.pid = 808;
    unpublished.state = TASK_STATE_READY;
    for (u32 i = 0; i < IPC_MAX_ENDPOINTS; i++)
        unpublished.ipc_endpoint_generations[i] = 1;
    assert(!ipc_endpoint_pair_create(&task_a, &unpublished,
        IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &a, &b));
    assert(!ipc_endpoint_pair_create(&task_a, &task_b, 0, IPC_RIGHT_RECEIVE, &a, &b));
    assert(a == IPC_HANDLE_INVALID && b == IPC_HANDLE_INVALID);
    assert(!ipc_endpoint_pair_create(&task_a, &task_a, IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &a, &b));
    assert(!ipc_endpoint_pair_create(&task_a, &task_b, IPC_RIGHT_SEND | 0x80U, IPC_RIGHT_RECEIVE, &a, &b));
    task_c.is_user = true;
    assert(!ipc_endpoint_pair_create(&task_a, &task_c, IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &a, &b));
    task_c.is_user = false;
}
static void test_rights_queue_and_close(void) {
    ipc_handle_t send_a, receive_b;
    assert(ipc_endpoint_pair_create(&task_a, &task_b, IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &send_a, &receive_b));
    assert((u32)(send_a >> 32) == task_a.pid);
    assert((u32)(receive_b >> 32) == task_b.pid);
    const char payload[] = "charis-ipc";
    active_task = &task_b;
    assert(ipc_send(send_a, payload, sizeof(payload)) == IPC_STATUS_ERROR);
    assert(ipc_send(receive_b, payload, sizeof(payload)) == IPC_STATUS_ERROR);
    assert(ipc_recv(receive_b, (void*)payload, sizeof(payload), NULL) == IPC_STATUS_EMPTY);
    assert(ipc_close(send_a) == IPC_STATUS_ERROR);
    active_task = &task_a;
    assert(ipc_recv(send_a, (void*)payload, sizeof(payload), NULL) == IPC_STATUS_ERROR);
    assert(ipc_send(send_a, payload, IPC_MAX_MSG_SIZE + 1U) == IPC_STATUS_ERROR);
    assert(ipc_send(send_a, NULL, 1) == IPC_STATUS_ERROR);
    for (u32 i = 0; i < IPC_MAX_MESSAGES; i++)
        assert(ipc_send(send_a, payload, sizeof(payload)) == IPC_STATUS_OK);
    assert(ipc_send(send_a, payload, sizeof(payload)) == IPC_STATUS_FULL);
    active_task = &task_b;
    u8 small[2]; u32 sender = 0;
    assert(ipc_recv(receive_b, small, sizeof(small), &sender) == IPC_STATUS_BUFFER_SMALL);
    assert(sender == 0);
    char received[sizeof(payload)];
    for (u32 i = 0; i < IPC_MAX_MESSAGES; i++) {
        memset(received, 0, sizeof(received));
        assert(ipc_recv(receive_b, received, sizeof(received), &sender) == (int)sizeof(payload));
        assert(sender == task_a.pid && memcmp(received, payload, sizeof(payload)) == 0);
    }
    assert(ipc_recv(receive_b, received, sizeof(received), NULL) == IPC_STATUS_EMPTY);
    active_task = &task_a;
    assert(ipc_send(send_a, payload, sizeof(payload)) == IPC_STATUS_OK);
    assert(ipc_close(send_a) == IPC_STATUS_OK);
    assert(ipc_send(send_a, payload, sizeof(payload)) == IPC_STATUS_ERROR);
    active_task = &task_b;
    assert(ipc_recv(receive_b, received, sizeof(received), &sender) == (int)sizeof(payload));
    assert(sender == task_a.pid);
    assert(ipc_send(receive_b, payload, sizeof(payload)) == IPC_STATUS_PEER_CLOSED);
    assert(ipc_close(receive_b) == IPC_STATUS_OK);
    assert(ipc_close(receive_b) == IPC_STATUS_ERROR);
}
static void test_stale_handle_and_teardown(void) {
    ipc_handle_t stale, peer;
    assert(ipc_endpoint_pair_create(&task_a, &task_c, IPC_RIGHT_MASK, IPC_RIGHT_MASK, &stale, &peer));
    active_task = &task_a;
    assert(ipc_close(stale) == IPC_STATUS_OK);
    ipc_handle_t fresh, fresh_peer;
    assert(ipc_endpoint_pair_create(&task_a, &task_b, IPC_RIGHT_MASK, IPC_RIGHT_MASK, &fresh, &fresh_peer));
    assert(fresh != stale);
    assert(ipc_send(stale, "x", 1) == IPC_STATUS_ERROR);
    assert(ipc_close(stale) == IPC_STATUS_ERROR);
    ipc_task_close_all(&task_a);
    assert(task_a.ipc_endpoints[0] == NULL && task_a.ipc_endpoints[1] == NULL);
    assert(ipc_send(fresh, "x", 1) == IPC_STATUS_ERROR);
    active_task = &task_b;
    assert(ipc_send(fresh_peer, "x", 1) == IPC_STATUS_PEER_CLOSED);
    assert(ipc_close(fresh_peer) == IPC_STATUS_OK);
    active_task = &task_c;
    assert(ipc_close(peer) == IPC_STATUS_OK);
}
static void test_generation_exhaustion(void) {
    task_t retired, peer;
    reset_task(&retired, 950);
    reset_task(&peer, 951);
    for (u32 i = 0; i < IPC_MAX_ENDPOINTS; i++)
        retired.ipc_endpoint_generations[i] = IPC_HANDLE_GENERATION_MAX;
    ipc_handle_t a, b;
    assert(!ipc_endpoint_pair_create(&retired, &peer,
        IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &a, &b));
}

static void test_table_limit(void) {
    task_t owner, peers[IPC_MAX_ENDPOINTS], extra;
    ipc_handle_t own[IPC_MAX_ENDPOINTS], peer[IPC_MAX_ENDPOINTS];
    reset_task(&owner, 401);
    for (u32 i = 0; i < IPC_MAX_ENDPOINTS; i++) {
        reset_task(&peers[i], 500+i);
        assert(ipc_endpoint_pair_create(&owner, &peers[i], IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &own[i], &peer[i]));
    }
    reset_task(&extra, 900);
    ipc_handle_t h1, h2;
    assert(!ipc_endpoint_pair_create(&owner, &extra, IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &h1, &h2));
    ipc_task_close_all(&owner);
    for (u32 i = 0; i < IPC_MAX_ENDPOINTS; i++) ipc_task_close_all(&peers[i]);
}
int main(void) {
    reset_task(&task_a, 101); reset_task(&task_b, 202); reset_task(&task_c, 303);
    ipc_init();
    test_pair_validation();
    test_rights_queue_and_close();
    test_stale_handle_and_teardown();
    test_generation_exhaustion();
    test_table_limit();
    puts("PASS: task-owned IPC handles, rights, bounded queues and teardown");
    return 0;
}
