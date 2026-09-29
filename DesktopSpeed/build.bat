@echo off
REM Builds a single-file DesktopSpeed.exe for Windows.
REM Output: ..\out\DesktopSpeed.exe
REM
REM For Linux/macOS, run build.sh instead on that machine — PyInstaller
REM builds for whatever OS it's run on, it doesn't cross-compile.
cd /d "%~dp0"

if not exist .buildenv (
    python -m venv .buildenv
)

if not exist ..\out mkdir ..\out
.buildenv\Scripts\pip install --quiet --upgrade pip pyinstaller
.buildenv\Scripts\pyinstaller --noconfirm --distpath ..\out DesktopSpeed.spec

echo.
echo Built: ..\out\DesktopSpeed.exe
