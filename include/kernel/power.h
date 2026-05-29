/* power.h - Power management for CharisOS */
#pragma once
#include <kernel/types.h>

typedef enum {
    POWER_STATE_RUNNING,
    POWER_STATE_IDLE,
    POWER_STATE_SLEEP,
    POWER_STATE_HIBERNATE,
    POWER_STATE_SHUTDOWN
} power_state_t;

typedef struct {
    u64 uptime_ms;
    u32 idle_ticks;
    u32 cpu_usage_percent;
    u32 wake_events;
} power_stats_t;

void power_init(void);
void power_idle_enter(void);
void power_idle_exit(void);
void power_set_state(power_state_t state);
void power_tick(void);
power_stats_t power_get_stats(void);