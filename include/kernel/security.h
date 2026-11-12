/* security.h - Security framework for CharisOS */
#pragma once
#include <kernel/types.h>
#include <kernel/task.h>

// Capability-based security (consolidated from task.h)
#define CAP_FS_READ     (1U << 0)
#define CAP_FS_WRITE    (1U << 1)
#define CAP_FS_CREATE   (1U << 2)
#define CAP_FS_DELETE   (1U << 3)
#define CAP_SPAWN       (1U << 4)
#define CAP_KILL        (1U << 5)
#define CAP_RAW_MEM     (1U << 6)
#define CAP_SHUTDOWN    (1U << 7)
#define CAP_SERIAL      (1U << 8)
#define CAP_NET_RAW     (1U << 9)
#define CAP_SYS_ADMIN   (1U << 10)
#define CAP_ALLOW_EXEC  (1U << 11)
#define CAP_DENY_EXEC   (1U << 12)
/* Only these thirteen authority bits are currently defined by the kernel. */
#define CAP_ALL         ((1U << 13) - 1U)

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
/* All requested bits must be present; zero and unknown masks are rejected. */
bool security_check_capability(task_t* task, u32 cap);
void security_enforce_capabilities(task_t* task);
void security_audit(const char* action, task_t* task);
bool security_verify_path(const char* path, task_t* task);
int security_generate_token(u8* out_token);