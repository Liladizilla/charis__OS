#include <kernel/timer.h>
#include <kernel/irq.h>
#include <kernel/vga.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>

static u64 timer_ticks = 0;
static u32 timer_frequency_hz = 0;

void timer_init(u32 frequency_hz) {
    timer_frequency_hz = frequency_hz;
    u32 divisor = PIT_FREQ / frequency_hz;

    // Command: Channel 0, lobyte/hibyte, rate generator
    asm volatile("outb %0, %1" : : "a"((u8)0x36), "Nd"((u16)PIT_COMMAND));

    // Set divisor
    asm volatile("outb %0, %1" : : "a"((u8)(divisor & 0xFF)), "Nd"((u16)PIT_CHANNEL0));
    asm volatile("outb %0, %1" : : "a"((u8)((divisor >> 8) & 0xFF)), "Nd"((u16)PIT_CHANNEL0));

    // Register IRQ handler
    irq_register_handler(IRQ_TIMER, timer_handler);

    vga_puts("Timer initialized\n");
}

void timer_handler(reg_frame_t* frame) {
    (void)frame;
    timer_ticks++;
    
    // Preemption: decrement quantum and reschedule if expired
    task_t* cur = scheduler_current();
    if (cur) {
        if (cur->remaining_quantum > 0) {
            cur->remaining_quantum--;
        }
        if (cur->remaining_quantum == 0) {
            cur->remaining_quantum = TASK_DEFAULT_QUANTUM;
            scheduler_schedule();
        }
    }
    pic_send_eoi(IRQ_TIMER);
}

u64 timer_get_ticks(void) {
    return timer_ticks;
}

u64 timer_get_ms(void) {
    // Fixed formula: divide by frequency_hz, not PIT_FREQ/1000
    return timer_ticks * 1000ULL / timer_frequency_hz;
}

void timer_sleep_ms(u32 ms) {
    u64 start = timer_get_ms();
    // Use hlt which blocks until next interrupt
    while (timer_get_ms() - start < ms) {
        asm volatile("hlt");
    }
}
