bits 64

global _start

section .text
_start:
    cmp qword [rsp], 2
    jne .bad_argc
    mov rax, [rsp + 16]
    cmp byte [rax], 'A'
    jne .bad_argv
    mov rax, [rsp + 32]
    cmp byte [rax], 'E'
    jne .bad_envp
    mov rax, 99
    lea rdi, [rel message]
    int 0x80
    jmp .loop

.bad_argc:
    mov rax, 99
    lea rdi, [rel bad_argc_message]
    int 0x80
    jmp .loop

.bad_argv:
    mov rax, 99
    lea rdi, [rel bad_argv_message]
    int 0x80
    jmp .loop

.bad_envp:
    mov rax, 99
    lea rdi, [rel bad_envp_message]
    int 0x80

.loop:
    mov rax, 98
    int 0x80
    jmp .loop

section .rodata
message: db "ELF_EXEC_OK", 10, 0
bad_argc_message: db "ELF_BAD_ARGC", 10, 0
bad_argv_message: db "ELF_BAD_ARGV", 10, 0
bad_envp_message: db "ELF_BAD_ENVP", 10, 0
