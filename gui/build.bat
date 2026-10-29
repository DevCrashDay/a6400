@echo off
echo Building Sony a6400 Freestanding GUI (ARMv7 EABI, no libc)...
zig cc -target arm-linux-musleabi -mcpu=cortex_a5 -mfloat-abi=soft -ffreestanding -nostdlib -fno-builtin -static -s -O2 src/rt.c src/fb.c src/main.c -o a6400_gui.elf
if %ERRORLEVEL% EQU 0 (
    copy /Y a6400_gui.elf ..\a6400_gui.elf >nul
    echo [SUCCESS] Built a6400_gui.elf
) else (
    echo [ERROR] Build failed!
    exit /b 1
)
