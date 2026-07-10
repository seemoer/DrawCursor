$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Source = Join-Path $Root "src\main.c"
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
    $Process = Start-Process -FilePath $Executable -PassThru -WindowStyle Hidden
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

Write-Host "Verification passed: syntax, build, startup/shutdown, and $ExpectedColumns-column profiling CSV"
