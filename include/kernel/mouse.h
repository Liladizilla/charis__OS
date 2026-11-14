/* mouse.h - PS/2 mouse driver */
#pragma once
#include <kernel/types.h>

typedef struct {
    int32_t x, y;         /* current cursor position in pixels */
    int32_t dx, dy;       /* last movement delta */
    uint8_t buttons;      /* bits 0-2=left/right/middle, bits 3-4=side/extra */
} mouse_state_t;

extern mouse_state_t g_mouse;

#define MOUSE_SPEED_MIN_PERCENT 50u
#define MOUSE_SPEED_MAX_PERCENT 200u
#define MOUSE_SPEED_STEP_PERCENT 25u

void mouse_init(void);
bool mouse_is_available(void);
u32 mouse_get_speed_percent(void);
void mouse_set_speed_percent(u32 percent);
bool mouse_wheel_available(void);
/* Atomically take pending wheel movement; positive is physical wheel-down. */
void mouse_take_scroll_deltas(s32* horizontal, s32* vertical);

/* Diagnostics: live state and a packet counter, so a stuck pointer can be
 * told apart from a dead IRQ. */
extern volatile u32 g_mouse_packets;
u32 mouse_packet_count(void);
/* Atomic snapshot of IRQ-updated mouse state for task-context readers. */
void mouse_get_snapshot(mouse_state_t* out);
mouse_state_t* mouse_get_state(void);
void mouse_handler(void); /* Called from IRQ 12 */
void mouse_set_bounds(uint32_t width, uint32_t height);

/* Poll-based mouse reading (non-blocking) */
bool mouse_has_input(void);
int mouse_get_movement(int* dx, int* dy);
int mouse_get_buttons(int* buttons);

#ifdef MOUSE_HOST_TEST
void mouse_test_process_byte(u8 status, u8 byte);
void mouse_test_set_device_id(u8 id);
#endif