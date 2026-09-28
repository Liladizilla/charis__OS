/* keyboard.h - PS/2 keyboard driver */
#pragma once
#include <kernel/types.h>
#include <kernel/idt.h>

#define KEYBOARD_DATA_PORT      0x60
#define KEYBOARD_STATUS_PORT    0x64
#define KEYBOARD_COMMAND_PORT   0x64

#define KEYBOARD_STATUS_OUTBUF  0x01
#define KEYBOARD_STATUS_INBUF   0x02

void keyboard_init(void);
void keyboard_handler(reg_frame_t* frame);
bool keyboard_get_key(char* out);
bool keyboard_has_input(void);
void keyboard_read_line(char* buffer, usize max_len);

/* Raw scancode sink, for keys that have no ASCII character: the Windows/Super
 * key, arrows, function keys, Escape. Called from the IRQ handler, so keep it
 * short and non-blocking. Extended keys are reported with bit 7 set in the
 * code so left/right can be told apart; `pressed` is false for the release
 * that carries bit 7 of the original scancode. */
void keyboard_set_key_event_hook(void (*hook)(u8 scancode, bool pressed));

/* Scancode set 1, named because a graphical shell needs them by name. */
#define KEY_UP        0x48
#define KEY_DOWN      0x50
#define KEY_LEFT      0x4B
#define KEY_RIGHT     0x4D
#define KEY_ENTER     0x1C
#define KEY_ESCAPE    0x01
#define KEY_TAB       0x0F
#define KEY_BACKSPACE 0x0E
#define KEY_LSUPER    0x5B
#define KEY_RSUPER    0x5C
#define KEY_LALT      0x38
#define KEY_SPACE     0x39

/* Simple scancode to ASCII (US QWERTY, set 1) */
extern const char keyboard_scancode_set1[128];
extern const char keyboard_scancode_set1_shift[128];

