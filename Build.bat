@echo off
setlocal
rem Builds Mario Kart WiiDS on Windows: double-click it and pick your disc
rem image, or drop the image on this file. Anything missing is downloaded.
title Mario Kart WiiDS builder
set "MKW_BAT=%~f0"
set "MKW_DISC=%~1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$s=[IO.File]::ReadAllText($env:MKW_BAT);Invoke-Expression $s.Substring($s.IndexOf('#'+'POWERSHELL'))"
set "MKW_RESULT=%errorlevel%"
echo.
pause
exit /b %MKW_RESULT%

#POWERSHELL
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$RepoUrl = 'https://github.com/ethanseagull/MarioKartWiiDS.git'
$MinFreeGB = 30

# Testing only: MKW_BOOTSTRAP_ROOT installs everything under that folder (no
# spaces), ignoring tools already on this PC; MKW_BOOTSTRAP_ONLY stops before the build.
$Fresh = [bool]$env:MKW_BOOTSTRAP_ROOT
$Tools = if ($Fresh) { "$env:MKW_BOOTSTRAP_ROOT\tools" } else { "$env:LOCALAPPDATA\MKW-3DS-Redux\tools" }
$Dkp = if ($Fresh) { "$env:MKW_BOOTSTRAP_ROOT\devkitPro" } else { 'C:\devkitPro' }

$Git = @{
    Url = 'https://github.com/git-for-windows/git/releases/download/v2.56.0.windows.4/MinGit-2.56.0.4-64-bit.zip'
    Sha256 = 'e3bf78351fe777fba47ddee5950082f8a98bb0f02604fdc7ba3ee2cb68ffa877'
    Name = 'MinGit-2.56.0.4-64-bit.zip'
}
$Python = @{
    Url = 'https://api.nuget.org/v3-flatcontainer/python/3.12.10/python.3.12.10.nupkg'
    Sha256 = '0eb85c2dfccccf1b17352de4c397f69194035b7d37149eacc16f1147d93de3b8'
    Name = 'python.3.12.10.zip'
}
$MsysArchive = @{
    Url = 'https://github.com/msys2/msys2-installer/releases/download/2026-09-27/msys2-base-x86_64-20260927.sfx.exe'
    Sha256 = 'ad336cccfda47758b5e15cda993fbba421115cb0b126697daef1ee4dfe37209f'
    Name = 'msys2-base-x86_64-20260927.sfx.exe'
}
$DevkitProKey = 'BC26F752D25B92CE272E0F44F7FD5492264BB9D0'

function Say($Text) { Write-Host $Text -ForegroundColor Cyan }
function Fail($Text) { Write-Host "`n$Text" -ForegroundColor Red; exit 1 }

# Native programs write progress to stderr; with 'Stop' a captured line would end the script.
function Invoke-Native([scriptblock]$Command) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Command } finally { $ErrorActionPreference = $saved }
}

function Get-Pinned($Item) {
    $folder = Join-Path $Tools 'downloads'
    New-Item -ItemType Directory -Force $folder | Out-Null
    $file = Join-Path $folder $Item.Name
    if (-not (Test-Path $file) -or (Get-FileHash $file -Algorithm SHA256).Hash -ne $Item.Sha256) {
        Say "Downloading $($Item.Name)"
        try { Invoke-WebRequest -UseBasicParsing -Uri $Item.Url -OutFile "$file.part" }
        catch { Fail "Couldn't download $($Item.Url). Check your internet connection and run this again." }
        Move-Item -Force "$file.part" $file
    }
    if ((Get-FileHash $file -Algorithm SHA256).Hash -ne $Item.Sha256) {
        Remove-Item -Force $file
        Fail "$($Item.Name) didn't match its expected checksum, so it was deleted. Run this again."
    }
    return $file
}

# Same checks as check_disc in tools/build.py, so a wrong file fails before any download.
function Test-Disc($Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return "That file wasn't found." }
    $stream = [IO.File]::OpenRead($Path)
    try {
        $header = New-Object byte[] 512
        [void]$stream.Read($header, 0, 512)
        if ([Text.Encoding]::ASCII.GetString($header, 0, 4) -eq 'WBFS') {
            $slot = ($header[12..511] | Where-Object { $_ -ne 0 } | Select-Object -First 1)
            if ($header[8] -lt 9 -or $header[8] -gt 20 -or -not $slot) { return 'That WBFS file is invalid.' }
            [void]$stream.Seek([int64]$slot * [int64][math]::Pow(2, $header[8]), 'Begin')
            [void]$stream.Read($header, 0, 256)
        }
    } finally { $stream.Dispose() }
    if ([Text.Encoding]::ASCII.GetString($header, 0, 6) -ne 'RMCE01') {
        return 'This needs Mario Kart Wii USA (RMCE01) as an ISO or WBFS.'
    }
    if ($header[7] -ne 0 -or ([BitConverter]::ToString($header, 24, 4) -ne '5D-1C-9E-A3')) {
        return 'This needs the normal RMCE01 revision 0 disc image.'
    }
    return ''
}

function Select-Disc {
    Add-Type -AssemblyName System.Windows.Forms
    $path = $env:MKW_DISC
    while ($true) {
        if ($path) {
            $problem = Test-Disc $path
            if (-not $problem) { return (Resolve-Path -LiteralPath $path).ProviderPath }
            Write-Host $problem -ForegroundColor Yellow
        }
        $owner = New-Object Windows.Forms.Form -Property @{ TopMost = $true; ShowInTaskbar = $false; Opacity = 0 }
        $owner.Show()
        $dialog = New-Object Windows.Forms.OpenFileDialog -Property @{
            Title = 'Select your Mario Kart Wii (USA) disc image'
            Filter = 'Wii disc image (*.iso;*.wbfs)|*.iso;*.wbfs'
        }
        $picked = $dialog.ShowDialog($owner)
        $owner.Close()
        if ($picked -ne [Windows.Forms.DialogResult]::OK) { Say 'No disc selected, so nothing was built.'; exit 0 }
        $path = $dialog.FileName
    }
}

function Add-Git {
    if (-not $Fresh -and (Get-Command git -ErrorAction SilentlyContinue)) { return }
    $folder = Join-Path $Tools 'mingit'
    if (-not (Test-Path "$folder\cmd\git.exe")) {
        Expand-Archive -Force -LiteralPath (Get-Pinned $Git) -DestinationPath $folder
    }
    $env:PATH = "$folder\cmd;$env:PATH"
}

function Find-Python {
    if (-not $Fresh) {
        foreach ($try in @(@{ Cmd = 'py'; Arg = @('-3.12') }, @{ Cmd = 'python'; Arg = @() })) {
            if (-not (Get-Command $try.Cmd -ErrorAction SilentlyContinue)) { continue }
            $exe = Invoke-Native {
                & $try.Cmd @($try.Arg) -c 'import sys; print(sys.executable if sys.version_info[:2]==(3,12) else str())' 2>$null
            } | Select-Object -Last 1
            if ($exe -and (Test-Path -LiteralPath $exe)) { return $exe }
        }
    }
    $folder = Join-Path $Tools 'python-3.12.10'
    if (-not (Test-Path "$folder\tools\python.exe")) {
        Expand-Archive -Force -LiteralPath (Get-Pinned $Python) -DestinationPath $folder
    }
    return "$folder\tools\python.exe"
}

function Test-DevkitPro {
    foreach ($file in 'devkitARM\bin\arm-none-eabi-g++.exe', 'devkitARM\bin\arm-none-eabi-gcc.exe',
            'devkitARM\bin\arm-none-eabi-ar.exe', 'tools\bin\picasso.exe', 'tools\bin\tex3ds.exe',
            'tools\bin\3dsxtool.exe', 'libctru\lib\libctru.a', 'msys2\usr\bin\g++.exe') {
        if (-not (Test-Path (Join-Path $Dkp $file))) { return $false }
    }
    return $true
}

function Invoke-Msys($Script) {
    Invoke-Native { & "$Dkp\msys2\usr\bin\bash.exe" -lc $Script }
    if ($LASTEXITCODE) { Fail "Setting up devkitPro failed at: $Script`nCheck your internet connection and run this again." }
}

function Install-DevkitPro {
    if (Test-DevkitPro) { return }
    Say 'Setting up devkitPro (the 3DS compiler). This takes a few minutes.'
    $msys = "$Dkp\msys2"
    if (-not (Test-Path "$msys\usr\bin\bash.exe")) {
        New-Item -ItemType Directory -Force $Dkp | Out-Null
        $archive = Get-Pinned $MsysArchive
        Invoke-Native { & $archive -y "-o$Dkp" | Out-Null }
        if ($LASTEXITCODE) { Fail "Couldn't unpack MSYS2 into $Dkp." }
        Rename-Item "$Dkp\msys64" 'msys2'
    }
    $fstab = "$msys\etc\fstab"
    if ((Get-Content -Raw $fstab) -notmatch '/opt/devkitpro') {
        [IO.File]::AppendAllText($fstab, "`n$($Dkp -replace '\\', '/') /opt/devkitpro`n")
    }
    $conf = "$msys\etc\pacman.conf"
    if ((Get-Content -Raw $conf) -notmatch 'dkp-libs') {
        [IO.File]::AppendAllText($conf, "`n[dkp-libs]`nServer = https://pkg.devkitpro.org/packages`n`n[dkp-windows]`nServer = https://pkg.devkitpro.org/packages/windows/`$arch`n")
    }
    $env:MSYSTEM = 'MSYS'
    $env:CHERE_INVOKING = '1'
    Invoke-Msys 'true'
    Invoke-Msys "pacman -Q devkitpro-keyring >/dev/null 2>&1 || { pacman-key --recv-keys $DevkitProKey --keyserver keyserver.ubuntu.com && pacman-key --lsign-key $DevkitProKey && pacman -U --noconfirm https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.zst; }"
    Invoke-Msys 'pacman -Sy --noconfirm'
    # Updating msys2-runtime can make pacman report errors after it has finished, so check the files.
    foreach ($attempt in 1, 2) {
        Remove-Item -Force -ErrorAction SilentlyContinue "$Dkp\msys2\var\lib\pacman\db.lck"
        Invoke-Native { & "$Dkp\msys2\usr\bin\bash.exe" -lc 'pacman -S --noconfirm --needed 3ds-dev gcc' }
        if (Test-DevkitPro) { break }
    }
    if (-not (Test-DevkitPro)) { Fail "devkitPro was installed but some files are missing in $Dkp." }
}

$Disc = Select-Disc
Say "Disc: $Disc"

$Here = Split-Path -Parent $env:MKW_BAT
$free = (Get-PSDrive ([IO.Path]::GetPathRoot($Here).Substring(0, 1))).Free / 1GB
if ($free -lt $MinFreeGB) { Fail ("The build needs about {0} GB free on this drive; it has {1:N0} GB." -f $MinFreeGB, $free) }

Add-Git
$Repo = $Here
if (-not (Test-Path "$Here\.git")) {
    # A lone Build.bat or a ZIP download: the builder needs a git clone.
    $Repo = Join-Path $Here 'MarioKartWii-3DS-Redux'
    if (Test-Path "$Repo\.git") {
        Say 'Updating the code'
        Invoke-Native { git -C $Repo pull --ff-only }
        if ($LASTEXITCODE) { Fail "Couldn't update the code in $Repo. Check your internet connection. If you changed files there, undo the changes or delete that folder. Then run this again." }
    } else {
        Say 'Getting the code'
        Invoke-Native { git clone $RepoUrl $Repo }
        if ($LASTEXITCODE) { Fail "Couldn't download the code from $RepoUrl." }
    }
}

if ($Repo -eq $Here -and -not $Fresh) {
    Say 'Updating the code'
    Invoke-Native { git -C $Repo pull --ff-only }
    if ($LASTEXITCODE) { Fail "Couldn't update the code in $Repo. Check your internet connection, and undo any changes you made to its files. Then run this again." }
}
$PythonExe = Find-Python
$cfg = "$Repo\.venv\pyvenv.cfg"
if ((Test-Path $cfg) -and (Get-Content -Raw $cfg) -notmatch '(?m)^version\s*=\s*3\.12') {
    Say 'Replacing a Python environment made with another version'
    Remove-Item -Recurse -Force "$Repo\.venv"
}
Install-DevkitPro
$env:DEVKITPRO = $Dkp
$env:DEVKITPRO_WIN = $Dkp
if ($env:MKW_BOOTSTRAP_ONLY) { Say "Tools ready. Python: $PythonExe"; exit 0 }

Say 'Building. The first build takes about an hour and needs nothing from you.'
Set-Location -LiteralPath $Repo
Invoke-Native { & $PythonExe tools/build.py --disc $Disc --delete-converted }
if ($LASTEXITCODE) { Fail "The build stopped. Run this again to continue where it left off.`nLogs are in $Repo\build\standard\logs" }

$Output = "$Repo\build\standard\output"
Write-Host "`nDone. Your files are in $Output" -ForegroundColor Green
Write-Host '1. Copy the sd\3ds folder to the root of your SD card.'
Write-Host '2. Install the .cia file with FBI.'
if (-not $Fresh) { Start-Process explorer.exe $Output }
exit 0
