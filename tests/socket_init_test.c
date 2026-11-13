#include <assert.h>
#include <stdio.h>
#include <kernel/socket.h>

void vga_puts(const char* text) { (void)text; }

int main(void) {
    /* A zeroed BSS table is not a free-slot table: every fd starts as 0. */
    assert(socket_create(AF_INET, SOCK_STREAM) == -1);

    socket_init();
    assert(socket_create(AF_INET, SOCK_STREAM) == 0);
    assert(socket_create(AF_INET, SOCK_DGRAM) == 1);

    for (int i = 2; i < SOCKET_MAX; i++)
        assert(socket_create(AF_INET, SOCK_STREAM) == i);
    assert(socket_create(AF_INET, SOCK_STREAM) == -1);

    socket_close(7);
    assert(socket_create(AF_INET, SOCK_DGRAM) == 7);
    assert(socket_create(0, SOCK_STREAM) == -1);
    assert(socket_create(AF_INET, 0) == -1);
    assert(socket_create(AF_INET, SOCK_STREAM) == -1);

    socket_close(-1);
    socket_close(SOCKET_MAX);
    puts("socket init/lifecycle: OK");
    return 0;
}
