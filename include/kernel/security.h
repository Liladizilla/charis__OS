/* security.h - Security framework for CharisOS */
#pragma once
#include <kernel/types.h>
#include <kernel/task.h>

// Capability-based security (consolidated from task.h)
#define CAP_FS_READ     (1 << 0)
#define CAP_FS_WRITE    (1 << 1)
#define CAP_FS_CREATE   (1 << 2)
#define CAP_FS_DELETE   (1 << 3)
#define CAP_SPAWN       (1 << 4)
#define CAP_KILL        (1 << 5)
#define CAP_RAW_MEM     (1 << 6)
#define CAP_SHUTDOWN    (1 << 7)
#define CAP_SERIAL      (1 << 8)
#define CAP_NET_RAW     (1 << 9)
#define CAP_SYS_ADMIN   (1 << 10)
#define CAP_ALLOW_EXEC  (1 << 11)
#define CAP_DENY_EXEC   (1 << 12)
#define CAP_ALL         0xFFFFFFFF

#define SECURITY_MAX_CAPS         32
#define SECURITY_TOKEN_SIZE       32

typedef struct {
    u8 token[SECURITY_TOKEN_SIZE];
    u32 process_caps;
    u64 parent_token;
    bool is_privileged;
} security_context_t;

extern uintptr_t __stack_chk_guard;

void security_init(void);
bool security_check_capability(task_t* task, u32 cap);
void security_enforce_capabilities(task_t* task);
void security_audit(const char* action, task_t* task);
bool security_verify_path(const char* path, task_t* task);
int security_generate_token(u8* out_token);