@echo off
REM ## A script to build Uload.efi and dependencies on Windows
REM ## Requires:
REM ##   - Visual Studio 2022 (or later) with C/C++ workload
REM ##   - NASM (https://www.nasm.us/) in PATH
REM ##   - Python 3.x in PATH
REM ##   - EDK2 base tools will be built automatically

setlocal enabledelayedexpansion

echo === MicroRenovator Windows Build Script ===
echo.

REM ## Check for Visual Studio
where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: cl.exe not found. Run this from a VS Developer Command Prompt.
    echo.
    echo Options:
    echo   1. Open "Developer Command Prompt for VS 2022" from Start Menu
    echo   2. Or run: "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
    exit /b 1
)

REM ## Check for NASM
where nasm.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: nasm.exe not found in PATH.
    echo Download from https://www.nasm.us/ and add to PATH.
    exit /b 1
)

REM ## Check for Python
where py >nul 2>&1
if %errorlevel% neq 0 (
    where python >nul 2>&1
    if !errorlevel! neq 0 (
        echo ERROR: Python not found. Install Python 3.x and add to PATH.
        exit /b 1
    )
    set PYTHON_COMMAND=python
) else (
    set PYTHON_COMMAND=py -3
)

echo Using: cl.exe, nasm, %PYTHON_COMMAND%
echo.

REM ## Set EDK2 environment
set WORKSPACE=%~dp0edk2
set EDK_TOOLS_PATH=%WORKSPACE%\BaseTools
set CONF_PATH=%WORKSPACE%\Conf
set PYTHONIOENCODING=utf-8

REM ## Apply Shell.c patch if not already applied
echo Checking Shell.c patch...
findstr /C:"NoInterrupt  = TRUE" "%WORKSPACE%\ShellPkg\Application\Shell\Shell.c" >nul 2>&1
if %errorlevel% neq 0 (
    echo Applying Shell.c patch ^(NoInterrupt = TRUE^)...
    powershell -Command "(Get-Content '%WORKSPACE%\ShellPkg\Application\Shell\Shell.c') -replace 'NoInterrupt  = FALSE', 'NoInterrupt  = TRUE' | Set-Content '%WORKSPACE%\ShellPkg\Application\Shell\Shell.c'"
    echo Done.
) else (
    echo Shell.c already patched.
)
echo.

REM ## Build BaseTools
echo Building BaseTools...
cd /d "%WORKSPACE%"
nmake
if %errorlevel% neq 0 (
    echo ERROR: BaseTools build failed.
    exit /b 1
)
echo BaseTools built successfully.
echo.

REM ## Build Shell.efi
echo Building Shell.efi...
build -a X64 -p ShellPkg\ShellPkg.dsc -b RELEASE -t VS2022
if %errorlevel% neq 0 (
    echo ERROR: Shell.efi build failed.
    exit /b 1
)
echo Shell.efi built successfully.
echo.

REM ## Build Uload.efi
echo Building Uload.efi...
build -a X64 -p Uload\Uload.dsc -b RELEASE -t VS2022
if %errorlevel% neq 0 (
    echo ERROR: Uload.efi build failed.
    exit /b 1
)
echo Uload.efi built successfully.
echo.

REM ## Show results
echo === Build Complete ===
echo Shell.efi: %WORKSPACE%\Build\Shell\RELEASE_VS2022\X64\ShellPkg\Application\Shell\Shell\OUTPUT\Shell.efi
echo Uload.efi: %WORKSPACE%\Build\Uload\RELEASE_VS2022\X64\Uload.efi
echo.

endlocal
