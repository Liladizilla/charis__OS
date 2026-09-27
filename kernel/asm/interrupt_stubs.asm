; interrupt_stubs.asm
; ISR stubs for all 256 x86_64 interrupts.
; Each stub pushes vector number (+ dummy error code if needed),
; then jumps to a common handler that saves all GPRs and calls C.

section .text
bits 64

extern idt_dispatch_handler

; Macro for ISRs without error code
%macro isr_noerr 1
global isr_%1
isr_%1:
    push 0              ; Dummy error code
    push %1             ; Vector number
    jmp isr_common
%endmacro

; Macro for ISRs with error code (CPU pushes it automatically)
%macro isr_err 1
global isr_%1
isr_%1:
    push %1             ; Vector number (error code already on stack)
    jmp isr_common
%endmacro

; Generate stubs 0-31 (CPU exceptions)
isr_noerr 0
isr_noerr 1
isr_noerr 2
isr_noerr 3
isr_noerr 4
isr_noerr 5
isr_noerr 6
isr_noerr 7
isr_err   8
isr_noerr 9
isr_err   10
isr_err   11
isr_err   12
isr_err   13
isr_err   14
isr_noerr 15
isr_noerr 16
isr_err   17
isr_noerr 18
isr_noerr 19
isr_noerr 20
isr_noerr 21
isr_noerr 22
isr_noerr 23
isr_noerr 24
isr_noerr 25
isr_noerr 26
isr_noerr 27
isr_noerr 28
isr_noerr 29
isr_err   30
isr_noerr 31

; Generate stubs 32-255 (IRQs and software interrupts)
%assign i 32
%rep 224
isr_noerr i
%assign i i+1
%endrep

; ---------------------------------------------------------------------------
; Common handler: save all GPRs, call C dispatcher, restore, iretq
; ---------------------------------------------------------------------------
isr_common:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp                ; First arg = pointer to register frame
    call idt_dispatch_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    add rsp, 16                 ; Remove vector number and error code
    iretq

; ---------------------------------------------------------------------------
; Table of ISR handlers for C to load into IDT
; ---------------------------------------------------------------------------
section .data
align 8
global isr_table
isr_table:
%assign i 0
%rep 256
    dq isr_%+i
%assign i i+1
%endrep

