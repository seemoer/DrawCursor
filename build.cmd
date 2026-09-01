@echo off
setlocal EnableExtensions
chcp 65001 >nul
cd /d "%~dp0"

set "NO_PAUSE="
if /i "%~1"=="--no-pause" set "NO_PAUSE=1"

call :find_tools
if not defined GCC call :install_tools
if errorlevel 1 goto :failed

call :find_tools
if not defined GCC (
    echo.
    echo GCC was not found. Install MSYS2 in C:\msys64, then run this file again.
    goto :failed
)
if not defined WINDRES (
    echo.
    echo windres was not found. Reinstall the MSYS2 UCRT64 GCC package, then try again.
    goto :failed
)

set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%build"
set "RESOURCE_OBJ=%BUILD_DIR%\resource.o"
set "OUTPUT_EXE=%BUILD_DIR%\DrawCursor.exe"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

pushd "%ROOT%res"
"%WINDRES%" --input-format=rc --output-format=coff --codepage=65001 -i "resource.rc" -o "%RESOURCE_OBJ%"
set "BUILD_RESULT=%ERRORLEVEL%"
popd
if not "%BUILD_RESULT%"=="0" goto :failed

"%GCC%" "%ROOT%src\main.c" "%RESOURCE_OBJ%" -o "%OUTPUT_EXE%" -O2 -Wall -Wextra -municode -mwindows -finput-charset=UTF-8 -lshell32 -luser32 -lgdi32
set "BUILD_RESULT=%ERRORLEVEL%"
if exist "%RESOURCE_OBJ%" del /q "%RESOURCE_OBJ%"
if not "%BUILD_RESULT%"=="0" goto :failed

echo.
echo Build complete:
echo %OUTPUT_EXE%
goto :finished

:find_tools
set "GCC="
set "WINDRES="
for /f "delims=" %%I in ('where gcc.exe 2^>nul') do if not defined GCC set "GCC=%%I"
for /f "delims=" %%I in ('where windres.exe 2^>nul') do if not defined WINDRES set "WINDRES=%%I"
if exist "C:\msys64\ucrt64\bin\gcc.exe" set "GCC=C:\msys64\ucrt64\bin\gcc.exe"
if exist "C:\msys64\ucrt64\bin\windres.exe" set "WINDRES=C:\msys64\ucrt64\bin\windres.exe"
exit /b 0

:install_tools
echo.
echo A C compiler is required to build DrawCursor.
echo This script can install MSYS2 and its UCRT64 GCC toolchain with winget.
choice /c YN /n /m "Install it now? [Y/N]: "
if errorlevel 2 exit /b 1

if not exist "C:\msys64\usr\bin\bash.exe" (
    where winget.exe >nul 2>nul
    if errorlevel 1 (
        echo winget is unavailable. Install MSYS2 from https://www.msys2.org/ and try again.
        exit /b 1
    )
    winget install --exact --id MSYS2.MSYS2 --scope user --accept-package-agreements --accept-source-agreements
    if errorlevel 1 exit /b 1
)

if not exist "C:\msys64\usr\bin\bash.exe" (
    echo MSYS2 was not found in C:\msys64 after installation.
    exit /b 1
)

"C:\msys64\usr\bin\bash.exe" -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc"
if errorlevel 1 exit /b 1
exit /b 0

:failed
if defined RESOURCE_OBJ if exist "%RESOURCE_OBJ%" del /q "%RESOURCE_OBJ%"
echo.
echo Build failed.
set "BUILD_RESULT=1"
goto :finished

:finished
if not defined NO_PAUSE pause
if "%BUILD_RESULT%"=="1" exit /b 1
exit /b 0
