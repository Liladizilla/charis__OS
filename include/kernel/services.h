/* services.h - System services for CharisOS */
#pragma once
#include <kernel/types.h>

#define SERVICE_MAX        16
#define SERVICE_NAME_MAX   32

typedef enum {
    SERVICE_STOPPED,
    SERVICE_RUNNING,
    SERVICE_PAUSED
} service_state_t;

typedef struct {
    char name[SERVICE_NAME_MAX];
    u32 capabilities;
    service_state_t state;
    task_t* task;
    bool auto_restart;
} service_t;

void services_init(void);
int service_register(const char* name, u32 capabilities, bool auto_restart);
void service_start(int service_id);
void service_stop(int service_id);
void service_list(void);
void service_monitor(void);
int service_find(const char* name);