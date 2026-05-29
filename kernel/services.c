#include <kernel/services.h>
#include <kernel/task.h>
#include <kernel/scheduler.h>
#include <kernel/vga.h>
#include <kernel/printf.h>

static service_t services[SERVICE_MAX];
static int service_count = 0;

void services_init(void) {
    for (int i = 0; i < SERVICE_MAX; i++) {
        services[i].state = SERVICE_STOPPED;
        services[i].task = NULL;
        services[i].name[0] = 0;
    }
    service_count = 0;
    vga_puts("Service manager initialized\n");
}

int service_register(const char* name, u32 capabilities, bool auto_restart) {
    if (service_count >= SERVICE_MAX) return -1;
    
    service_t* svc = &services[service_count];
    kstrncpy(svc->name, name, SERVICE_NAME_MAX - 1);
    svc->capabilities = capabilities;
    svc->state = SERVICE_STOPPED;
    svc->task = NULL;
    svc->auto_restart = auto_restart;
    
    return service_count++;
}

void service_start(int service_id) {
    if (service_id < 0 || service_id >= service_count) return;
    
    service_t* svc = &services[service_id];
    if (svc->state == SERVICE_RUNNING) return;
    
    // Would create task for actual service
    svc->state = SERVICE_RUNNING;
    kprintf("Service '%s' started\n", svc->name);
}

void service_stop(int service_id) {
    if (service_id < 0 || service_id >= service_count) return;
    
    service_t* svc = &services[service_id];
    svc->state = SERVICE_STOPPED;
    kprintf("Service '%s' stopped\n", svc->name);
}

void service_list(void) {
    kprintf("System Services (%d total):\n", service_count);
    for (int i = 0; i < service_count; i++) {
        kprintf("  [%d] %s: %s%s\n", i, services[i].name,
                services[i].state == SERVICE_RUNNING ? "RUNNING" : "STOPPED",
                services[i].auto_restart ? " (auto)" : "");
    }
}

void service_monitor(void) {
    // Periodically check and restart crashed services
    for (int i = 0; i < service_count; i++) {
        if (services[i].auto_restart && services[i].state == SERVICE_STOPPED) {
            service_start(i);
        }
    }
}

int service_find(const char* name) {
    for (int i = 0; i < service_count; i++) {
        if (kstrcmp(services[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}