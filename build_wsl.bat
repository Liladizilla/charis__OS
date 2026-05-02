@echo off
setlocal enabledelayedexpansion
cd /d %~dp0

rem Build helper for CharisOS on Windows using WSL with NASM/GCC/ld installed.
if "%1"=="clean" goto clean

rem Get current directory path in WSL format
for /f "delims=" %%i in ('wsl wslpath "%CD%"') do set WSLPATH=%%i

mkdir build 2>nul

rem Assemble boot files using WSL
wsl nasm -f elf64 !WSLPATH!/boot/boot.asm -o !WSLPATH!/build/boot.o
wsl nasm -f elf64 !WSLPATH!/boot/long_mode.asm -o !WSLPATH!/build/long_mode.o
wsl nasm -f elf64 !WSLPATH!/kernel/asm/interrupt_stubs.asm -o !WSLPATH!/build/interrupt_stubs.o
wsl nasm -f elf64 !WSLPATH!/kernel/asm/context.asm -o !WSLPATH!/build/context.o
wsl nasm -f elf64 !WSLPATH!/kernel/asm/gdt.asm -o !WSLPATH!/build/gdt.o
wsl nasm -f elf64 !WSLPATH!/kernel/asm/io.asm -o !WSLPATH!/build/io.o

rem Compile kernel files using WSL - added -fno-pie and -fno-PIC to avoid PIC mode issues
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/main.c -o !WSLPATH!/build/main.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/vga.c -o !WSLPATH!/build/vga.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/serial.c -o !WSLPATH!/build/serial.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/memory.c -o !WSLPATH!/build/memory.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/idt.c -o !WSLPATH!/build/idt.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/irq.c -o !WSLPATH!/build/irq.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/timer.c -o !WSLPATH!/build/timer.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/keyboard.c -o !WSLPATH!/build/keyboard.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/syscall.c -o !WSLPATH!/build/syscall.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/string.c -o !WSLPATH!/build/string.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/printf.c -o !WSLPATH!/build/printf.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/net.c -o !WSLPATH!/build/net.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/task.c -o !WSLPATH!/build/task.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/scheduler.c -o !WSLPATH!/build/scheduler.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/shell.c -o !WSLPATH!/build/shell.o
wsl gcc -ffreestanding -m64 -mcmodel=kernel -fno-pie -fno-PIC -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I!WSLPATH!/include -c !WSLPATH!/kernel/il_runtime.c -o !WSLPATH!/build/il_runtime.o

rem Link kernel using WSL - ensure all objects are included
wsl ld -T !WSLPATH!/link.ld -nostdlib -z max-page-size=0x1000 -z noexecstack -o !WSLPATH!/build/kernel.elf !WSLPATH!/build/boot.o !WSLPATH!/build/long_mode.o !WSLPATH!/build/main.o !WSLPATH!/build/vga.o !WSLPATH!/build/serial.o !WSLPATH!/build/string.o !WSLPATH!/build/printf.o !WSLPATH!/build/memory.o !WSLPATH!/build/idt.o !WSLPATH!/build/irq.o !WSLPATH!/build/timer.o !WSLPATH!/build/keyboard.o !WSLPATH!/build/syscall.o !WSLPATH!/build/task.o !WSLPATH!/build/scheduler.o !WSLPATH!/build/shell.o !WSLPATH!/build/il_runtime.o !WSLPATH!/build/net.o !WSLPATH!/build/interrupt_stubs.o !WSLPATH!/build/context.o !WSLPATH!/build/gdt.o !WSLPATH!/build/io.o

rem Create ISO directory structure
if not exist iso\boot\grub mkdir iso\boot\grub

rem Copy kernel to ISO
copy /Y build\kernel.elf iso\boot\kernel.elf >nul

rem Create ISO using GRUB2 from WSL
wsl grub-mkrescue -o !WSLPATH!/build/charisos.iso !WSLPATH!/iso

echo Build complete. Output: build\charisos.iso
exit /b 0

:clean
rmdir /s /q build 2>nul
if exist iso\boot\kernel.elf del /q iso\boot\kernel.elf
if exist build\charisos.iso del /q build\charisos.iso
exit /b 0