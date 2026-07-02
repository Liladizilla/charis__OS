@echo off
setlocal enabledelayedexpansion
cd /d %~dp0

rem Build helper for CharisOS on Windows with MSYS2 MinGW64 tools
if "%1"=="clean" goto clean

set MSYS2=C:\msys64\mingw64\bin
set USR=C:\msys64\usr\bin
set NASM=%MSYS2%\nasm.exe
set CLANG=%MSYS2%\clang.exe
set LD=%USR%\ld.exe
set XORRISO=%USR%\xorriso.exe

mkdir build 2>nul

echo Assembling boot files...
%NASM% -f elf64 boot\boot.asm -o build\boot.o
%NASM% -f elf64 boot\long_mode.asm -o build\long_mode.o
%NASM% -f elf64 kernel\asm\interrupt_stubs.asm -o build\interrupt_stubs.o
%NASM% -f elf64 kernel\asm\context.asm -o build\context.o
%NASM% -f elf64 kernel\asm\gdt.asm -o build\gdt.o
%NASM% -f elf64 kernel\asm\io.asm -o build\io.o

set CFLAGS=-std=gnu89 -ffreestanding -target x86_64-elf -m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -Iinclude -Wno-error -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-function-pointer-types -Wno-typedef-redefinition -Wno-gcc-compat -Wno-unused-parameter -Wno-unused-variable -Wno-cast-qual -Wno-missing-prototypes -Wno-missing-declarations -fgnu89-inline -fcommon

echo Compiling kernel...
%CLANG% %CFLAGS% -c kernel\main.c -o build\main.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\vga.c -o build\vga.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\serial.c -o build\serial.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\string.c -o build\string.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\printf.c -o build\printf.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\memory.c -o build\memory.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\bootmem.c -o build\bootmem.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\heap.c -o build\heap.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\pmm.c -o build\pmm.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\vmm.c -o build\vmm.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\idt.c -o build\idt.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\irq.c -o build\irq.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\timer.c -o build\timer.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\keyboard.c -o build\keyboard.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\syscall.c -o build\syscall.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\task.c -o build\task.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\scheduler.c -o build\scheduler.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\shell.c -o build\shell.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\il_runtime.c -o build\il_runtime.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\net.c -o build\net.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\ata.c -o build\ata.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\fs.c -o build\fs.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\vfs.c -o build\vfs.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\elf.c -o build\elf.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\user.c -o build\user.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\input.c -o build\input.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\mouse.c -o build\mouse.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\fb.c -o build\fb.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\psf.c -o build\psf.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\graphics.c -o build\graphics.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\wm.c -o build\wm.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\ipc.c -o build\ipc.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\compositor.c -o build\compositor.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\socket.c -o build\socket.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\demo.c -o build\demo.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\desktop.c -o build\desktop.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\apps.c -o build\apps.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\audio.c -o build\audio.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\usb.c -o build\usb.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\pci.c -o build\pci.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\services.c -o build\services.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\diagnostics.c -o build\diagnostics.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\display.c -o build\display.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\config.c -o build\config.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\power.c -o build\power.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\security.c -o build\security.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\widgets.c -o build\widgets.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\signal.c -o build\signal.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\pipe.c -o build\pipe.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\driver.c -o build\driver.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\raster.c -o build\raster.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\hda.c -o build\hda.o || exit /b 1
%CLANG% %CFLAGS% -c kernel\gamepad.c -o build\gamepad.o || exit /b 1

echo Linking kernel...
%LD% -T link.ld -nostdlib -o build\kernel.elf ^
    build\boot.o build\long_mode.o build\main.o build\vga.o build\serial.o ^
    build\string.o build\printf.o build\memory.o build\bootmem.o build\heap.o ^
    build\pmm.o build\vmm.o build\idt.o build\irq.o build\timer.o ^
    build\keyboard.o build\syscall.o build\task.o build\scheduler.o ^
    build\shell.o build\il_runtime.o build\net.o build\ata.o build\fs.o ^
    build\vfs.o build\elf.o build\user.o build\input.o build\mouse.o ^
    build\fb.o build\psf.o build\graphics.o build\wm.o build\ipc.o ^
    build\compositor.o build\socket.o build\demo.o build\desktop.o ^
    build\apps.o build\audio.o build\usb.o build\pci.o build\services.o ^
    build\diagnostics.o build\display.o build\config.o build\power.o ^
    build\security.o build\widgets.o build\signal.o build\pipe.o ^
    build\driver.o build\raster.o build\hda.o build\gamepad.o ^
    build\interrupt_stubs.o build\context.o build\gdt.o build\io.o
if %ERRORLEVEL% neq 0 (
    echo Link failed!
    exit /b 1
)
echo Kernel linked successfully: build\kernel.elf

if not exist iso\boot\grub mkdir iso\boot\grub
copy /Y build\kernel.elf iso\boot\kernel.elf >nul

echo Creating ISO...
%XORRISO% -as mkisofs -o build\charisos.iso -b boot/grub/grub.cfg -no-emul-boot -boot-load-size 4 -iso-level 3 -quiet iso
if %ERRORLEVEL% neq 0 (
    echo ISO creation failed!
    exit /b 1
)
echo Build complete. Output: build\charisos.iso
exit /b 0

:clean
rmdir /s /q build 2>nul
if exist iso\boot\kernel.elf del /q iso\boot\kernel.elf
if exist build\charisos.iso del /q build\charisos.iso
exit /b 0