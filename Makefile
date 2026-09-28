# CharisOS Makefile

# Tools
UNAME := $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(UNAME),Linux)
# Native Linux/WSL - use host GCC for compilation
NASM = nasm
GCC = gcc
LD = ld
QEMU = qemu-system-x86_64
GRUB = grub-mkrescue
else ifeq ($(UNAME),MINGW64)
# MSYS2 MinGW-w64 environment
NASM = nasm
GCC = x86_64-w64-mingw32-gcc
LD = x86_64-w64-mingw32-ld
QEMU = qemu-system-x86_64
GRUB = grub-mkrescue
else
# Assume MSYS2 or try common paths
NASM = nasm
GCC = gcc
LD = ld
QEMU = qemu-system-x86_64
GRUB = grub-mkrescue
endif

# Directories
SRC_DIR = .
BOOT_DIR = boot
KERNEL_DIR = kernel
INCLUDE_DIR = include
BUILD_DIR = build

# Flags
NASM_FLAGS = -f elf64
# -MMD -MP writes a .d file beside each object listing the headers it pulled in.
# Without it, editing a header rebuilds nothing that includes it, so a macro or
# struct change silently leaves stale objects in the link. That already caused
# a bug here: a corrected FB_COLOR argument order had no effect until a clean
# build, and the build looked correct the whole time.
DEPFLAGS = -MMD -MP

GCC_FLAGS = -ffreestanding -m64 -fno-pie -fno-pic -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -fno-omit-frame-pointer -Wall -Wextra -fstack-protector-strong -I$(INCLUDE_DIR) $(DEPFLAGS)
LD_FLAGS = -T link.ld -nostdlib -z max-page-size=0x1000 -z noexecstack

# Source files
BOOT_SOURCES = $(BOOT_DIR)/boot.asm $(BOOT_DIR)/long_mode.asm
KERNEL_SOURCES = $(KERNEL_DIR)/main.c $(KERNEL_DIR)/vga.c $(KERNEL_DIR)/serial.c $(KERNEL_DIR)/string.c $(KERNEL_DIR)/printf.c $(KERNEL_DIR)/memory.c $(KERNEL_DIR)/bootmem.c $(KERNEL_DIR)/heap.c $(KERNEL_DIR)/pmm.c $(KERNEL_DIR)/vmm.c $(KERNEL_DIR)/idt.c $(KERNEL_DIR)/irq.c $(KERNEL_DIR)/timer.c $(KERNEL_DIR)/keyboard.c $(KERNEL_DIR)/syscall.c $(KERNEL_DIR)/task.c $(KERNEL_DIR)/scheduler.c $(KERNEL_DIR)/shell.c $(KERNEL_DIR)/il_runtime.c $(KERNEL_DIR)/net.c $(KERNEL_DIR)/ata.c $(KERNEL_DIR)/fs.c $(KERNEL_DIR)/vfs.c $(KERNEL_DIR)/elf.c $(KERNEL_DIR)/user.c $(KERNEL_DIR)/input.c $(KERNEL_DIR)/mouse.c $(KERNEL_DIR)/fb.c $(KERNEL_DIR)/psf.c $(KERNEL_DIR)/graphics.c $(KERNEL_DIR)/compositor.c $(KERNEL_DIR)/wm.c $(KERNEL_DIR)/ipc.c $(KERNEL_DIR)/socket.c $(KERNEL_DIR)/demo.c $(KERNEL_DIR)/desktop.c $(KERNEL_DIR)/apps.c $(KERNEL_DIR)/audio.c $(KERNEL_DIR)/usb.c $(KERNEL_DIR)/pci.c $(KERNEL_DIR)/services.c $(KERNEL_DIR)/diagnostics.c $(KERNEL_DIR)/display.c $(KERNEL_DIR)/config.c $(KERNEL_DIR)/power.c $(KERNEL_DIR)/security.c $(KERNEL_DIR)/widgets.c $(KERNEL_DIR)/signal.c $(KERNEL_DIR)/pipe.c $(KERNEL_DIR)/driver.c $(KERNEL_DIR)/raster.c $(KERNEL_DIR)/hda.c $(KERNEL_DIR)/gamepad.c $(KERNEL_DIR)/logo.c $(KERNEL_DIR)/font_data.c $(KERNEL_DIR)/gfx_text.c $(KERNEL_DIR)/shell_ui.c $(KERNEL_DIR)/vmm_test.c
ASM_SOURCES = $(KERNEL_DIR)/asm/interrupt_stubs.asm $(KERNEL_DIR)/asm/context.asm $(KERNEL_DIR)/asm/gdt.asm $(KERNEL_DIR)/asm/io.asm

# Object files
BOOT_OBJS = $(patsubst $(BOOT_DIR)/%.asm,$(BUILD_DIR)/%.o,$(BOOT_SOURCES))
KERNEL_OBJS = $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL_SOURCES))
ASM_OBJS = $(patsubst $(KERNEL_DIR)/asm/%.asm,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

ALL_OBJS = $(BOOT_OBJS) $(KERNEL_OBJS) $(ASM_OBJS)

# Targets
# `all` stays the canonical build so CI and existing scripts keep working.
all: $(BUILD_DIR)/charisos.iso

$(BUILD_DIR)/charisos.iso: $(BUILD_DIR)/kernel.elf
	mkdir -p iso/boot/grub
	cp $(BUILD_DIR)/kernel.elf iso/boot/
	$(GRUB) -o $@ iso/

$(BUILD_DIR)/kernel.elf: $(ALL_OBJS) link.ld
	$(LD) $(LD_FLAGS) -o $@ $(ALL_OBJS)

# ── Named boot artifacts ────────────────────────────────────────────
# grub-mkrescue already emits a hybrid image: an ISO9660 filesystem with
# El Torito for CD/DVD, a protective MBR and a GPT header, and GRUB images
# for both i386-pc (legacy BIOS) and x86_64-efi (UEFI). That single image
# boots in all four combinations, so these are the same bytes under names
# that say how they are meant to be used. Verified in QEMU as CD-ROM and as
# raw disk, under both BIOS and OVMF/UEFI.

VM_ISO  = $(BUILD_DIR)/charisos-vm.iso
USB_IMG = $(BUILD_DIR)/charisos-usb.img

images: $(VM_ISO) $(USB_IMG)

# ── Text-only variant ─────────────────────────────────────────────────
# Same source, graphics compiled out. Boots to the VGA text console and the
# shell on any machine, including ones whose firmware offers no linear
# framebuffer. The difference is a build flag, not a separate tree, so the two
# cannot drift apart.
TEXT_ELIG  = $(filter-out $(BUILD_DIR)/fb.o $(BUILD_DIR)/font_data.o \
                          $(BUILD_DIR)/gfx_text.o $(BUILD_DIR)/shell_ui.o, $(ALL_OBJS))
TEXT_ISO   = $(BUILD_DIR)/charisos-text.iso
TEXT_FLAGS = -DCHARIS_TEXT_ONLY

images-text: $(TEXT_ISO)

$(TEXT_ISO): $(KERNEL_DIR)/main.c
	@echo "==> Building text-only image"
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS="$(TEXT_FLAGS)" images
	@cp $(VM_ISO) $@
	@cp $(USB_IMG) $(BUILD_DIR)/charisos-text-usb.img
	@echo "==> text-only images: $@ and $(BUILD_DIR)/charisos-text-usb.img"

$(VM_ISO): $(BUILD_DIR)/charisos.iso
	cp $< $@

$(USB_IMG): $(BUILD_DIR)/charisos.iso
	cp $< $@

$(BUILD_DIR)/%.o: $(BOOT_DIR)/%.asm
	mkdir -p $(BUILD_DIR)
	$(NASM) $(NASM_FLAGS) -o $@ $<

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c
	mkdir -p $(BUILD_DIR)
	$(GCC) $(GCC_FLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/vmm_test.o: $(KERNEL_DIR)/vmm_test.c
	mkdir -p $(BUILD_DIR)
	$(GCC) $(GCC_FLAGS) -DRUN_VMM_TESTS -c -o $@ $<

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/asm/%.asm
	mkdir -p $(BUILD_DIR)
	$(NASM) $(NASM_FLAGS) -o $@ $<

run: $(BUILD_DIR)/charisos.iso
	$(QEMU) -cdrom $< -m 256M -serial stdio -no-reboot -no-shutdown -d int,cpu_reset -D qemu.log

gdb: $(BUILD_DIR)/charisos.iso
	@echo "==> GDB server on :1234"
	@echo "    In another terminal run:"
	@echo "    x86_64-elf-gdb build/kernel.elf"
	@echo "    (gdb) target remote :1234"
	@echo "    (gdb) break kernel_main && continue"
	$(QEMU) -cdrom $< -m 256M -serial stdio -s -S -no-reboot

debug: $(BUILD_DIR)/charisos.iso
	$(QEMU) -cdrom $< -m 256M -serial stdio -no-reboot -no-shutdown -d int,cpu_reset,pcall,mmu -D qemu.log

run-debug: $(BUILD_DIR)/charisos.iso
	$(QEMU) -cdrom $< -m 256M -serial stdio

test: $(BUILD_DIR)/vmm_test.o
	@echo "VMM test object built successfully with RUN_VMM_TESTS defined"

# Fail early and legibly when the toolchain is incomplete, rather than part
# way through a build with a confusing "command not found".
check-tools:
	@ok=1; \
	for pair in "cc:$(CC)" "nasm:$(NASM)" "ld:$(LD)" "grub-mkrescue:$(GRUB)"; do \
		name=$${pair%%:*}; cmd=$${pair#*:}; \
		if command -v $$cmd >/dev/null 2>&1; then \
			printf "  ok    %-16s %s\n" "$$name" "$$cmd"; \
		else \
			printf "  MISSING %-14s %s\n" "$$name" "$$cmd"; ok=0; \
		fi; \
	done; \
	if [ "$$ok" = "0" ]; then \
		echo; echo "Install the missing tools, e.g.:"; \
		echo "  Debian/Ubuntu: sudo apt install build-essential nasm grub-pc-bin xorriso qemu-system-x86"; \
		echo "  Fedora:        sudo dnf install gcc nasm binutils grub2-tools xorriso qemu-system-x86"; \
		exit 1; \
	fi; \
	echo "  all build tools present"

# ── Environment-specific runners ────────────────────────────────────

# VM: attach the ISO as a CD-ROM. This is the configuration to use with
# GNOME Boxes / virt-manager / `qemu-system-x86_64` on a workstation.
run-vm: $(VM_ISO)
	$(QEMU) -cdrom $< -m 256M -serial stdio -no-reboot

# Bare metal: emulate a USB stick already flashed with the image, by handing
# the raw image to QEMU as a hard disk. Exercises exactly the layout that
# `dd if=charisos-usb.img of=/dev/sdX` produces.
# snapshot=on because UEFI firmware writes back to the medium it booted from;
# without it, simply testing the image would corrupt it.
run-usb: $(USB_IMG)
	$(QEMU) -drive file=$<,format=raw,if=ide,snapshot=on -m 256M -serial stdio -no-reboot

# UEFI variants — useful for checking that firmware-level boot works before
# touching real hardware. OVMF lives in a different place on Fedora, Debian and
# Ubuntu, so resolve it rather than hardcoding one path.
OVMF ?= $(firstword $(wildcard \
	/usr/share/OVMF/OVMF_CODE.fd \
	/usr/share/edk2/ovmf/OVMF_CODE.fd \
	/usr/share/edk2-ovmf/OVMF_CODE.fd \
	/usr/share/qemu/OVMF.fd \
	/usr/share/OVMF/OVMF_CODE_4M.fd))

run-vm-uefi: $(VM_ISO)
	@test -n "$(OVMF)" && test -f "$(OVMF)" || { echo "No OVMF firmware found; install ovmf / edk2-ovmf"; exit 1; }
	$(QEMU) -cdrom $< -m 256M -serial stdio -no-reboot -bios $(OVMF)

run-usb-uefi: $(USB_IMG)
	@test -n "$(OVMF)" && test -f "$(OVMF)" || { echo "No OVMF firmware found; install ovmf / edk2-ovmf"; exit 1; }
	$(QEMU) -drive file=$<,format=raw,if=ide,snapshot=on -m 256M -serial stdio -no-reboot -bios $(OVMF)

# Boot gate used by CI and by hand before tagging a release. Asserts on the
# sentinel the kernel prints on the serial console only after the Multiboot2
# magic validated, every init*() returned and the shell task was created.
# The assertions live in tools/verify-boot.sh so CI and humans run identical
# logic; see that file for why grepping the banner is not sufficient.
verify-boot: $(VM_ISO) $(USB_IMG)
	./tools/verify-boot.sh $(VM_ISO)
	./tools/verify-boot.sh $(USB_IMG) --disk
	@if [ -n "$(OVMF)" ] && [ -f "$(OVMF)" ]; then \
		OVMF=$(OVMF) ./tools/verify-boot.sh $(VM_ISO) --uefi; \
		OVMF=$(OVMF) ./tools/verify-boot.sh $(USB_IMG) --disk --uefi; \
	else \
		echo "SKIP: no OVMF firmware found, skipping the UEFI boot checks"; \
	fi

clean:
	rm -rf $(BUILD_DIR) iso/boot/kernel.elf iso/charisos.iso

# Pull in the generated header dependencies.
-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all images images-text run run-vm run-usb run-vm-uefi run-usb-uefi verify-boot gdb debug run-debug test clean