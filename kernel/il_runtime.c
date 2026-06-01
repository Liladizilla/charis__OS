#include <kernel/il_runtime.h>
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
            case IL_SUB:
                if (rt->sp < IL_MAX_STACK - 1) {
                    u64 b = rt->stack[rt->sp++];
                    u64 a = rt->stack[rt->sp++];
                    rt->stack[--rt->sp] = a - b;
                }
                break;
            case IL_MUL:
                if (rt->sp < IL_MAX_STACK - 1) {
                    u64 b = rt->stack[rt->sp++];
                    u64 a = rt->stack[rt->sp++];
                    rt->stack[--rt->sp] = a * b;
                }
                break;
            case IL_PRINT:
                if (rt->sp < IL_MAX_STACK) {
                    char buf[32];
                    kultoa(rt->stack[rt->sp++], buf, 10);
                    vga_puts(buf);
                }
                break;
            case IL_HALT:
                rt->running = false;
                break;
            case IL_EQ:
                if (rt->sp < IL_MAX_STACK - 1) {
                    u64 b = rt->stack[rt->sp++];
                    u64 a = rt->stack[rt->sp++];
                    rt->stack[--rt->sp] = (a == b) ? 1 : 0;
                }
                break;
            case IL_JMP:
                rt->ip = (usize)instr->operand;
                break;
            case IL_JZ:
                if (rt->sp < IL_MAX_STACK && rt->stack[rt->sp++] == 0) {
                    rt->ip = (usize)instr->operand;
                }
                break;
            case IL_CALL:
                if (rt->sp > 0) {
                    rt->stack[--rt->sp] = rt->ip;
                }
                rt->ip = (usize)instr->operand;
                break;
            case IL_RET:
                if (rt->sp < IL_MAX_STACK) {
                    rt->ip = rt->stack[rt->sp++];
                } else {
                    rt->running = false;
                }
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
    if (!source || !out) return 0;

    usize ip = 0;
    usize i = 0;

    while (source[i] && ip < IL_MAX_INSTRUCTIONS - 1) {
        if (source[i] == ' ' || source[i] == '\n' || source[i] == '\t') {
            i++;
            continue;
        }

        if (source[i] >= '0' && source[i] <= '9') {
            usize start = i;
            while (source[i] >= '0' && source[i] <= '9') i++;
            out[ip++] = (il_instruction_t){IL_LOADI, katoi(source + start, 10)};
            continue;
        }

        if (source[i] == '+') {
            out[ip++] = (il_instruction_t){IL_ADD, 0};
            i++;
            continue;
        }

        if (source[i] == '-') {
            out[ip++] = (il_instruction_t){IL_SUB, 0};
            i++;
            continue;
        }

        if (source[i] == '*') {
            out[ip++] = (il_instruction_t){IL_MUL, 0};
            i++;
            continue;
        }

        if (source[i] == '=') {
            if (source[i+1] == '=') {
                out[ip++] = (il_instruction_t){IL_EQ, 0};
                i += 2;
            }
            continue;
        }

        if (source[i] == 'j' || source[i] == 'J') {
            i++;
            if (source[i] == 'z' || source[i] == 'Z') {
                out[ip++] = (il_instruction_t){IL_JZ, 0};
                i++;
            } else if (source[i] == 'm' || source[i] == 'M') {
                usize target = 0;
                while (source[i] >= '0' && source[i] <= '9') {
                    target = target * 10 + (source[i] - '0');
                    i++;
                }
                out[ip++] = (il_instruction_t){IL_JMP, target};
            }
            continue;
        }

        if (source[i] == 'p' || source[i] == 'P') {
            out[ip++] = (il_instruction_t){IL_PRINT, 0};
            i++;
            continue;
        }

        if (source[i] == 'h' || source[i] == 'H') {
            out[ip++] = (il_instruction_t){IL_HALT, 0};
            i++;
            continue;
        }

        i++;
    }

    return ip;
}

int il_load_bytecode(const char* path, il_instruction_t* out) {
    if (!path || (u64)path >= 0xFFFF800000000000ULL) return -1;

    vfs_node_t* node = vfs_resolve(path);
    if (!node) return -1;

    usize bytes_read = 0;
    if (node->read) {
        bytes_read = node->read(node, 0, IL_MAX_INSTRUCTIONS * sizeof(il_instruction_t), (u8*)out);
    }

    return (int)bytes_read;
}