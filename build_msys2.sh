#!/bin/bash
# Build script for CharisOS in MSYS2/Windows
# Run this from MSYS2 MinGW 64-bit shell

set -e

echo "Building CharisOS..."

# Create build directory
mkdir -p build

# Assemble boot files
"C:\msys64\mingw64\bin\nasm.exe" -f elf64 boot/boot.asm -o build/boot.o
"C:\msys64\mingw64\bin\nasm.exe" -f elf64 boot/long_mode.asm -o build/long_mode.o
"C:\msys64\mingw64\bin\nasm.exe" -f elf64 kernel/asm/interrupt_stubs.asm -o build/interrupt_stubs.o
"C:\msys64\mingw64\bin\nasm.exe" -f elf64 kernel/asm/context.asm -o build/context.o
"C:\msys64\mingw64\bin\nasm.exe" -f elf64 kernel/asm/gdt.asm -o build/gdt.o
"C:\msys64\mingw64\bin\nasm.exe" -f elf64 kernel/asm/io.asm -o build/io.o

# Compile all kernel files
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/string.c -o build/string.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/printf.c -o build/printf.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/memory.c -o build/memory.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/bootmem.c -o build/bootmem.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/heap.c -o build/heap.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/pmm.c -o build/pmm.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/vmm.c -o build/vmm.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/vmm_test.c -o build/vmm_test.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/idt.c -o build/idt.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/irq.c -o build/irq.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/timer.c -o build/timer.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/keyboard.c -o build/keyboard.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/syscall.c -o build/syscall.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/task.c -o build/task.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/scheduler.c -o build/scheduler.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/shell.c -o build/shell.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/vga.c -o build/vga.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/serial.c -o build/serial.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/net.c -o build/net.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/ata.c -o build/ata.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/fs.c -o build/fs.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/vfs.c -o build/vfs.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/elf.c -o build/elf.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/user.c -o build/user.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/input.c -o build/input.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/mouse.c -o build/mouse.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/fb.c -o build/fb.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/psf.c -o build/psf.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/graphics.c -o build/graphics.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/wm.c -o build/wm.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/ipc.c -o build/ipc.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/socket.c -o build/socket.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/demo.c -o build/demo.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/desktop.c -o build/desktop.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/apps.c -o build/apps.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/audio.c -o build/audio.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/usb.c -o build/usb.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/pci.c -o build/pci.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/services.c -o build/services.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/diagnostics.c -o build/diagnostics.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/display.c -o build/display.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/config.c -o build/config.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/power.c -o build/power.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/security.c -o build/security.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/il_runtime.c -o build/il_runtime.o
"C:\msys64\mingw64\bin\gcc.exe" -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Iinclude -c kernel/main.c -o build/main.o

# Link kernel
"C:\msys64\mingw64\bin\ld.exe" -T link.ld -nostdlib -z max-page-size=0x1000 -z noexecstack -o build/kernel.elf \
    build/boot.o build/long_mode.o build/main.o build/vga.o build/serial.o \
    build/string.o build/printf.o build/pmm.o build/vmm.o build/vmm_test.o build/memory.o build/heap.o build/bootmem.o \
    build/idt.o build/irq.o build/timer.o build/keyboard.o build/syscall.o build/task.o build/scheduler.o \
    build/shell.o build/il_runtime.o build/net.o build/ata.o build/fs.o build/vfs.o build/elf.o build/fb.o build/psf.o \
    build/graphics.o build/wm.o build/input.o build/mouse.o build/ipc.o build/socket.o build/user.o \
    build/demo.o build/desktop.o build/apps.o build/audio.o build/usb.o build/pci.o build/services.o \
    build/diagnostics.o build/display.o build/config.o build/power.o build/security.o \
    build/interrupt_stubs.o build/context.o build/gdt.o build/io.o

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

# Create bootable ISO (use xorriso since GRUB not available)
"C:\msys64\usr\bin\xorriso.exe" -as mkisofs -o build/charisos.iso -b boot/grub/grub.cfg -no-emul-boot -boot-load-size 4 -iso-level 3 -quiet iso

echo "Build complete. Output: build/charisos.iso"