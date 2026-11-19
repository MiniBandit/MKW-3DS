# SPDX-License-Identifier: LicenseRef-MKW3DS
# Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
#
# Windows builder, step one ("Build MKW CIA.bat" runs this first): finds what the
# build needs and, once the player agrees, installs whatever is missing:
#   Python 3.13, Git, Node.js LTS and a C++ compiler (LLVM-MinGW) with winget,
#   devkitPro with its own installer (the player ticks "3DS Development").
# Writes the Python to build with (a full path) to -PythonOut. Exit code 0 when
# everything is there, 1 otherwise (with what to do printed).
# Windows PowerShell 5.1: no newer syntax.
param([string]$PythonOut = 'build\python-path.txt')

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is far slower with its progress bar

function Update-SessionPath {
    # Installers write PATH to the registry; this window only sees it after a refresh.
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}

# A Python the builder supports (3.11 to 3.14), as a full path, or $null.
# The Microsoft Store's "python" placeholder fails the check and doesn't count.
function Find-Python {
    # No double quotes: Windows PowerShell 5.1 passes them to programs mangled.
    $check = 'import sys; print(sys.executable); sys.exit(0 if (3, 11) <= sys.version_info[:2] < (3, 15) else 1)'
    $tries = @()
    if (Get-Command py -ErrorAction SilentlyContinue) { foreach ($v in '3.14', '3.13', '3.12', '3.11') { $tries += , @('py', "-$v") } }
    if (Get-Command python -ErrorAction SilentlyContinue) { $tries += , @('python') }
    foreach ($v in '314', '313', '312', '311') {
        foreach ($folder in @("$env:LOCALAPPDATA\Programs\Python\Python$v", "$env:ProgramFiles\Python$v")) {
            if (Test-Path "$folder\python.exe") { $tries += , @("$folder\python.exe") }
        }
    }
    foreach ($try in $tries) {
        $rest = @(); if ($try.Count -gt 1) { $rest = $try[1..($try.Count - 1)] }
        try { $out = & $try[0] @rest -c $check 2>$null } catch { continue }
        if ($LASTEXITCODE -ne 0 -or -not $out) { continue }
        $path = ([string]$out).Trim()
        if (Test-Path -LiteralPath $path) { return $path }
    }
    return $null
}

function Test-Compiler([string]$python) {
    if ($python) {
        # The builder's own search (tools\host_tools.py): PATH, winget, MSYS2, llvm-mingw, w64devkit.
        & $python -c "import sys; sys.path.insert(0, 'tools'); import host_tools; host_tools.compiler()" *> $null
        return ($LASTEXITCODE -eq 0)
    }
    if (Get-Command clang++ -ErrorAction SilentlyContinue) { return $true }
    if (Get-Command g++ -ErrorAction SilentlyContinue) { return $true }
    return [bool](Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW*" -ErrorAction SilentlyContinue)
}

# devkitPro with the 3DS tools, on any drive (as tools\devkitpro.py looks), or $null.
function Find-DevkitPro {
    $places = @()
    if ($env:DEVKITPRO_WIN) { $places += $env:DEVKITPRO_WIN }
    foreach ($drive in 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J') { foreach ($name in 'devkitPro', 'devkitpro') { $places += "$($drive):\$name" } }
    foreach ($p in $places) {
        if ((Test-Path "$p\devkitARM") -and (Test-Path "$p\libctru") -and (Test-Path "$p\tools\bin\tex3ds.exe")) { return $p }
    }
    return $null
}
function Test-DevkitProFolder {
    foreach ($drive in 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J') { foreach ($name in 'devkitPro', 'devkitpro') { if (Test-Path "$($drive):\$name") { return $true } } }
    return $false
}

# For the .bat to read with "set /p": in this console's code page, so a user
# folder with accented letters reaches it intact.
function Save-Python([string]$python) {
    $folder = Split-Path $PythonOut
    if ($folder) { New-Item -ItemType Directory -Force -Path $folder | Out-Null }
    [IO.File]::WriteAllText((Join-Path (Get-Location).Path $PythonOut), $python, [Text.Encoding]::GetEncoding([Console]::OutputEncoding.CodePage))
}

function Get-Missing([string]$python) {
    $missing = @()
    if (-not $python) { $missing += 'Python' }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { $missing += 'Git' }
    if (-not (Get-Command node -ErrorAction SilentlyContinue)) { $missing += 'Node.js' }
    if (-not (Test-Compiler $python)) { $missing += 'C++ compiler' }
    if (-not (Find-DevkitPro)) { $missing += 'devkitPro' }
    return , $missing
}

function Install-WithWinget([string]$id, [string]$name, [string[]]$extra) {
    Write-Host ""
    Write-Host "Installing $name..." -ForegroundColor Cyan
    Write-Host "If Windows asks whether to allow changes, choose Yes."
    & winget install --id $id --exact --silent --accept-package-agreements --accept-source-agreements @extra
    $code = $LASTEXITCODE
    Update-SessionPath
    # 0 installed; -1978335189 nothing newer to install; -1978335135 already installed.
    if ($code -ne 0 -and $code -ne -1978335189 -and $code -ne -1978335135) {
        Write-Host "winget couldn't install $name (code $code)." -ForegroundColor Yellow
    }
}

function Install-DevkitPro {
    $version = '3.0.3'; $size = 196304
    $url = "https://github.com/devkitPro/installer/releases/download/v$version/devkitProUpdater-$version.exe"
    $file = Join-Path $env:TEMP "devkitProUpdater-$version.exe"
    Write-Host ""
    Write-Host "Downloading devkitPro's installer (from devkitPro's GitHub page)..." -ForegroundColor Cyan
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $url -OutFile $file -UseBasicParsing
    } catch {
        Write-Host "The download failed: $($_.Exception.Message)" -ForegroundColor Yellow
        return
    }
    if ((Get-Item $file).Length -ne $size) {
        Write-Host "The downloaded installer isn't the expected file, so it wasn't run." -ForegroundColor Yellow
        Remove-Item $file -ErrorAction SilentlyContinue
        return
    }
    Write-Host ""
    Write-Host "devkitPro's installer opens now:" -ForegroundColor Cyan
    Write-Host "  - Keep its choices, but make sure '3DS Development' is ticked."
    Write-Host "  - Let it finish and close it. This window carries on after that."
    if (Test-DevkitProFolder) {
        Write-Host "  - devkitPro is already partly installed: choose to change or update it, and tick '3DS Development'."
    }
    Start-Process -FilePath $file -Wait
    Remove-Item $file -ErrorAction SilentlyContinue
    Update-SessionPath
}

$python = Find-Python
$missing = Get-Missing $python
if ($missing.Count -eq 0) {
    Save-Python $python
    exit 0
}

Write-Host ""
Write-Host "Building the game needs some free programs this computer doesn't have yet:" -ForegroundColor Cyan
foreach ($m in $missing) { Write-Host "  - $m" }
Write-Host ""
Write-Host "They can be installed for you now (a few GB of downloads in all). Windows may"
Write-Host "ask for permission a few times: choose Yes."
Write-Host ""
Read-Host "Press Enter to install them, or close this window to stop" | Out-Null

$winget = [bool](Get-Command winget -ErrorAction SilentlyContinue)
$needsWinget = @($missing | Where-Object { $_ -ne 'devkitPro' }).Count -gt 0
if ($needsWinget -and -not $winget) {
    Write-Host ""
    Write-Host "This Windows doesn't have winget, which installs them. Get 'App Installer' from the" -ForegroundColor Yellow
    Write-Host "Microsoft Store (it adds winget), then run the builder again; or install them yourself:" -ForegroundColor Yellow
    Write-Host "  Python 3.13: https://www.python.org/downloads/ (tick 'Add python.exe to PATH')"
    Write-Host "  Git: https://git-scm.com/download/win      Node.js LTS: https://nodejs.org"
    Write-Host "  C++ compiler: https://github.com/mstorsjo/llvm-mingw/releases"
    if ($missing -contains 'devkitPro') { Install-DevkitPro }
    exit 1
}

# Windows on ARM: x64 Python (run through Windows' x64 emulation), so the
# builder's packages (numpy, Pillow) and pinned x64 tools all match (#129).
$pythonArch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { @('--architecture', 'x64') } else { @() }
if ($missing -contains 'Python') { Install-WithWinget 'Python.Python.3.13' 'Python 3.13' (@('--scope', 'user') + $pythonArch); $python = Find-Python }
if ($missing -contains 'Git') { Install-WithWinget 'Git.Git' 'Git' @() }
if ($missing -contains 'Node.js') { Install-WithWinget 'OpenJS.NodeJS.LTS' 'Node.js LTS' @() }
if ($missing -contains 'C++ compiler' -and -not (Test-Compiler $python)) { Install-WithWinget 'MartinStorsjo.LLVM-MinGW.UCRT' 'a C++ compiler (LLVM-MinGW)' @() }
if ($missing -contains 'devkitPro') { Install-DevkitPro }

$python = Find-Python
$still = Get-Missing $python
if ($still.Count -gt 0) {
    Write-Host ""
    Write-Host "Still missing: $($still -join ', ')." -ForegroundColor Yellow
    if ($still -contains 'devkitPro') {
        Write-Host "devkitPro: run its installer again from https://github.com/devkitPro/installer/releases and tick '3DS Development'."
        Write-Host "If you put it somewhere other than C:\devkitPro, set DEVKITPRO_WIN to that folder."
    }
    Write-Host "Close this window and run the builder again; if it still says this, share this window's text in an issue."
    exit 1
}
Save-Python $python
Write-Host ""
Write-Host "Everything the build needs is installed." -ForegroundColor Green
exit 0
