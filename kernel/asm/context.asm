; context.asm
; Low-level context switch and task trampolines for CharisOS.

section .text
bits 64

; ---------------------------------------------------------------------------
; void context_switch(u64* old_rsp, u64 new_rsp, bool is_user);
; Saves callee-saved regs, switches stacks, restores callee-saved regs.
; Used for cooperative task switching (yield).
; ---------------------------------------------------------------------------
global context_switch
context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp          ; Save old stack pointer
    mov rsp, rsi            ; Load new stack pointer

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ; Check if user task
    cmp rdx, 1
    je .iretq
    ret
.iretq:
    iretq

; ---------------------------------------------------------------------------
; void task_trampoline(void);
; Called when a new task begins execution via context_switch.
; Expects the task entry point to be pushed on the stack before switch.
; ---------------------------------------------------------------------------
global task_trampoline
task_trampoline:
    pop rax                 ; Pop entry point
    pop rdi                 ; Pop task argument
    call rax
    ; If task returns, call exit handler
    call task_exit_handler
    hlt

section .text
extern task_exit_handler

