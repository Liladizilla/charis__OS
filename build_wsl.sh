#!/bin/bash
# Build script for CharisOS in WSL/Linux

set -e

echo "Building CharisOS..."

# Create build directory
mkdir -p build

# Assemble boot files
nasm -f elf64 boot/boot.asm -o build/boot.o
nasm -f elf64 boot/long_mode.asm -o build/long_mode.o
nasm -f elf64 kernel/asm/interrupt_stubs.asm -o build/interrupt_stubs.o
nasm -f elf64 kernel/asm/context.asm -o build/context.o
nasm -f elf64 kernel/asm/gdt.asm -o build/gdt.o
nasm -f elf64 kernel/asm/io.asm -o build/io.o

# Compile kernel files
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/main.c -o build/main.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/vga.c -o build/vga.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/serial.c -o build/serial.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/memory.c -o build/memory.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/idt.c -o build/idt.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/irq.c -o build/irq.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/timer.c -o build/timer.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/keyboard.c -o build/keyboard.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/syscall.c -o build/syscall.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/task.c -o build/task.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/scheduler.c -o build/scheduler.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/shell.c -o build/shell.o
gcc -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/il_runtime.c -o build/il_runtime.o

# Link kernel
ld -T link.ld -nostdlib -z max-page-size=0x1000 -z noexecstack -o build/kernel.elf \
    build/boot.o build/long_mode.o build/main.o build/vga.o build/serial.o \
    build/memory.o build/idt.o build/irq.o build/timer.o build/keyboard.o \
    build/syscall.o build/task.o build/scheduler.o build/shell.o build/il_runtime.o \
    build/interrupt_stubs.o build/context.o build/gdt.o build/io.o

# Create ISO directory structure
mkdir -p iso/boot/grub

# Copy kernel and GRUB config
cp build/kernel.elf iso/boot/kernel.elf

# Create GRUB configuration
cat > iso/boot/grub/grub.cfg << 'EOF'
set timeout=5
set default=0

menuentry "CharisOS" {
    multiboot2 /boot/kernel.elf
    boot
}
EOF

# Create bootable ISO
grub-mkrescue -o build/charisos.iso iso

echo "Build complete. Output: build/charisos.iso"