; context.asm
; Low-level context switch and task trampolines for CharisOS.

section .text
bits 64

extern syscall_dispatch
extern task_exit_handler

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

    mov [rdi], rsp
    mov rsp, rsi

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
    pop rax
    pop rdi
    call rax
    call task_exit_handler
    hlt

; ---------------------------------------------------------------------------
; Syscall entry point (called via SYSCALL instruction)
; SYSCALL saves RCX, R11. We push syscall number in RAX -> RDI for dispatcher.
; ---------------------------------------------------------------------------
global syscall_entry
syscall_entry:
    ; Save user rsp in GS:0x8, load kernel stack from GS:0x0
    swapgs
    mov [gs:0x8], rsp
    mov rsp, [gs:0x0]

    ; Save all registers
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    ; Call dispatcher (syscall number already in RAX, will move to RDI)
    mov rdi, rax
    call syscall_dispatch

    ; Restore registers
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    ; Restore user rsp and return
    mov rsp, [gs:0x8]
    swapgs
    ; NASM needs explicit 64-bit operand size for sysret
    o64 sysret