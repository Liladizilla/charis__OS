; io.asm
; Port I/O helpers for x86_64.

section .text
bits 64
global outb
global inb
global outw
global inw
global outl
global inl
global io_wait

; void outb(u16 port, u8 value);
outb:
    mov al, sil
    mov dx, di
    out dx, al
    ret

; u8 inb(u16 port);
inb:
    mov dx, di
    in al, dx
    ret

; void outw(u16 port, u16 value);
outw:
    mov ax, si
    mov dx, di
    out dx, ax
    ret

; u16 inw(u16 port);
inw:
    mov dx, di
    in ax, dx
    ret

; void outl(u16 port, u32 value);
outl:
    mov eax, esi
    mov dx, di
    out dx, eax
    ret

; u32 inl(u16 port);
inl:
    mov dx, di
    in eax, dx
    ret

; void io_wait(void);
io_wait:
    out 0x80, al
    ret

; void io_delay(void);
global io_delay
io_delay:
    push rcx
    mov rcx, 1000
.delay_loop:
    out 0x80, al
    loop .delay_loop
    pop rcx
    ret

