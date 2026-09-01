@echo off
setlocal EnableExtensions
chcp 65001 >nul
cd /d "%~dp0"

set "NO_PAUSE="
if /i "%~1"=="--no-pause" set "NO_PAUSE=1"

call :find_tools
set "NEED_TOOLCHAIN="
if not defined GCC set "NEED_TOOLCHAIN=1"
if not defined WINDRES set "NEED_TOOLCHAIN=1"
if defined NEED_TOOLCHAIN call :configure_tools
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
set "VERSION_HEADER=%BUILD_DIR%\version.h"

if not exist "%ROOT%VERSION" (
    echo VERSION file was not found.
    goto :failed
)
set /p "APP_VERSION="<"%ROOT%VERSION"
echo(%APP_VERSION%| findstr /r "^[0-9][0-9]*[.][0-9][0-9]*[.][0-9][0-9]*$" >nul
if errorlevel 1 (
    echo VERSION must use the format major.minor.patch, for example 1.0.0.
    goto :failed
)
for /f "tokens=1-3 delims=." %%A in ("%APP_VERSION%") do (
    set "VERSION_MAJOR=%%A"
    set "VERSION_MINOR=%%B"
    set "VERSION_PATCH=%%C"
)
set "OUTPUT_EXE=%BUILD_DIR%\DrawCursor-%APP_VERSION%.exe"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

>"%VERSION_HEADER%" echo #define DRAWCURSOR_VERSION_NUMERIC %VERSION_MAJOR%,%VERSION_MINOR%,%VERSION_PATCH%,0
>>"%VERSION_HEADER%" echo #define DRAWCURSOR_VERSION_STRING "%APP_VERSION%"
>>"%VERSION_HEADER%" echo #define DRAWCURSOR_ORIGINAL_FILENAME "DrawCursor-%APP_VERSION%.exe"

pushd "%ROOT%res"
"%WINDRES%" --input-format=rc --output-format=coff --codepage=65001 -I "%BUILD_DIR%" -i "resource.rc" -o "%RESOURCE_OBJ%"
set "BUILD_RESULT=%ERRORLEVEL%"
popd
if not "%BUILD_RESULT%"=="0" goto :failed

"%GCC%" "%ROOT%src\main.c" "%RESOURCE_OBJ%" -o "%OUTPUT_EXE%" -O2 -Wall -Wextra -municode -mwindows -finput-charset=UTF-8 -lshell32 -luser32 -lgdi32
set "BUILD_RESULT=%ERRORLEVEL%"
if exist "%RESOURCE_OBJ%" del /q "%RESOURCE_OBJ%"
if exist "%VERSION_HEADER%" del /q "%VERSION_HEADER%"
if not "%BUILD_RESULT%"=="0" goto :failed

echo.
echo Build complete:
echo %OUTPUT_EXE%
goto :finished

:find_tools
set "GCC="
set "WINDRES="
for /f "delims=" %%I in ('where gcc.exe 2^>nul') do if not defined GCC if exist "%%~dpIwindres.exe" (
    set "GCC=%%I"
    set "WINDRES=%%~dpIwindres.exe"
)
if not defined GCC if exist "C:\msys64\ucrt64\bin\gcc.exe" if exist "C:\msys64\ucrt64\bin\windres.exe" (
    set "GCC=C:\msys64\ucrt64\bin\gcc.exe"
    set "WINDRES=C:\msys64\ucrt64\bin\windres.exe"
)
exit /b 0

:configure_tools
echo.
echo No usable GCC toolchain was found.
echo [1] Install MSYS2 and GCC automatically
echo [2] Choose an existing toolchain folder
echo [3] Cancel
choice /c 123 /n /m "Select [1/2/3]: "
if errorlevel 3 exit /b 1
if errorlevel 2 goto :select_tools
goto :install_tools

:select_tools
echo.
echo Enter the bin folder that contains gcc.exe and windres.exe.
echo You can also drag the folder into this window.
set "SELECTED_TOOLCHAIN="
set /p "SELECTED_TOOLCHAIN=Toolchain folder: "
if not defined SELECTED_TOOLCHAIN exit /b 1
set "SELECTED_TOOLCHAIN=%SELECTED_TOOLCHAIN:"=%"
for %%I in ("%SELECTED_TOOLCHAIN%") do set "SELECTED_TOOLCHAIN=%%~fI"

if not exist "%SELECTED_TOOLCHAIN%\gcc.exe" (
    echo gcc.exe was not found in that folder.
    exit /b 1
)
if not exist "%SELECTED_TOOLCHAIN%\windres.exe" (
    echo windres.exe was not found in that folder.
    exit /b 1
)

call :add_user_path
if errorlevel 1 exit /b 1
set "PATH=%SELECTED_TOOLCHAIN%;%PATH%"
echo Toolchain is ready and available through PATH.
exit /b 0

:add_user_path
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$p=[IO.Path]::GetFullPath($env:SELECTED_TOOLCHAIN).TrimEnd('\'); $u=[Environment]::GetEnvironmentVariable('Path','User'); $m=[Environment]::GetEnvironmentVariable('Path','Machine'); $items=@(($u+';'+$m) -split ';' | ForEach-Object { $_.Trim().TrimEnd('\') }); if ($items -notcontains $p) { $new=if ([string]::IsNullOrWhiteSpace($u)) { $p } else { $u.TrimEnd(';')+';'+$p }; [Environment]::SetEnvironmentVariable('Path',$new,'User') }"
if errorlevel 1 (
    echo Could not update the current user's PATH.
    exit /b 1
)
exit /b 0

:install_tools
echo.
echo Installing MSYS2 and its UCRT64 GCC toolchain...

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
if defined VERSION_HEADER if exist "%VERSION_HEADER%" del /q "%VERSION_HEADER%"
echo.
echo Build failed.
set "BUILD_RESULT=1"
goto :finished

:finished
if not defined NO_PAUSE pause
if "%BUILD_RESULT%"=="1" exit /b 1
exit /b 0
