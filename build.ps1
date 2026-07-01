$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build"
$ResourceObj = Join-Path $BuildDir "resource.o"
$OutputExe = Join-Path $BuildDir "DrawCursor.exe"

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$Gcc = (Get-Command gcc -ErrorAction Stop).Source
$Windres = (Get-Command windres -ErrorAction Stop).Source

Push-Location (Join-Path $Root "res")
try {
    & $Windres `
        --input-format=rc `
        --output-format=coff `
        --codepage=65001 `
        -i "resource.rc" `
        -o $ResourceObj
    if ($LASTEXITCODE -ne 0) {
        throw "windres failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

& $Gcc `
    (Join-Path $Root "src\main.c") `
    $ResourceObj `
    -o $OutputExe `
    -O2 `
    -Wall `
    -Wextra `
    -municode `
    -mwindows `
    -finput-charset=UTF-8 `
    -lshell32 `
    -luser32 `
    -lgdi32

if ($LASTEXITCODE -ne 0) {
    throw "gcc failed with exit code $LASTEXITCODE"
}

Write-Host "Built $OutputExe"
