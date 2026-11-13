#include <stdio.h>
#include <stdlib.h>
#include <kernel/mouse.h>

static u32 keyboard_bytes;
static u8 last_keyboard_byte;

void keyboard_process_scancode(u8 scancode) {
    keyboard_bytes++;
    last_keyboard_byte = scancode;
}

void mouse_test_process_byte(u8 status, u8 byte);

static void expect(bool ok, const char* message) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void send_packet(u8 flags, u8 dx, u8 dy) {
    mouse_test_process_byte(0x21u, flags);
    mouse_test_process_byte(0x21u, dx);
    mouse_test_process_byte(0x21u, dy);
}

int main(void) {
    mouse_set_bounds(800, 600);

    mouse_test_process_byte(0x01u, 0x1Eu);
    expect(keyboard_bytes == 1 && last_keyboard_byte == 0x1E,
           "non-AUX byte is routed to keyboard");
    expect(mouse_packet_count() == 0, "keyboard byte does not complete a mouse packet");

    mouse_test_process_byte(0x21u, 0x00u);
    mouse_test_process_byte(0x21u, 0x0Au);
    mouse_test_process_byte(0x21u, 0x05u);
    expect(mouse_packet_count() == 0, "invalid packet header is discarded");

    send_packet(0x09u, 10u, 5u);
    mouse_state_t state;
    mouse_get_snapshot(&state);
    expect(mouse_packet_count() == 1, "valid packet increments packet counter");
    expect(state.x == 60 && state.y == 45, "positive deltas move right and up");
    expect(state.buttons == 1, "left button bit is preserved");

    send_packet(0x08u, 0xFFu, 0xFEu);
    mouse_get_snapshot(&state);
    expect(mouse_packet_count() == 2, "second valid packet is accepted");
    expect(state.x == 59 && state.y == 47, "negative signed deltas move left and down");
    expect(state.buttons == 0, "released buttons are recorded");

    send_packet(0xC8u, 20u, 20u);
    mouse_get_snapshot(&state);
    expect(mouse_packet_count() == 2, "overflow packet is ignored");
    expect(state.x == 59 && state.y == 47, "overflow packet cannot corrupt coordinates");

    mouse_set_bounds(60u, 48u);
    send_packet(0x08u, 127u, 127u);
    mouse_get_snapshot(&state);
    expect(state.x == 59 && state.y == 0,
           "screen bounds clamp positive x and inverted y correctly");

    mouse_set_bounds(0, 0);
    send_packet(0x08u, 127u, 127u);
    mouse_get_snapshot(&state);
    expect(state.x == 0 && state.y == 0, "zero-sized bounds pin the pointer safely");

    puts("mouse packet decoder tests: PASS");
    return 0;
}
