@echo off
cd /d %~dp0
if not exist build\charisos.iso (
    echo build\charisos.iso not found. Run build.bat first.
    exit /b 1
)
qemu-system-x86_64 -cdrom build\charisos.iso -m 256M -serial file:serial.log
