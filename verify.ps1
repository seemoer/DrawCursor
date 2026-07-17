$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Source = Join-Path $Root "src\main.c"
$CursorTestSource = Join-Path $Root "tests\cursor_pixels_test.c"
$CursorTestExecutable = Join-Path $Root "build\cursor-pixels-test.exe"
$Executable = Join-Path $Root "build\DrawCursor.exe"
$Profile = Join-Path $Root "build\logs\drawcursor-profile.csv"
$ExpectedColumns = 45

$Gcc = (Get-Command gcc -ErrorAction Stop).Source

& $Gcc `
    -fsyntax-only `
    -Wall `
    -Wextra `
    -Werror `
    -municode `
    -finput-charset=UTF-8 `
    $Source
if ($LASTEXITCODE -ne 0) {
    throw "gcc syntax check failed with exit code $LASTEXITCODE"
}

& (Join-Path $Root "build.ps1")

& $Gcc `
    $CursorTestSource `
    -o $CursorTestExecutable `
    -O2 `
    -Wall `
    -Wextra `
    -Werror `
    -finput-charset=UTF-8 `
    -lshell32 `
    -luser32 `
    -lgdi32
if ($LASTEXITCODE -ne 0) {
    throw "cursor pixel test build failed with exit code $LASTEXITCODE"
}

& $CursorTestExecutable
if ($LASTEXITCODE -ne 0) {
    throw "cursor pixel test failed with exit code $LASTEXITCODE"
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class DrawCursorVerifyNative
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string className, string windowName);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
}
'@

$ClassName = "DrawCursor.MainWindow"
$WindowName = "DrawCursor"
if ([DrawCursorVerifyNative]::FindWindow($ClassName, $WindowName) -ne [IntPtr]::Zero) {
    throw "DrawCursor is already running; close it before verification"
}

$ProfileBackup = "$Profile.verify-backup-$PID"
$HadExistingProfile = Test-Path -LiteralPath $Profile
if ($HadExistingProfile) {
    Move-Item -LiteralPath $Profile -Destination $ProfileBackup
}

try {
    $DefaultProcess = Start-Process -FilePath $Executable -PassThru -WindowStyle Hidden
    try {
        $DefaultWindow = [IntPtr]::Zero
        for ($attempt = 0; $attempt -lt 30; ++$attempt) {
            Start-Sleep -Milliseconds 50
            $DefaultWindow = [DrawCursorVerifyNative]::FindWindow($ClassName, $WindowName)
            if ($DefaultWindow -ne [IntPtr]::Zero) { break }
        }
        if ($DefaultWindow -eq [IntPtr]::Zero) { throw "DrawCursor default mode did not create its main window" }
        [void][DrawCursorVerifyNative]::PostMessage($DefaultWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
        if (-not $DefaultProcess.WaitForExit(5000)) { throw "DrawCursor default mode did not exit" }
    }
    finally {
        if (-not $DefaultProcess.HasExited) { Stop-Process -Id $DefaultProcess.Id -Force }
    }
    if (Test-Path -LiteralPath $Profile) {
        throw "default mode unexpectedly created a full profiling CSV"
    }

    $Process = Start-Process -FilePath $Executable -ArgumentList "--profile" -PassThru -WindowStyle Hidden
    try {
        $Window = [IntPtr]::Zero
        for ($attempt = 0; $attempt -lt 30; ++$attempt) {
            Start-Sleep -Milliseconds 50
            $Window = [DrawCursorVerifyNative]::FindWindow($ClassName, $WindowName)
            if ($Window -ne [IntPtr]::Zero) {
                break
            }
        }
        if ($Window -eq [IntPtr]::Zero) {
            throw "DrawCursor did not create its main window"
        }

        Start-Sleep -Milliseconds 150
        [void][DrawCursorVerifyNative]::PostMessage(
            $Window,
            0x0010,
            [IntPtr]::Zero,
            [IntPtr]::Zero)
        if (-not $Process.WaitForExit(5000)) {
            throw "DrawCursor did not exit within five seconds"
        }
    }
    finally {
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force
        }
    }

    $Lines = @(Get-Content -LiteralPath $Profile)
    if ($Lines.Count -lt 2) {
        throw "profiling CSV does not contain a header and data row"
    }

    $HeaderColumns = $Lines[0].Split(',').Count
    if ($HeaderColumns -ne $ExpectedColumns) {
        throw "profiling header has $HeaderColumns columns; expected $ExpectedColumns"
    }

    for ($index = 1; $index -lt $Lines.Count; ++$index) {
        $DataColumns = $Lines[$index].Split(',').Count
        if ($DataColumns -ne $ExpectedColumns) {
            throw "profiling row $index has $DataColumns columns; expected $ExpectedColumns"
        }
    }
}
finally {
    if ($HadExistingProfile -and (Test-Path -LiteralPath $ProfileBackup)) {
        Remove-Item -LiteralPath $Profile -Force -ErrorAction SilentlyContinue
        Move-Item -LiteralPath $ProfileBackup -Destination $Profile
    }
}

Write-Host "Verification passed: syntax, build, cursor pixels, lightweight default, startup/shutdown, and opt-in $ExpectedColumns-column profiling CSV"
