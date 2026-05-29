/* il_runtime.h - CharisOS Intermediate Language runtime */
#pragma once
#include <kernel/types.h>

#define IL_MAX_METHODS    256
#define IL_MAX_STACK      1024
#define IL_MAX_INSTRUCTIONS 4096

// OpCodes
#define IL_NOP       0x00
#define IL_LOADI     0x01
#define IL_LOAD     0x02
#define IL_STORE    0x03
#define IL_ADD      0x04
#define IL_SUB      0x05
#define IL_MUL      0x06
#define IL_DIV      0x07
#define IL_MOD      0x08
#define IL_EQ       0x09
#define IL_NE       0x0A
#define IL_JMP      0x0B
#define IL_JZ       0x0C
#define IL_JNZ      0x0D
#define IL_CALL     0x0E
#define IL_RET      0x0F
#define IL_PRINT    0x10
#define IL_HALT     0x11

typedef struct {
    u8 opcode;
    u64 operand;
} il_instruction_t;

typedef struct {
    il_instruction_t instructions[IL_MAX_INSTRUCTIONS];
    usize ip; // instruction pointer
    u64 stack[IL_MAX_STACK];
    usize sp; // stack pointer
    u64 globals[256];
    bool running;
} il_runtime_t;

// Runtime functions
il_runtime_t* il_create(void);
void il_destroy(il_runtime_t* rt);
int il_load_program(il_runtime_t* rt, il_instruction_t* prog, usize len);
int il_run(il_runtime_t* rt);
void il_reset(il_runtime_t* rt);

// Bytecode management
int il_compile_source(const char* source, il_instruction_t* out);
int il_load_bytecode(const char* path, il_instruction_t* out);