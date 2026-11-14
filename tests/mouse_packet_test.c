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

static void send_wheel_packet(u8 flags, u8 dx, u8 dy, u8 wheel) {
    mouse_test_process_byte(0x21u, flags);
    mouse_test_process_byte(0x21u, dx);
    mouse_test_process_byte(0x21u, dy);
    mouse_test_process_byte(0x21u, wheel);
}

int main(void) {
    mouse_set_bounds(800, 600);

    mouse_test_process_byte(0x00u, 0x1Du);
    expect(keyboard_bytes == 0 && mouse_packet_count() == 0,
           "bytes without output-buffer status are ignored");

    mouse_test_process_byte(0x01u, 0x1Eu);
    expect(keyboard_bytes == 1 && last_keyboard_byte == 0x1E,
           "non-AUX byte is routed to keyboard");
    expect(mouse_packet_count() == 0, "keyboard byte does not complete a mouse packet");

    mouse_test_process_byte(0x21u, 0x00u);
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

    mouse_set_bounds(100u, 100u);
    mouse_set_speed_percent(50u);
    expect(mouse_get_speed_percent() == 50u,
           "minimum pointer speed is accepted");
    send_packet(0x08u, 1u, 0xFFu);
    mouse_get_snapshot(&state);
    expect(state.x == 0 && state.y == 0,
           "half-speed movement carries fractional pixels");
    send_packet(0x08u, 1u, 0xFFu);
    mouse_get_snapshot(&state);
    expect(state.x == 1 && state.y == 1 &&
           state.dx == 1 && state.dy == -1,
           "two half-speed packets accumulate to one pixel");

    mouse_set_speed_percent(100u);
    send_packet(0x08u, 1u, 0xFFu);
    mouse_get_snapshot(&state);
    expect(state.x == 2 && state.y == 2,
           "changing pointer speed clears the previous fractional remainder");

    mouse_set_speed_percent(200u);
    expect(mouse_get_speed_percent() == 200u,
           "maximum pointer speed is accepted");
    send_packet(0x08u, 2u, 0xFEu);
    mouse_get_snapshot(&state);
    expect(state.x == 6 && state.y == 6,
           "double-speed pointer movement doubles both axes");

    mouse_set_speed_percent(0u);
    expect(mouse_get_speed_percent() == MOUSE_SPEED_MIN_PERCENT,
           "out-of-range low pointer speed clamps to minimum");
    mouse_set_speed_percent(300u);
    expect(mouse_get_speed_percent() == MOUSE_SPEED_MAX_PERCENT,
           "out-of-range high pointer speed clamps to maximum");
    mouse_set_speed_percent(113u);
    expect(mouse_get_speed_percent() == 125u,
           "pointer speed snaps to the nearest 25 percent step");

    mouse_test_set_device_id(0u);
    send_packet(0x08u, 0u, 0u);
    s32 scroll_x = 0, scroll_y = 0;
    mouse_take_scroll_deltas(&scroll_x, &scroll_y);
    expect(scroll_x == 0 && scroll_y == 0,
           "standard PS/2 packet mode has no wheel data");

    mouse_test_set_device_id(3u);
    expect(mouse_wheel_available(), "ID 3 advertises vertical wheel support");
    send_wheel_packet(0x08u, 0u, 0u, 2u);
    mouse_take_scroll_deltas(&scroll_x, &scroll_y);
    expect(scroll_x == 0 && scroll_y == 2,
           "ID 3 positive wheel delta is accumulated");
    send_wheel_packet(0x08u, 0u, 0u, 0xFEu);
    mouse_take_scroll_deltas(&scroll_x, &scroll_y);
    expect(scroll_y == -2, "ID 3 negative wheel delta is sign extended");

    mouse_test_set_device_id(4u);
    send_wheel_packet(0x08u, 0u, 0u, 0x33u);
    mouse_get_snapshot(&state);
    mouse_take_scroll_deltas(&scroll_x, &scroll_y);
    expect((state.buttons & 0x18u) == 0x18u,
           "ID 4 side and extra button bits are mapped");
    expect(scroll_y == 3, "ID 4 vertical wheel nibble is decoded");
    send_wheel_packet(0x08u, 0u, 0u, 0x08u);
    mouse_take_scroll_deltas(&scroll_x, &scroll_y);
    expect(scroll_y == -8, "ID 4 negative wheel nibble is sign extended");
    send_wheel_packet(0x08u, 0u, 0u, 0x42u);
    mouse_take_scroll_deltas(&scroll_x, &scroll_y);
    expect(scroll_x == 2 && scroll_y == 0,
           "ID 4 horizontal wheel uses its signed 6-bit field");

    puts("mouse packet decoder tests: PASS");
    return 0;
}
