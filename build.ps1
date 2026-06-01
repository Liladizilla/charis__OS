# build.ps1 - PowerShell build script for CharisOS

$ErrorActionPreference = "Stop"

$BuildDir = "build"
if (!(Test-Path $BuildDir)) { New-Item -ItemType Directory -Path $BuildDir }

$Nasm = "C:\msys64\mingw64\bin\nasm.exe"
$Gcc = "C:\msys64\mingw64\bin\gcc.exe"
$Ld = "C:\msys64\mingw64\bin\ld.exe"
$Xorriso = "C:\msys64\usr\bin\xorriso.exe"

$GccFlags = @("-ffreestanding", "-m64", "-fno-pie", "-fno-pic", "-mno-red-zone", "-mno-mmx", "-mno-sse", "-mno-sse2", "-O2", "-Wall", "-Wextra", "-Iinclude")

Write-Host "Assembling..."
& $Nasm -f elf64 boot/boot.asm -o "$BuildDir\boot.o"
& $Nasm -f elf64 boot/long_mode.asm -o "$BuildDir\long_mode.o"
& $Nasm -f elf64 kernel/asm/interrupt_stubs.asm -o "$BuildDir\interrupt_stubs.o"
& $Nasm -f elf64 kernel/asm/context.asm -o "$BuildDir\context.o"
& $Nasm -f elf64 kernel/asm/gdt.asm -o "$BuildDir\gdt.o"
& $Nasm -f elf64 kernel/asm/io.asm -o "$BuildDir\io.o"

Write-Host "Compiling..."
$KernelFiles = @(
    "string.c","printf.c","memory.c","bootmem.c","heap.c","pmm.c","vmm.c","vmm_test.c",
    "idt.c","irq.c","timer.c","keyboard.c","syscall.c","task.c","scheduler.c","shell.c",
    "vga.c","serial.c","net.c","ata.c","disk.c","fs.c","vfs.c","elf.c","user.c","input.c",
    "mouse.c","fb.c","psf.c","graphics.c","wm.c","ipc.c","socket.c","demo.c","desktop.c",
    "apps.c","audio.c","usb.c","pci.c","services.c","diagnostics.c","display.c","config.c",
    "power.c","security.c","il_runtime.c","main.c"
)

foreach ($file in $KernelFiles) {
    Write-Host "Compiling: $file"
    $outputFile = "$BuildDir\$($file -replace '\.c$','.o')"
    & $Gcc @GccFlags -c "kernel\$file" -o $outputFile
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  OK: $outputFile"
    } else {
        Write-Host "  FAILED"
    }
}

Write-Host "Linking..."
& $Ld -T link.ld -nostdlib -o "$BuildDir\kernel.elf" `
    "$BuildDir\boot.o" "$BuildDir\long_mode.o" "$BuildDir\main.o" "$BuildDir\vga.o" "$BuildDir\serial.o" `
    "$BuildDir\string.o" "$BuildDir\printf.o" "$BuildDir\pmm.o" "$BuildDir\vmm.o" "$BuildDir\vmm_test.o" "$BuildDir\memory.o" "$BuildDir\heap.o" "$BuildDir\bootmem.o" `
    "$BuildDir\idt.o" "$BuildDir\irq.o" "$BuildDir\timer.o" "$BuildDir\keyboard.o" "$BuildDir\syscall.o" "$BuildDir\task.o" "$BuildDir\scheduler.o" `
    "$BuildDir\shell.o" "$BuildDir\il_runtime.o" "$BuildDir\net.o" "$BuildDir\ata.o" "$BuildDir\fs.o" "$BuildDir\vfs.o" "$BuildDir\elf.o" "$BuildDir\fb.o" "$BuildDir\psf.o" `
    "$BuildDir\graphics.o" "$BuildDir\wm.o" "$BuildDir\input.o" "$BuildDir\mouse.o" "$BuildDir\ipc.o" "$BuildDir\socket.o" "$BuildDir\user.o" `
    "$BuildDir\demo.o" "$BuildDir\desktop.o" "$BuildDir\apps.o" "$BuildDir\audio.o" "$BuildDir\usb.o" "$BuildDir\pci.o" "$BuildDir\services.o" `
    "$BuildDir\diagnostics.o" "$BuildDir\display.o" "$BuildDir\config.o" "$BuildDir\power.o" "$BuildDir\security.o" `
    "$BuildDir\interrupt_stubs.o" "$BuildDir\context.o" "$BuildDir\gdt.o" "$BuildDir\io.o"

# Create ISO directory structure
if (!(Test-Path "iso")) { New-Item -ItemType Directory -Path "iso" }
if (!(Test-Path "iso\boot")) { New-Item -ItemType Directory -Path "iso\boot" }
if (!(Test-Path "iso\boot\grub")) { New-Item -ItemType Directory -Path "iso\boot\grub" }

# Copy kernel
Copy-Item "$BuildDir\kernel.elf" -Destination "iso\boot\kernel.elf"

# Create GRUB configuration
@"
set timeout=5
set default=0

menuentry ""CharisOS"" {
    multiboot2 /boot/kernel.elf
    boot
}
"@ | Out-File -FilePath "iso\boot\grub\grub.cfg" -Encoding ASCII

Write-Host "Creating ISO..."
& $Xorriso -as mkisofs -o "$BuildDir\charisos.iso" -b boot/grub/grub.cfg -no-emul-boot -boot-load-size 4 -iso-level 3 -quiet iso

Write-Host "Build complete. Output: build\charisos.iso"