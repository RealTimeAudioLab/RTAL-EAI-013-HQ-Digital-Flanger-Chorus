@echo off
setlocal
cd /d "%~dp0"
echo RTAL HQ FLANGER Editor V1.0.0 FINAL - Windows EXE Build (Python 3.12)
where py >nul 2>&1
if errorlevel 1 (
 echo Python Launcher fehlt. Bitte Python 3.12 installieren.
 pause
 exit /b 1
)
py -3.12 --version
if errorlevel 1 goto :error
py -3.12 -m venv .venv312
if errorlevel 1 goto :error
call .venv312\Scripts\activate.bat
if errorlevel 1 goto :error
python -m pip install --upgrade pip
if errorlevel 1 goto :error
python -m pip install -r requirements.txt "pyinstaller>=6,<7"
if errorlevel 1 goto :error
python -c "import mido, rtmidi; print('RTAL MIDI OK')"
if errorlevel 1 goto :error
python -m PyInstaller --noconfirm --clean --onefile --windowed --name RTAL_HQ_FLANGER_EDITOR --collect-all rtmidi --collect-all mido --hidden-import=mido.backends.rtmidi flanger_graphical_editor.py
if errorlevel 1 goto :error
echo.
echo EXE erstellt: dist\RTAL_HQ_FLANGER_EDITOR.exe
echo Bitte auf Windows 10/11 mit MIDI-Interface testen.
pause
exit /b 0
:error
echo Build fehlgeschlagen. Bitte die Fehlermeldung mitteilen.
pause
exit /b 1
