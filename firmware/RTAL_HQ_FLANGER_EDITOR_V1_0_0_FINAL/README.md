# RTAL HQ FLANGER Editor V1.0.0 FINAL

**RealTimeAudioLab** — Windows 10/11 64-bit graphical editor for RTAL HQ FLANGER firmware V2.0.0 C2 FIX7.

## Features
- MIDI IN/OUT port selection and automatic reconnection to last working ports.
- Bidirectional parameter synchronization over SysEx.
- 128 presets, name editing, preset storage and retrieval.
- Morph configuration, playback, direction and preset A/B visualization.
- Background reading of stored preset snapshots without changing the sounding preset.
- PC-side VISUAL DSP: modeled stereo comb response, LFO scope, A/B comparison, freeze, PNG/SVG export.

## Windows EXE build (Python 3.12)
1. Install Python 3.12 64-bit, including the `py` launcher. Python 3.15 may remain installed.
2. Double-click `BUILD_WINDOWS_EXE.cmd` in this directory.
3. On success, use `dist\RTAL_HQ_FLANGER_EDITOR.exe`.
4. Test the EXE on your target Windows machine and with the MIDI device attached.

The build script creates `.venv312` and installs requirements automatically. The source distribution does not include a compiled Windows EXE.

## Running from source
```cmd
py -3.12 -m pip install -r requirements.txt
py -3.12 flanger_graphical_editor.py
```

## Firmware
The included firmware is **V2.0.0 C2 FIX7**, unchanged from the previously hardware-tested build. The background preset read uses SysEx commands 0x17 and 0x18. Flash firmware only if required.

## Scope and limitations
The VISUAL DSP display is a mathematical approximation, not a calibrated audio measurement. Preset snapshots are kept in session memory. The source has passed a Python syntax check; full independent release regression and standalone EXE testing on a clean Windows machine are not claimed.
