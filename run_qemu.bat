@echo off
REM ## QEMU test script for MicroRenovator
REM ## Requires: QEMU (https://qemu.weilnetz.de/w64/) in PATH or installed at default location

setlocal

set QEMU="C:\Program Files\qemu\qemu-system-x86_64.exe"
set OVMF_DIR=%~dp0
set BOOT_DIR=%~dp0boot

REM ## Check QEMU exists
if not exist %QEMU% (
    echo ERROR: QEMU not found at %QEMU%
    echo Download from: https://qemu.weilnetz.de/w64/
    exit /b 1
)

REM ## Reset VARS (fresh UEFI variable store each run)
copy /y "%OVMF_DIR%vars.fd" "%OVMF_DIR%vars_test.fd" >nul

echo === Starting QEMU with OVMF ===
echo Boot dir: %BOOT_DIR%
echo OVMF CODE: %OVMF_DIR%code.fd
echo OVMF VARS: %OVMF_DIR%vars_test.fd
echo.
echo UEFI Shell will auto-run: \EFI\MICRO\ULOAD.EFI haswell_0x22.bin
echo.

%QEMU% ^
    -machine q35 ^
    -cpu Haswell ^
    -m 2048 ^
    -smp 4 ^
    -drive if=pflash,format=raw,unit=0,file="%OVMF_DIR%code.fd",readonly=on ^
    -drive if=pflash,format=raw,unit=1,file="%OVMF_DIR%vars_test.fd" ^
    -drive file="fat:rw:%BOOT_DIR%",format=raw ^
    -net none

endlocal
