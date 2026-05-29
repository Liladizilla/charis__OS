#include <kernel/power.h>
#include <kernel/timer.h>
#include <kernel/vga.h>
#include <kernel/printf.h>

static power_stats_t power_stats = {0};
static power_state_t current_state = POWER_STATE_RUNNING;

void power_init(void) {
    power_stats.uptime_ms = 0;
    power_stats.idle_ticks = 0;
    power_stats.cpu_usage_percent = 100;
    power_stats.wake_events = 0;
    vga_puts("Power: Management initialized\n");
}

void power_idle_enter(void) {
    if (current_state == POWER_STATE_RUNNING) {
        power_stats.idle_ticks++;
        asm volatile("hlt"); // Enter CPU idle state
    }
}

void power_idle_exit(void) {
    if (current_state != POWER_STATE_RUNNING) {
        current_state = POWER_STATE_RUNNING;
        power_stats.wake_events++;
    }
}

void power_set_state(power_state_t state) {
    current_state = state;
    switch (state) {
        case POWER_STATE_SLEEP:
            vga_puts_info("Power: Entering sleep mode");
            break;
        case POWER_STATE_HIBERNATE:
            vga_puts_info("Power: Hibernating (not fully implemented)");
            break;
        case POWER_STATE_SHUTDOWN:
            vga_puts_info("Power: Shutting down");
            break;
        default:
            break;
    }
}

void power_tick(void) {
    power_stats.uptime_ms = timer_get_ms();
    // Calculate CPU usage based on idle vs total ticks
}

power_stats_t power_get_stats(void) {
    return power_stats;
}