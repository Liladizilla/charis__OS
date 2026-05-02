# CharisOS Makefile

# Tools
NASM = nasm
GCC = gcc
LD = ld
QEMU = qemu-system-x86_64

# Directories
SRC_DIR = .
BOOT_DIR = boot
KERNEL_DIR = kernel
INCLUDE_DIR = include
BUILD_DIR = build

# Flags
NASM_FLAGS = -f elf64
GCC_FLAGS = -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -I$(INCLUDE_DIR)
LD_FLAGS = -T link.ld -nostdlib -z max-page-size=0x1000 -z noexecstack

# Source files
BOOT_SOURCES = $(BOOT_DIR)/boot.asm $(BOOT_DIR)/long_mode.asm
KERNEL_SOURCES = $(KERNEL_DIR)/main.c $(KERNEL_DIR)/vga.c $(KERNEL_DIR)/serial.c $(KERNEL_DIR)/string.c $(KERNEL_DIR)/printf.c $(KERNEL_DIR)/memory.c $(KERNEL_DIR)/idt.c $(KERNEL_DIR)/irq.c $(KERNEL_DIR)/timer.c $(KERNEL_DIR)/keyboard.c $(KERNEL_DIR)/syscall.c $(KERNEL_DIR)/task.c $(KERNEL_DIR)/scheduler.c $(KERNEL_DIR)/shell.c $(KERNEL_DIR)/il_runtime.c $(KERNEL_DIR)/net.c
ASM_SOURCES = $(KERNEL_DIR)/asm/interrupt_stubs.asm $(KERNEL_DIR)/asm/context.asm $(KERNEL_DIR)/asm/gdt.asm $(KERNEL_DIR)/asm/io.asm

# Object files
BOOT_OBJS = $(patsubst $(BOOT_DIR)/%.asm, $(BUILD_DIR)/%.o, $(BOOT_SOURCES))
KERNEL_OBJS = $(patsubst $(KERNEL_DIR)/%.c, $(BUILD_DIR)/%.o, $(KERNEL_SOURCES))
ASM_OBJS = $(patsubst $(KERNEL_DIR)/asm/%.asm, $(BUILD_DIR)/%.o, $(ASM_SOURCES))

ALL_OBJS = $(BOOT_OBJS) $(KERNEL_OBJS) $(ASM_OBJS)

# Targets
all: $(BUILD_DIR)/charisos.iso

$(BUILD_DIR)/charisos.iso: $(BUILD_DIR)/kernel.elf
	mkdir -p iso/boot/grub
	cp $(BUILD_DIR)/kernel.elf iso/boot/
	grub-mkrescue -o $@ iso/

$(BUILD_DIR)/kernel.elf: $(ALL_OBJS) link.ld
	$(LD) $(LD_FLAGS) -o $@ $(ALL_OBJS)

$(BUILD_DIR)/%.o: $(BOOT_DIR)/%.asm
	mkdir -p $(BUILD_DIR)
	$(NASM) $(NASM_FLAGS) -o $@ $<

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c
	mkdir -p $(BUILD_DIR)
	$(GCC) $(GCC_FLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/asm/%.asm
	mkdir -p $(BUILD_DIR)
	$(NASM) $(NASM_FLAGS) -o $@ $<

run: $(BUILD_DIR)/charisos.iso
	$(QEMU) -cdrom $< -m 256M -serial file:serial.log

debug: $(BUILD_DIR)/charisos.iso
	$(QEMU) -cdrom $< -m 256M -serial stdio -s -S

run-debug: $(BUILD_DIR)/charisos.iso
	$(QEMU) -cdrom $< -m 256M -serial stdio

clean:
	rm -rf $(BUILD_DIR) iso/boot/kernel.elf iso/charisos.iso

.PHONY: all run debug run-debug clean