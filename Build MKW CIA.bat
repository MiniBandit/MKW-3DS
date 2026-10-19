@echo off
rem Windows: double-click, then drag your Mario Kart Wii (USA) ISO or WBFS into this window.
rem Needs devkitPro (3DS tools), Python 3.11-3.14, Node.js LTS and Git; see README.md.
setlocal
cd /d "%~dp0"

set "PY="
where py >nul 2>nul || goto :python_path
set "PY=py -3.14"
call :checkpython
if not errorlevel 1 goto :python_ready
set "PY=py -3.13"
call :checkpython
if not errorlevel 1 goto :python_ready
set "PY=py -3.12"
call :checkpython
if not errorlevel 1 goto :python_ready
set "PY=py -3.11"
call :checkpython
if not errorlevel 1 goto :python_ready
goto :nopython

:python_path
where python >nul 2>nul || goto :nopython
set "PY=python"
call :checkpython
if errorlevel 1 goto :nopython

:python_ready
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
".venv\Scripts\python.exe" -c "import numpy, PIL, Crypto" >nul 2>nul || (echo The Python packages did not install into .venv. Delete the .venv folder and run this again. & goto :end)
rem Show the build and keep a copy in build-cia-log.txt. UTF-8 text on Windows too.
set "PYTHONUTF8=1"
set "MKW_DISC=%disc%"
rem Windows PowerShell's Tee-Object writes UTF-16, so each line goes to the log as UTF-8 instead.
set "PYTHONUNBUFFERED=1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Content -Path 'build-cia-log.txt' -Value $null -Encoding UTF8; & '.venv\Scripts\python.exe' 'tools\release\build_cia.py' $env:MKW_DISC 2>&1 | ForEach-Object { $line=$_.ToString(); $line; Add-Content -Path 'build-cia-log.txt' -Value $line -Encoding UTF8 }; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }"
if errorlevel 1 (echo Build failed. See build-cia-log.txt for details. & goto :end)
echo Log: "%CD%\build-cia-log.txt"
goto :end

:nopython
echo Python 3.11 through 3.14 is needed; Python 3.15 is not supported yet: https://www.python.org/downloads/ (tick "Add python.exe to PATH")
goto :end
:checkpython
%PY% -c "import sys; sys.exit(not ((3, 11) <= sys.version_info[:2] < (3, 15)))" >nul 2>nul
exit /b %errorlevel%
:nodisc
echo No disc image at: "%disc%"
:end
pause
