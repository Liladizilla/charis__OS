#include <kernel/gamepad.h>
#include <kernel/memory.h>
#include <kernel/vga.h>

static gamepad_state_t g_gamepad_state = {0};
static bool g_gamepad_connected = false;

/* USB HID report descriptor parsing is complex.
 * For now, we support the standard Xbox-compatible gamepad format.
 * Report: 8 bytes
 * - Byte 0-1: Left stick X/Y (16-bit each)
 * - Byte 2-3: Right stick X/Y
 * - Byte 4: Left trigger
 * - Byte 5: Right trigger  
 * - Byte 6-7: Button bitmask
 */

void gamepad_init(void) {
    /* Gamepad support will come from USB HID once USB stack is complete */
    /* For Phase 10, we provide the API and placeholder */
    g_gamepad_connected = false;
    g_gamepad_state.left_x = 0;
    g_gamepad_state.left_y = 0;
    g_gamepad_state.right_x = 0;
    g_gamepad_state.right_y = 0;
    g_gamepad_state.left_trigger = 0;
    g_gamepad_state.right_trigger = 0;
    g_gamepad_state.buttons = 0;
    
    vga_puts("Gamepad subsystem initialized\n");
}

int gamepad_get_state(gamepad_state_t* state) {
    if (!state) return -1;
    
    if (!g_gamepad_connected) return -1;
    
    *state = g_gamepad_state;
    return 0;
}

bool gamepad_is_connected(void) {
    return g_gamepad_connected;
}

/* Called by USB HID driver when gamepad is detected */
void gamepad_handle_input(const u8* report, usize len) {
    if (!report || len < 8) return;
    
    g_gamepad_connected = true;
    
    /* Parse standard HID gamepad report */
    g_gamepad_state.left_x = (s16)(report[0] | (report[1] << 8));
    g_gamepad_state.left_y = (s16)(report[2] | (report[3] << 8));
    g_gamepad_state.right_x = (s16)(report[4] | (report[5] << 8));
    g_gamepad_state.right_y = (s16)(report[6] | (report[7] << 8));
    
    if (len >= 8) {
        g_gamepad_state.buttons = report[7] | (report[8] << 8);
    }
}