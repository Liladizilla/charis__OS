#include <kernel/il_runtime.h>
#include <kernel/il_runtime.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/string.h>

il_runtime_t il_runtimes[TASK_MAX_TASKS];
static usize il_runtime_count = 0;

il_runtime_t* il_create(void) {
    if (il_runtime_count >= TASK_MAX_TASKS) return NULL;
    
    il_runtime_t* rt = &il_runtimes[il_runtime_count++];
    kmemset(rt, 0, sizeof(il_runtime_t));
    rt->sp = IL_MAX_STACK;
    rt->running = false;
    
    return rt;
}

void il_destroy(il_runtime_t* rt) {
    if (rt) rt->running = false;
}

int il_load_program(il_runtime_t* rt, il_instruction_t* prog, usize len) {
    if (!rt || !prog || len > IL_MAX_INSTRUCTIONS) return -1;
    
    for (usize i = 0; i < len; i++) {
        rt->instructions[i] = prog[i];
    }
    return 0;
}

int il_run(il_runtime_t* rt) {
    if (!rt) return -1;
    
    rt->running = true;
    rt->ip = 0;
    rt->sp = IL_MAX_STACK;
    
    while (rt->running && rt->ip < IL_MAX_INSTRUCTIONS) {
        il_instruction_t* instr = &rt->instructions[rt->ip++];
        
        switch (instr->opcode) {
            case IL_NOP:
                break;
            case IL_LOADI:
                if (rt->sp > 0) {
                    rt->stack[--rt->sp] = instr->operand;
                }
                break;
            case IL_ADD:
                if (rt->sp < IL_MAX_STACK - 1) {
                    u64 b = rt->stack[rt->sp++];
                    u64 a = rt->stack[rt->sp++];
                    rt->stack[--rt->sp] = a + b;
                }
                break;
            case IL_PRINT:
                if (rt->sp < IL_MAX_STACK) {
                    char buf[32];
                    ksprintf(buf, "%llu", (unsigned long long)rt->stack[rt->sp++]);
                    vga_puts(buf);
                }
                break;
            case IL_HALT:
                rt->running = false;
                break;
            default:
                vga_puts("Unknown IL op\n");
                rt->running = false;
                break;
        }
    }
    
    return 0;
}

void il_reset(il_runtime_t* rt) {
    if (rt) {
        rt->ip = 0;
        rt->sp = IL_MAX_STACK;
        rt->running = false;
    }
}

int il_compile_source(const char* source, il_instruction_t* out) {
    // Simple tokenizer for demonstration
    usize ip = 0;
    for (usize i = 0; source[i] && ip < IL_MAX_INSTRUCTIONS - 1; i++) {
        if (source[i] == '1' && source[i+1] == '+' && source[i+2] == '2') {
            out[ip++] = (il_instruction_t){IL_LOADI, 1};
            out[ip++] = (il_instruction_t){IL_LOADI, 2};
            out[ip++] = (il_instruction_t){IL_ADD, 0};
            out[ip++] = (il_instruction_t){IL_PRINT, 0};
            out[ip++] = (il_instruction_t){IL_HALT, 0};
            break;
        }
    }
    return ip;
}

int il_load_bytecode(const char* path, il_instruction_t* out) {
    // Would read compiled IL bytecode from disk
    return 0;
}