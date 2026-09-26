# CharisOS freestanding C + NASM build

BUILD_DIR := build
BOOT_DIR := boot
KERNEL_DIR := kernel
INCLUDE_DIR := include

# Prefer the cross compiler when installed; override CC/LD if needed.
CC := $(or $(shell command -v x86_64-elf-gcc 2>/dev/null),gcc)
LD := $(or $(shell command -v x86_64-elf-ld 2>/dev/null),ld)
NASM ?= nasm
GRUB_MKRESCUE ?= grub-mkrescue
QEMU ?= qemu-system-x86_64

CFLAGS := -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel \
          -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 \
          -fno-omit-frame-pointer -Wall -Wextra -I$(INCLUDE_DIR)
ASFLAGS := -f elf64
LDFLAGS := -T link.ld -nostdlib -z max-page-size=0x1000 -z noexecstack

BOOT_SOURCES := $(BOOT_DIR)/boot.asm $(BOOT_DIR)/long_mode.asm
ASM_SOURCES := $(wildcard $(KERNEL_DIR)/asm/*.asm)
KERNEL_SOURCES := $(wildcard $(KERNEL_DIR)/*.c)

BOOT_OBJECTS := $(patsubst $(BOOT_DIR)/%.asm,$(BUILD_DIR)/%.o,$(BOOT_SOURCES))
ASM_OBJECTS := $(patsubst $(KERNEL_DIR)/asm/%.asm,$(BUILD_DIR)/asm_%.o,$(ASM_SOURCES))
KERNEL_OBJECTS := $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL_SOURCES))
OBJECTS := $(BOOT_OBJECTS) $(KERNEL_OBJECTS) $(ASM_OBJECTS)

.PHONY: all clean run debug check-tools

all: $(BUILD_DIR)/charisos.iso

$(BUILD_DIR)/charisos.iso: $(BUILD_DIR)/kernel.elf iso/boot/grub/grub.cfg
	mkdir -p iso/boot
	cp $(BUILD_DIR)/kernel.elf iso/boot/kernel.elf
	$(GRUB_MKRESCUE) -o $@ iso

$(BUILD_DIR)/kernel.elf: $(OBJECTS) link.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILD_DIR)/%.o: $(BOOT_DIR)/%.asm
	mkdir -p $(BUILD_DIR)
	$(NASM) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/compositor.o: CFLAGS += -msse2

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/asm_%.o: $(KERNEL_DIR)/asm/%.asm
	mkdir -p $(BUILD_DIR)
	$(NASM) $(ASFLAGS) -o $@ $<

check-tools:
	@command -v $(CC) >/dev/null || (echo "missing compiler: $(CC)"; exit 1)
	@command -v $(NASM) >/dev/null || (echo "missing NASM: $(NASM)"; exit 1)
	@command -v $(GRUB_MKRESCUE) >/dev/null || (echo "missing grub-mkrescue"; exit 1)
	@command -v $(QEMU) >/dev/null || (echo "missing QEMU"; exit 1)
	@echo "CharisOS build tools are available."

run: $(BUILD_DIR)/charisos.iso
	$(QEMU) -cdrom $< -m 256M -serial stdio -no-reboot -no-shutdown

debug: $(BUILD_DIR)/charisos.iso
	$(QEMU) -cdrom $< -m 256M -serial stdio -no-reboot -no-shutdown -d int,cpu_reset,pcall,mmu -D qemu.log

clean:
	rm -rf $(BUILD_DIR) iso/boot/kernel.elf iso/charisos.iso
