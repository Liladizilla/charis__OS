#include <kernel/mouse.h>
#include <kernel/io.h>
#include <kernel/printf.h>
#include <kernel/irq.h>
#include <kernel/keyboard.h>
#include <kernel/vga.h>

mouse_state_t g_mouse = {50, 50, 0, 0, 0};
static uint32_t mouse_screen_width = 800;
static uint32_t mouse_screen_height = 600;
static volatile bool mouse_device_ready;
static volatile u32 mouse_speed_percent = 100u;
static s32 mouse_speed_remainder_x;
static s32 mouse_speed_remainder_y;
static volatile bool mouse_wheel_ready;
static volatile u8 mouse_device_id;
static volatile u8 mouse_packet_size = 3u;
static volatile s32 mouse_scroll_pending_x;
static volatile s32 mouse_scroll_pending_y;

#ifdef MOUSE_HOST_TEST
static u64 mouse_host_irq_state;
static inline u64 mouse_irq_save(void) {
    u64 previous = mouse_host_irq_state;
    mouse_host_irq_state = 1;
    return previous;
}
static inline void mouse_irq_restore(u64 flags) {
    mouse_host_irq_state = flags;
}
#else
static inline u64 mouse_irq_save(void) {
    u64 flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void mouse_irq_restore(u64 flags) {
    asm volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}
#endif

static u32 mouse_normalize_speed_percent(u32 percent) {
    if (percent < MOUSE_SPEED_MIN_PERCENT)
        percent = MOUSE_SPEED_MIN_PERCENT;
    if (percent > MOUSE_SPEED_MAX_PERCENT)
        percent = MOUSE_SPEED_MAX_PERCENT;
    u32 offset = percent - MOUSE_SPEED_MIN_PERCENT;
    u32 steps = (offset + MOUSE_SPEED_STEP_PERCENT / 2u) /
                MOUSE_SPEED_STEP_PERCENT;
    return MOUSE_SPEED_MIN_PERCENT + steps * MOUSE_SPEED_STEP_PERCENT;
}

bool mouse_is_available(void) { return mouse_device_ready; }

u32 mouse_get_speed_percent(void) {
    u64 irq_flags = mouse_irq_save();
    u32 percent = mouse_speed_percent;
    mouse_irq_restore(irq_flags);
    return percent;
}

void mouse_set_speed_percent(u32 percent) {
    u32 normalized = mouse_normalize_speed_percent(percent);
    u64 irq_flags = mouse_irq_save();
    mouse_speed_percent = normalized;
    mouse_speed_remainder_x = 0;
    mouse_speed_remainder_y = 0;
    mouse_irq_restore(irq_flags);
}

bool mouse_wheel_available(void) { return mouse_device_ready && mouse_wheel_ready; }

void mouse_take_scroll_deltas(s32* horizontal, s32* vertical) {
    u64 irq_flags = mouse_irq_save();
    if (horizontal) *horizontal = mouse_scroll_pending_x;
    if (vertical) *vertical = mouse_scroll_pending_y;
    mouse_scroll_pending_x = 0;
    mouse_scroll_pending_y = 0;
    mouse_irq_restore(irq_flags);
}

static void mouse_accumulate_scroll(volatile s32* pending, s32 delta) {
    s64 next = (s64)*pending + (s64)delta;
    if (next > 0x7FFFFFFFLL) next = 0x7FFFFFFFLL;
    if (next < (-0x7FFFFFFFLL - 1LL)) next = -0x7FFFFFFFLL - 1LL;
    *pending = (s32)next;
}

void mouse_set_bounds(uint32_t width, uint32_t height) {
    /* Coordinates are signed 32-bit values. Do not store a bound that cannot
     * be represented by the cursor, or the IRQ path's comparisons wrap. */
    if (width > 0x7FFFFFFFu) width = 0x7FFFFFFFu;
    if (height > 0x7FFFFFFFu) height = 0x7FFFFFFFu;

    u64 irq_flags = mouse_irq_save();
    mouse_screen_width = width;
    mouse_screen_height = height;

    if (!width) g_mouse.x = 0;
    else if (g_mouse.x >= (s32)width) g_mouse.x = (s32)(width - 1);
    if (!height) g_mouse.y = 0;
    else if (g_mouse.y >= (s32)height) g_mouse.y = (s32)(height - 1);
    mouse_irq_restore(irq_flags);
}

/*
 * 8042 controller helpers. The status register's AUX bit distinguishes mouse
 * replies from keyboard scancodes on the shared data port. Every wait is
 * bounded, and no command is written after an input-buffer timeout.
 */
#define PS2_STATUS_PORT       0x64
#define PS2_COMMAND_PORT      0x64
#define PS2_DATA_PORT         0x60
#define PS2_STATUS_OUTPUT     0x01
#define PS2_STATUS_INPUT      0x02
#define PS2_STATUS_AUX        0x20
#define PS2_REPLY_ACK         0xFA
#define PS2_REPLY_RESEND      0xFE
#define PS2_WAIT_LIMIT        4000
#define PS2_DEVICE_RETRIES    3

static bool ps2_wait_write(void) {
    for (volatile u32 i = 0; i < PS2_WAIT_LIMIT; i++) {
        if (!(inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT)) return true;
    }
    return false;
}

static bool ps2_write_command(u8 cmd) {
    if (!ps2_wait_write()) return false;
    outb(PS2_COMMAND_PORT, cmd);
    return true;
}

/* Controller data writes (such as the configuration byte) have no mouse ACK. */
static bool ps2_write_controller_data(u8 value) {
    if (!ps2_wait_write()) return false;
    outb(PS2_DATA_PORT, value);
    return true;
}

/* Before the controller replies, process any keyboard bytes already queued on
 * the shared output port. AUX bytes are stale mouse data and can be discarded. */
static bool ps2_read_controller_data(u8* out) {
    if (!out) return false;
    for (volatile u32 i = 0; i < PS2_WAIT_LIMIT; i++) {
        u8 status = inb(PS2_STATUS_PORT);
        if (!(status & PS2_STATUS_OUTPUT)) continue;
        u8 value = inb(PS2_DATA_PORT);
        if (status & PS2_STATUS_AUX) continue;
        *out = value;
        return true;
    }
    return false;
}

static void ps2_drain_output(void) {
    for (volatile u32 i = 0; i < PS2_WAIT_LIMIT; i++) {
        u8 status = inb(PS2_STATUS_PORT);
        if (!(status & PS2_STATUS_OUTPUT)) return;
        u8 value = inb(PS2_DATA_PORT);
        if (status & PS2_STATUS_AUX) {
            continue;
        }
        keyboard_process_scancode(value);
    }
}

/* Mouse replies are read only when the controller marks the byte as AUX.
 * Keyboard scancodes are decoded and preserved while waiting for the ACK. */
static bool ps2_wait_aux_reply(u8* reply) {
    if (!reply) return false;
    for (volatile u32 i = 0; i < PS2_WAIT_LIMIT; i++) {
        u8 status = inb(PS2_STATUS_PORT);
        if (!(status & PS2_STATUS_OUTPUT)) continue;
        u8 value = inb(PS2_DATA_PORT);
        if (status & PS2_STATUS_AUX) {
            *reply = value;
            return true;
        }
        keyboard_process_scancode(value);
    }
    return false;
}

static bool ps2_write_aux_data(u8 value) {
    for (u32 attempt = 0; attempt < PS2_DEVICE_RETRIES; attempt++) {
        u8 reply = 0;
        if (!ps2_write_command(0xD4) ||
            !ps2_write_controller_data(value) ||
            !ps2_wait_aux_reply(&reply)) {
            return false;
        }
        if (reply == PS2_REPLY_ACK) return true;
        if (reply != PS2_REPLY_RESEND) return false;
    }
    return false;
}

static bool ps2_set_sample_rate(u8 rate) {
    return ps2_write_aux_data(0xF3) && ps2_write_aux_data(rate);
}

static bool ps2_get_device_id(u8* id) {
    if (!id || !ps2_write_aux_data(0xF2)) return false;
    return ps2_wait_aux_reply(id);
}

/* Enter IntelliMouse wheel mode when the device supports it. ID 3 enables
 * vertical wheel packets; ID 4 adds the Explorer horizontal-wheel extension.
 * Any unsupported ID or failed negotiation leaves the caller on 3-byte mode. */
static bool ps2_negotiate_device(u8* id) {
    if (!id ||
        !ps2_set_sample_rate(200u) ||
        !ps2_set_sample_rate(100u) ||
        !ps2_set_sample_rate(80u) ||
        !ps2_get_device_id(id)) return false;

    if (*id == 3u) {
        u8 extended_id = 3u;
        if (ps2_set_sample_rate(200u) &&
            ps2_set_sample_rate(200u) &&
            ps2_set_sample_rate(80u) &&
            ps2_get_device_id(&extended_id) &&
            extended_id == 4u) {
            *id = 4u;
        }
    }
    return true;
}

void mouse_init(void) {
    mouse_device_ready = false;
    /* IRQ12 must stay masked until both the controller and device have
     * acknowledged setup. The handler is already registered by irq_init(). */
    pic_mask_irq(IRQ_MOUSE);
    ps2_drain_output();

    /* Temporarily stop first-port traffic so the controller configuration
     * reply cannot be confused with a keyboard byte on the shared output port.
     * Pending keyboard bytes were drained and decoded immediately above. */
    if (!ps2_write_command(0xAD)) {
        kprintf("Mouse: controller timeout disabling keyboard port\n");
        return;
    }
    if (!ps2_write_command(0xA7)) {
        (void)ps2_write_command(0xAE);
        kprintf("Mouse: controller timeout disabling AUX port\n");
        return;
    }
    ps2_drain_output();

    if (!ps2_write_command(0xA8)) {
        (void)ps2_write_command(0xAE);
        kprintf("Mouse: controller timeout enabling AUX port\n");
        return;
    }

    /* Controller command 0x20 returns one configuration byte, not an ACK.
     * Keyboard traffic is paused, so a non-AUX response here is unambiguous. */
    u8 config = 0;
    if (!ps2_write_command(0x20) || !ps2_read_controller_data(&config)) {
        (void)ps2_write_command(0xAE);
        kprintf("Mouse: controller configuration read failed\n");
        return;
    }

    /* Explicitly enable both interrupt lines and both controller ports.
     * Preserve translation and all unrelated configuration bits. */
    config = (u8)((config | 0x03) & (u8)~0x30);
    if (!ps2_write_command(0x60) ||
        !ps2_write_controller_data(config)) {
        (void)ps2_write_command(0xAE);
        kprintf("Mouse: controller configuration write failed\n");
        return;
    }

    if (!ps2_write_command(0xAE)) {
        kprintf("Mouse: controller timeout restoring keyboard port\n");
        return;
    }

    if (!ps2_write_aux_data(0xF6)) {
        kprintf("Mouse: device did not ACK defaults command\n");
        return;
    }

    u8 negotiated_id = 0u;
    if (!ps2_negotiate_device(&negotiated_id) ||
        (negotiated_id != 0u && negotiated_id != 3u && negotiated_id != 4u)) {
        /* Restore the standard packet format after any incomplete negotiation. */
        (void)ps2_write_aux_data(0xF6);
        negotiated_id = 0u;
    }
    mouse_device_id = negotiated_id;
    mouse_packet_size = negotiated_id >= 3u ? 4u : 3u;
    mouse_wheel_ready = negotiated_id >= 3u;

    if (!ps2_write_aux_data(0xF4)) {
        mouse_wheel_ready = false;
        mouse_device_id = 0u;
        mouse_packet_size = 3u;
        kprintf("Mouse: device did not ACK enable-reporting command\n");
        return;
    }

    mouse_device_ready = true;
    pic_unmask_irq(IRQ_MOUSE);
    kprintf("Mouse: PS/2 enabled, config=0x%02x id=%u packet_bytes=%u\n",
            config, (u32)mouse_device_id, (u32)mouse_packet_size);
}

static u8 mouse_packet[4];
static u8 mouse_packet_index;
static bool mouse_overflow_seen;

/* Consume only bytes the 8042 says are present. IRQ12 normally means AUX, but
 * routing a non-AUX byte through the keyboard decoder is safer than corrupting
 * the mouse packet stream or silently losing a key. */
static void mouse_process_byte(u8 status, u8 byte) {
    if (!(status & PS2_STATUS_OUTPUT)) return;
    if (!(status & PS2_STATUS_AUX)) {
        keyboard_process_scancode(byte);
        return;
    }

    if (mouse_packet_index == 0) {
        /* Bit 3 is fixed in every PS/2 packet header. A stray byte must not
         * shift the next two bytes into movement/button fields. */
        if (!(byte & 0x08u)) return;
        mouse_overflow_seen = (byte & 0xC0u) != 0;
    }

    u8 packet_size = mouse_packet_size;
    if (packet_size != 3u && packet_size != 4u) packet_size = 3u;
    mouse_packet[mouse_packet_index++] = byte;
    if (mouse_packet_index < packet_size) return;
    mouse_packet_index = 0;
    if (mouse_overflow_seen) {
        mouse_overflow_seen = false;
        return;
    }
    mouse_overflow_seen = false;

    u8 flags = mouse_packet[0];
    g_mouse_packets++;
    u8 buttons = flags & 0x07u;
    if (packet_size == 4u && mouse_device_id == 4u)
        buttons |= (u8)((mouse_packet[3] & 0x30u) >> 1);
    g_mouse.buttons = buttons;

    if (packet_size == 4u && mouse_wheel_ready) {
        u8 extra = mouse_packet[3];
        if (mouse_device_id == 4u && (extra & 0x40u)) {
            s32 horizontal = (s32)(extra & 0x3Fu);
            if (horizontal & 0x20) horizontal -= 0x40;
            mouse_accumulate_scroll(&mouse_scroll_pending_x, horizontal);
        } else if (mouse_device_id == 4u) {
            s32 vertical = (s32)(extra & 0x0Fu);
            if (vertical & 0x08) vertical -= 0x10;
            mouse_accumulate_scroll(&mouse_scroll_pending_y, vertical);
        } else {
            mouse_accumulate_scroll(&mouse_scroll_pending_y, (s32)(s8)extra);
        }
    }

    s32 raw_dx = (s32)(s8)mouse_packet[1];
    s32 raw_dy = (s32)(s8)mouse_packet[2];
    u32 speed = mouse_speed_percent;
    if (speed < MOUSE_SPEED_MIN_PERCENT || speed > MOUSE_SPEED_MAX_PERCENT)
        speed = 100u;

    /* Preserve fractional motion between packets so lower speeds do not
     * discard small movements. Signed remainders cancel on direction changes. */
    s64 scaled_x = (s64)raw_dx * (s64)speed + mouse_speed_remainder_x;
    s64 scaled_y = (s64)raw_dy * (s64)speed + mouse_speed_remainder_y;
    s64 adjusted_dx = scaled_x / 100;
    s64 adjusted_dy = scaled_y / 100;
    mouse_speed_remainder_x = (s32)(scaled_x - adjusted_dx * 100);
    mouse_speed_remainder_y = (s32)(scaled_y - adjusted_dy * 100);
    g_mouse.dx = (s32)adjusted_dx;
    g_mouse.dy = (s32)adjusted_dy;

    s64 next_x = (s64)g_mouse.x + (s64)g_mouse.dx;
    s64 next_y = (s64)g_mouse.y - (s64)g_mouse.dy;
    if (!mouse_screen_width) next_x = 0;
    else if (next_x < 0) next_x = 0;
    else if (next_x >= (s64)mouse_screen_width)
        next_x = (s64)mouse_screen_width - 1;
    if (!mouse_screen_height) next_y = 0;
    else if (next_y < 0) next_y = 0;
    else if (next_y >= (s64)mouse_screen_height)
        next_y = (s64)mouse_screen_height - 1;

    g_mouse.x = (s32)next_x;
    g_mouse.y = (s32)next_y;
}

#ifdef MOUSE_HOST_TEST
void mouse_test_process_byte(u8 status, u8 byte) {
    mouse_process_byte(status, byte);
}

void mouse_test_set_device_id(u8 id) {
    u64 flags = mouse_irq_save();
    mouse_device_id = id;
    mouse_device_ready = true;
    mouse_packet_size = (id == 3u || id == 4u) ? 4u : 3u;
    mouse_wheel_ready = id >= 3u;
    mouse_packet_index = 0u;
    mouse_overflow_seen = false;
    mouse_scroll_pending_x = 0;
    mouse_scroll_pending_y = 0;
    mouse_irq_restore(flags);
}
#endif

void mouse_handler(void) {
    u8 status = inb(PS2_STATUS_PORT);
    if (!(status & PS2_STATUS_OUTPUT)) return;
    u8 byte = inb(PS2_DATA_PORT);
    mouse_process_byte(status, byte);
}
bool mouse_has_input(void) {
    mouse_state_t mouse;
    mouse_get_snapshot(&mouse);
    return mouse.dx != 0 || mouse.dy != 0 || mouse.buttons != 0;
}

int mouse_get_movement(int* dx, int* dy) {
    u64 flags = mouse_irq_save();
    if (dx) *dx = g_mouse.dx;
    if (dy) *dy = g_mouse.dy;
    g_mouse.dx = 0;
    g_mouse.dy = 0;
    mouse_irq_restore(flags);
    return 0;
}

int mouse_get_buttons(int* buttons) {
    if (buttons) {
        u64 flags = mouse_irq_save();
        *buttons = g_mouse.buttons;
        mouse_irq_restore(flags);
    }
    return 0;
}

/* Counters and accessors for the shell's input diagnostics. Without these,
 * "the pointer is stuck" is indistinguishable from "no packets ever arrived".
 */
volatile u32 g_mouse_packets = 0;

u32 mouse_packet_count(void) { return g_mouse_packets; }

/* IRQ12 is the sole writer; briefly mask local interrupts while copying so
 * callers never observe coordinates and button bits from different packets. */
void mouse_get_snapshot(mouse_state_t* out) {
    if (!out) return;

    u64 flags = mouse_irq_save();
    out->x = g_mouse.x;
    out->y = g_mouse.y;
    out->dx = g_mouse.dx;
    out->dy = g_mouse.dy;
    out->buttons = g_mouse.buttons;
    mouse_irq_restore(flags);
}

mouse_state_t* mouse_get_state(void) { return &g_mouse; }
