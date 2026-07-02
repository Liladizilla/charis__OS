#!/bin/bash
# Build script for CharisOS in MSYS2/Windows
# Run this from MSYS2 MinGW 64-bit shell

set -e

echo "Building CharisOS..."

# Create build directory
mkdir -p build

NASM=nasm
GCC=gcc
LD=ld
XORRISO=xorriso

# Assemble boot files
$NASM -f elf64 boot/boot.asm -o build/boot.o
$NASM -f elf64 boot/long_mode.asm -o build/long_mode.o
$NASM -f elf64 kernel/asm/interrupt_stubs.asm -o build/interrupt_stubs.o
$NASM -f elf64 kernel/asm/context.asm -o build/context.o
$NASM -f elf64 kernel/asm/gdt.asm -o build/gdt.o
$NASM -f elf64 kernel/asm/io.asm -o build/io.o

CFLAGS="-ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude"

# Compile all kernel files
$GCC $CFLAGS -c kernel/string.c -o build/string.o
$GCC $CFLAGS -c kernel/printf.c -o build/printf.o
$GCC $CFLAGS -c kernel/memory.c -o build/memory.o
$GCC $CFLAGS -c kernel/bootmem.c -o build/bootmem.o
$GCC $CFLAGS -c kernel/heap.c -o build/heap.o
$GCC $CFLAGS -c kernel/pmm.c -o build/pmm.o
$GCC $CFLAGS -c kernel/vmm.c -o build/vmm.o
$GCC $CFLAGS -c kernel/idt.c -o build/idt.o
$GCC $CFLAGS -c kernel/irq.c -o build/irq.o
$GCC $CFLAGS -c kernel/timer.c -o build/timer.o
$GCC $CFLAGS -c kernel/keyboard.c -o build/keyboard.o
$GCC $CFLAGS -c kernel/syscall.c -o build/syscall.o
$GCC $CFLAGS -c kernel/task.c -o build/task.o
$GCC $CFLAGS -c kernel/scheduler.c -o build/scheduler.o
$GCC $CFLAGS -c kernel/shell.c -o build/shell.o
$GCC $CFLAGS -c kernel/vga.c -o build/vga.o
$GCC $CFLAGS -c kernel/serial.c -o build/serial.o
$GCC $CFLAGS -c kernel/net.c -o build/net.o
$GCC $CFLAGS -c kernel/ata.c -o build/ata.o
$GCC $CFLAGS -c kernel/fs.c -o build/fs.o
$GCC $CFLAGS -c kernel/vfs.c -o build/vfs.o
$GCC $CFLAGS -c kernel/elf.c -o build/elf.o
$GCC $CFLAGS -c kernel/user.c -o build/user.o
$GCC $CFLAGS -c kernel/input.c -o build/input.o
$GCC $CFLAGS -c kernel/mouse.c -o build/mouse.o
$GCC $CFLAGS -c kernel/fb.c -o build/fb.o
$GCC $CFLAGS -c kernel/psf.c -o build/psf.o
$GCC $CFLAGS -c kernel/graphics.c -o build/graphics.o
$GCC $CFLAGS -c kernel/wm.c -o build/wm.o
$GCC $CFLAGS -c kernel/ipc.c -o build/ipc.o
$GCC $CFLAGS -c kernel/compositor.c -o build/compositor.o
$GCC $CFLAGS -c kernel/socket.c -o build/socket.o
$GCC $CFLAGS -c kernel/demo.c -o build/demo.o
$GCC $CFLAGS -c kernel/desktop.c -o build/desktop.o
$GCC $CFLAGS -c kernel/apps.c -o build/apps.o
$GCC $CFLAGS -c kernel/audio.c -o build/audio.o
$GCC $CFLAGS -c kernel/usb.c -o build/usb.o
$GCC $CFLAGS -c kernel/pci.c -o build/pci.o
$GCC $CFLAGS -c kernel/services.c -o build/services.o
$GCC $CFLAGS -c kernel/diagnostics.c -o build/diagnostics.o
$GCC $CFLAGS -c kernel/display.c -o build/display.o
$GCC $CFLAGS -c kernel/config.c -o build/config.o
$GCC $CFLAGS -c kernel/power.c -o build/power.o
$GCC $CFLAGS -c kernel/security.c -o build/security.o
$GCC $CFLAGS -c kernel/il_runtime.c -o build/il_runtime.o
$GCC $CFLAGS -c kernel/main.c -o build/main.o

# Link kernel
$LD -T link.ld -nostdlib -z max-page-size=0x1000 -z noexecstack -o build/kernel.elf \
    build/boot.o build/long_mode.o build/main.o build/vga.o build/serial.o \
    build/string.o build/printf.o build/pmm.o build/vmm.o build/memory.o build/heap.o build/bootmem.o \
    build/idt.o build/irq.o build/timer.o build/keyboard.o build/syscall.o build/task.o build/scheduler.o \
    build/shell.o build/il_runtime.o build/net.o build/ata.o build/fs.o build/vfs.o build/elf.o build/fb.o build/psf.o \
    build/graphics.o build/wm.o build/input.o build/mouse.o build/ipc.o build/compositor.o build/socket.o build/user.o \
    build/demo.o build/desktop.o build/apps.o build/audio.o build/usb.o build/pci.o build/services.o \
    build/diagnostics.o build/display.o build/config.o build/power.o build/security.o \
    build/interrupt_stubs.o build/context.o build/gdt.o build/io.o

echo "Kernel linked successfully: build/kernel.elf"

# Create ISO directory structure
mkdir -p iso/boot/grub

# Copy kernel
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
$XORRISO -as mkisofs -o build/charisos.iso -b boot/grub/grub.cfg -no-emul-boot -boot-load-size 4 -iso-level 3 -quiet iso

echo "Build complete. Output: build/charisos.iso"