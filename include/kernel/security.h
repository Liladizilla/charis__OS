/* security.h - Security framework for CharisOS */
#pragma once
#include <kernel/types.h>

// Capability-based security
#define SECURITY_MAX_CAPS         32
#define SECURITY_TOKEN_SIZE       32

typedef enum {
    CAP_NET_RAW = (1 << 9),
    CAP_SYS_ADMIN = (1 << 10),
    CAP_ALLOW_EXEC = (1 << 11),
    CAP_DENY_EXEC = (1 << 12)
} security_cap_t;

typedef struct {
    u8 token[SECURITY_TOKEN_SIZE];
    u32 process_caps;
    u64 parent_token;
    bool is_privileged;
} security_context_t;

void security_init(void);
bool security_check_capability(task_t* task, u32 cap);
void security_enforce_capabilities(task_t* task);
void security_audit(const char* action, task_t* task);
bool security_verify_path(const char* path, task_t* task);
int security_generate_token(u8* out_token);