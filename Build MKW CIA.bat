@echo off
rem Windows: double-click, then drag your Mario Kart Wii (USA) ISO or WBFS into this window.
rem Anything the build needs and this computer lacks (Python, Git, Node.js, a C++ compiler,
rem devkitPro) is installed first, once you agree: tools\windows_setup.ps1.
setlocal
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "tools\windows_setup.ps1" -PythonOut "build\python-path.txt"
if errorlevel 1 goto :end
rem Programs installed just now aren't on this window's PATH yet: read it again.
for /f "usebackq delims=" %%p in (`powershell -NoProfile -Command "[Environment]::GetEnvironmentVariable('Path','Machine')+';'+[Environment]::GetEnvironmentVariable('Path','User')"`) do set "PATH=%%p;%PATH%"
set "PYEXE="
set /p PYEXE=<"build\python-path.txt"
if not defined PYEXE goto :nopython
set PY="%PYEXE%"
where git >nul 2>nul || (echo Git is needed: https://git-scm.com/download/win & goto :end)
where node >nul 2>nul || (echo Node.js LTS is needed: https://nodejs.org & goto :end)

set "disc="
set /p "disc=Drag your Mario Kart Wii (USA) disc image here and press Enter: "
if not defined disc goto :nodisc
set "disc=%disc:"=%"
if not exist "%disc%" goto :nodisc

if not exist "vendor\gclib" (%PY% tools\bootstrap.py || goto :end)
if not exist ".venv\Scripts\python.exe" (%PY% -m venv .venv || goto :end)
".venv\Scripts\python.exe" -c "import sys; sys.exit(not ((3, 11) <= sys.version_info[:2] < (3, 15)))" || (echo The existing .venv uses an unsupported Python version. Delete .venv and rerun with Python 3.11 through 3.14. & goto :end)
rem Install the packages whenever the build environment cannot import them
rem (a marker file alone could say "installed" after a failed or partial install).
".venv\Scripts\python.exe" -c "import numpy, PIL, Crypto" >nul 2>nul || (
    ".venv\Scripts\python.exe" -m pip install -r requirements.txt || goto :end
)
".venv\Scripts\python.exe" -c "import numpy, PIL, Crypto" || (echo The Python packages above did not load. Delete the .venv folder and run this again. If it happens again, share this message in an issue. & goto :end)
rem Show the build and keep a copy in build-cia-log.txt. UTF-8 text on Windows too.
set "PYTHONUTF8=1"
set "MKW_DISC=%disc%"
rem Windows PowerShell's Tee-Object writes UTF-16, so each line goes to the log as UTF-8 instead.
rem It exits with Python's own code: Windows PowerShell can otherwise end with 1 when Python wrote to stderr.
set "PYTHONUNBUFFERED=1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Content -Path 'build-cia-log.txt' -Value $null -Encoding UTF8; & '.venv\Scripts\python.exe' 'tools\release\build_cia.py' $env:MKW_DISC 2>&1 | ForEach-Object { $line=$_.ToString(); $line; Add-Content -Path 'build-cia-log.txt' -Value $line -Encoding UTF8 }; exit $LASTEXITCODE"
if errorlevel 1 (echo Build failed. See build-cia-log.txt for details. & goto :end)
echo Log: "%CD%\build-cia-log.txt"
goto :end

:nopython
echo Python 3.11 through 3.14 is needed; Python 3.15 is not supported yet: https://www.python.org/downloads/ (tick "Add python.exe to PATH")
goto :end
:nodisc
echo No disc image at: "%disc%"
:end
pause
