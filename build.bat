@echo off
setlocal enabledelayedexpansion
cd /d %~dp0

rem Build helper for CharisOS on Windows with NASM/GCC/ld installed.
if "%1"=="clean" goto clean

mkdir build 2>nul

nasm -f elf64 boot\boot.asm -o build\boot.o
nasm -f elf64 boot\long_mode.asm -o build\long_mode.o
nasm -f elf64 kernel\asm\interrupt_stubs.asm -o build\interrupt_stubs.o
nasm -f elf64 kernel\asm\context.asm -o build\context.o
nasm -f elf64 kernel\asm\gdt.asm -o build\gdt.o
nasm -f elf64 kernel\asm\io.asm -o build\io.o

gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel\main.c -o build\main.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel\vga.c -o build\vga.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel\serial.c -o build\serial.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel\memory.c -o build\memory.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel\idt.c -o build\idt.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel\irq.c -o build\irq.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel\timer.c -o build\timer.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel\keyboard.c -o build\keyboard.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel\syscall.c -o build\syscall.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel\task.c -o build\task.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel\scheduler.c -o build\scheduler.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel\shell.c -o build\shell.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel\il_runtime.c -o build\il_runtime.o

ld -T link.ld -nostdlib -z max-page-size=0x1000 -z noexecstack -o build\kernel.elf build\boot.o build\long_mode.o build\main.o build\vga.o build\serial.o build\memory.o build\idt.o build\irq.o build\timer.o build\keyboard.o build\syscall.o build\task.o build\scheduler.o build\shell.o build\il_runtime.o build\interrupt_stubs.o build\context.o build\gdt.o build\io.o

if not exist iso\boot\grub mkdir iso\boot\grub
copy /Y build\kernel.elf iso\boot\kernel.elf >nul

grub-mkrescue -o build\charisos.iso iso

echo Build complete. Output: build\charisos.iso
exit /b 0

:clean
rmdir /s /q build 2>nul
if exist iso\boot\kernel.elf del /q iso\boot\kernel.elf
if exist build\charisos.iso del /q build\charisos.iso
exit /b 0
