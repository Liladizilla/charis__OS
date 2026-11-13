#include <assert.h>
#include <stdio.h>
#include <kernel/gamepad.h>

void vga_puts(const char* text) { (void)text; }

int main(void) {
    gamepad_state_t state = {0};
    u8 undersized[8] = {0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A, 0xF0, 0xDE};

    gamepad_init();
    assert(!gamepad_is_connected());
    assert(gamepad_get_state(&state) == -1);

    /* Must reject before reading index 8 from this eight-byte buffer. */
    gamepad_handle_input(undersized, sizeof(undersized));
    assert(!gamepad_is_connected());
    assert(gamepad_get_state(&state) == -1);

    u8 complete[9] = {
        0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A, 0xF0, 0xDE, 0xA5
    };
    gamepad_handle_input(complete, sizeof(complete));
    assert(gamepad_is_connected());
    assert(gamepad_get_state(&state) == 0);
    assert(state.left_x == (s16)0x1234);
    assert(state.left_y == (s16)0x5678);
    assert(state.right_x == (s16)0x9ABC);
    assert(state.right_y == (s16)0xDEF0);
    assert(state.buttons == (u16)0xA5DE);

    puts("gamepad input bounds: OK");
    return 0;
}
