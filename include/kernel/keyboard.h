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

/* Simple scancode to ASCII (US QWERTY, set 1) */
extern const char keyboard_scancode_set1[128];
extern const char keyboard_scancode_set1_shift[128];

