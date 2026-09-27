/* timer.h - PIT timer driver */
#pragma once
#include <kernel/types.h>
#include <kernel/idt.h>

#define PIT_FREQ        1193182
#define PIT_CHANNEL0    0x40
#define PIT_COMMAND     0x43

void timer_init(u32 frequency_hz);
void timer_handler(reg_frame_t* frame);
u64 timer_get_ticks(void);
u64 timer_get_ms(void);
void timer_sleep_ms(u32 ms);

