@echo off
cd /d "%~dp0"
where py >nul 2>&1
if not errorlevel 1 (set "PY=py") else (set "PY=python")
%PY% -c "import mido; import rtmidi" 2>nul
if errorlevel 1 (
 echo Installing MIDI dependencies...
 %PY% -m pip install -r requirements.txt
 if errorlevel 1 (echo Installation failed. & pause & exit /b 1)
)
%PY% flanger_graphical_editor.py
if errorlevel 1 pause
