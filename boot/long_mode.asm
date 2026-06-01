; long_mode.asm
; 64-bit entry point after bootloader sets up paging and GDT.

section .text
bits 64
global long_mode_start
extern kernel_main
extern gdt64
extern mb_magic
extern mb_info
extern stack_top
extern tss

long_mode_start:
    ; Debug: indicate we are in long mode
    mov dx, 0x3F8
    mov al, '['   ; Start marker
    out dx, al

    ; Load data segment selectors (GDT offsets: code=0x08, data=0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Clear unused general-purpose registers (clean state)
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

     ; Set up stack
     mov rsp, stack_top
     ; Set TSS RSP0 to kernel stack
     mov qword [rel tss + 4], rsp

    ; Pass multiboot magic and info to kernel_main
    mov edi, dword [rel mb_magic]
    mov esi, dword [rel mb_info]

    ; Debug: indicate we are about to call kernel
    mov dx, 0x3F8
    mov al, 'L'
    out dx, al

    ; Debug: about to call
    mov dx, 0x3F8
    mov al, '>'
    out dx, al

    ; Jump to C kernel
    call kernel_main

    ; If we get here, kernel_main returned (shouldn't happen)
    mov dx, 0x3F8
    mov al, '<'
    out dx, al

    ; If kernel_main returns, halt forever
.halt:
    cli
    hlt
    jmp .halt

