# RTAL HQ FLANGER — v2.0.0

**Firmware v2.0.0 FINAL · Windows Editor v1.0.0 FINAL · RTAL Phoenix ESP32-S3**

RTAL HQ FLANGER is a standalone digital stereo flanger and chorus effect for the RTAL Phoenix ESP32-S3 platform. Its BBD-inspired sound-shaping controls, graphical modulation display, 128-slot preset system, preset morphing and MIDI integration combine classic modulation textures with a modern hands-on workflow.

> **Verification status:** Firmware v2.0.0 FINAL is based on the hardware-tested C2 FIX7 branch. Background preset reading was confirmed on Phoenix hardware with preset 050 remaining active while presets 001 and 105 were read for the editor. The separately repackaged v2.0.0 FINAL sketch has not been independently confirmed as compiled/uploaded. The Windows Editor v1.0.0 FINAL build workflow using Python 3.12 was confirmed working; perform a complete regression test before distribution.

## Watch & listen

[▶ Watch the RTAL HQ FLANGER sound demonstration](https://github.com/user-attachments/assets/7ec1192f-9a88-45e2-88b6-416ba4f6f3eb)

The demonstration uses a separately recorded soundtrack; the original camera audio was removed. See the repository's main README for its preview image and interface gallery.

## What's included in v2.0.0

- **Stereo flanger and chorus**, with BBD-inspired timbre and feedback parameters.
- **Graphical MOTION display** for real-time modulation visualization.
- **Multiple LFO waveforms**, adjustable rate and depth, and supported synchronization options.
- **128 presets**, numbered 001–128, with on-device browsing and editable names.
- **Preset morphing** between selected sounds, with persistent morph configuration.
- **MIDI Program Change 0–127**, MIDI Control Change and MIDI Clock support.
- **OLED and rotary encoder** for standalone operation.
- **SD-card preset storage**, RAM preset caching and optimized save behavior.
- **Windows Editor v1.0.0 FINAL** with bidirectional SysEx, 39 synchronized parameters and MIDI reconnect.
- **VISUAL DSP** with illustrative stereo comb response, LFO visualization, A/B comparison and PNG/SVG export.
- **Background preset read** for Morph A/B snapshots without switching the currently sounding preset.

## Release highlights

### Presets and storage

The preset browser supports 128 slots. Preset data and related morph configurations are stored on the SD card. The release incorporates improvements from the RC3 stabilization work: selective writing of unchanged morph configurations, morph-index protection, and direct refresh of the in-memory preset cache after saving. These changes were intended to reduce redundant SD operations without changing existing preset formats.

### Morphing

Morphing provides transitions between two stored preset states. Morph settings are persistent and the browser can indicate morph-enabled presets. Keep the SD card's existing preset and morph files together when backing up or migrating a setup.

### MIDI

| Function | Release behavior |
| --- | --- |
| Program Change | PC **0–127** selects presets **001–128** |
| Control Change | Controls supported sound and morph parameters |
| Parameter bank selection | **CC 32** selects the relevant CC parameter bank |
| MIDI Clock | External timing/synchronization for supported functions |

**CC mappings are bank-dependent.** Refer to the matching **MIDI CC Reference** document for the complete mapping and value ranges. Do not assume the same CC number controls the same parameter in every bank.

## Windows Editor v1.0.0 FINAL — new in the v2.0.0 ecosystem

The optional **HQ DIGITAL FLANGER** desktop editor provides direct access to the Phoenix device while retaining full standalone operation. It is designed for **Windows 10/11 (64-bit)** and communicates over MIDI/SysEx. The editor does not perform the embedded audio processing.

### Editor in action

![RTAL HQ FLANGER Windows Editor demonstration](images/editor/RTAL_FLANGER_EDITOR.gif)

> Upload `media/editor/RTAL_FLANGER_EDITOR.gif` to the repository at the path shown above. If the GIF is a single-frame file, GitHub displays it as a static image.

### Editor interface gallery

| MAIN | BBD / CHARACTER |
| --- | --- |
| ![MAIN parameter page](images/editor/RTAL_Flanger_Editor_Main.JPG) | ![BBD and character page](images/editor/RTAL_Flanger_Editor_BBD_Character.JPG) |
| **LFO / CLOCK** | **STEREO / ROUTING** |
| ![LFO and clock page](images/editor/RTAL_Flanger_Editor_LFO_Clock.JPG) | ![Stereo and routing page](images/editor/RTAL_Flanger_Editor_Stereo_Routing.JPG) |
| **ENVELOPE** | **PRESETS** |
| ![Envelope page](images/editor/RTAL_Flanger_Editor_Envelope.JPG) | ![Preset browser page](images/editor/RTAL_Flanger_Editor_Presets.JPG) |
| **MORPH CONTROL** | **VISUAL DSP** |
| ![Morph control page](images/editor/RTAL_Flanger_Editor_Morph_Control.JPG) | ![Visual DSP page](images/editor/RTAL_Flanger_Editor_Visual_DSP.JPG) |

### Connection and parameter synchronization

1. Connect the Phoenix MIDI interface to the Windows computer using a compatible MIDI interface.
2. Start `RTAL_HQ_FLANGER_EDITOR.exe` or run the editor from source.
3. Use **CHANGE MIDI PORTS** to select the appropriate input/output ports; use **RECONNECT** after interruptions.
4. Select **READ ALL** to synchronize the editor's **39 parameters** with the device.
5. Change controls in the editor to adjust the hardware sound in real time. Keep the hardware's MIDI channel and relevant parameter banks configured correctly.

### Preset browser and background reads

- Browse **001–128**, load a preset on the hardware, read its name, rename it or save the active preset to SD.
- **Program Change** is zero-based: PC 0 selects slot 001, PC 127 selects slot 128.
- Back up `/RTAL_FLANGER` before write operations; saving requires the selected slot to be active.
- **FETCH MORPH A/B** retrieves stored A/B parameter snapshots into the editor session without loading those presets on the hardware. This permits a visual preview while another preset remains audible.

### Morph Control

Set **Preset A**, **Preset B**, position, time, manual/auto mode, linear/smooth curve, ping-pong, MIDI sync, division and start direction. **READ MORPH** loads the current configuration, **APPLY TO FLANGER** sends edits, and **SAVE MORPH TO SD** stores the active preset's morph configuration. Transport controls include **PLAY**, **STOP** and **REVERSE**.

### VISUAL DSP

The PC-side visualizer offers stereo comb-response and LFO views, A/B capture, morph preview, compare/freeze, display options and **PNG/SVG export**. The graphs are **illustrative DSP-inspired models**, not measurements of the device's actual audio output or guaranteed frequency-response plots.

### Building the Windows executable

The confirmed build workflow uses **Python 3.12 (64-bit)**, `mido`, `python-rtmidi` and PyInstaller. Run the included `BUILD_WINDOWS_EXE.cmd` in the editor source directory; the resulting portable executable is placed in `dist/`. Do not use the default Python 3.15 environment for this build, as `python-rtmidi` attempted a failing source compilation in that environment. A prebuilt EXE is available only if explicitly attached to the GitHub release assets.

**Version-label note:** The editor footer may display `Firmware 1.0.0` by design; this is a UI label and does not supersede the separately versioned **firmware v2.0.0 FINAL** release.

## Hardware and build requirements

| Item | Details |
| --- | --- |
| Target hardware | RTAL Phoenix with ESP32-S3 |
| Interface | OLED display and rotary encoder |
| Audio | Digital stereo effect, **32 kHz** processing |
| Storage | SD card; 128 preset slots |
| External control | MIDI Program Change, CC and Clock |
| Development environment | Arduino IDE 2.x with firmware-matched ESP32-S3 support and dependencies |

The **firmware sketch and hardware-specific documentation** are authoritative for board options, pin assignments and required libraries. Do not substitute generic ESP32-S3 pinouts.

## Install or upgrade

1. **Back up the complete SD card**, including presets, names and morph-related files.
2. Download the **v2.0.0 firmware archive** from this GitHub release.
3. Extract the archive while preserving the Arduino sketch folder structure.
4. Open the `.ino` sketch in Arduino IDE 2.x and select the correct Phoenix ESP32-S3 board configuration.
5. Confirm the required libraries and board settings against the included firmware instructions.
6. Compile, upload and restart the device with the SD card inserted.
7. Check the display, audio input/output, encoder, preset browser and MIDI operation.
8. Reload existing presets and verify that saved parameters and morph settings persist after a full power cycle.

**Compatibility note:** Existing preset formats were deliberately preserved during the RC3 save optimizations. Nevertheless, make a backup before updating, and do not remove the SD card during writes.

## Recommended release verification

- [ ] Firmware compiles with the intended Arduino/ESP32-S3 toolchain.
- [ ] System boots and reports SD readiness.
- [ ] Flanger and chorus produce clean stereo audio.
- [ ] OLED, encoder and parameter-group navigation work.
- [ ] Preset 001 with morph saves and reloads after power-off.
- [ ] Preset 010 without morph saves and reloads after power-off.
- [ ] Preset rename and morph indicators remain correct.
- [ ] MIDI PC 0 and PC 127 select the first and last preset slots.
- [ ] MIDI CC bank selection and external Clock are checked.
- [ ] No unexpected audio underruns appear in normal use.
- [ ] Editor connects, reads 39 parameters and reconnects after MIDI interruption.
- [ ] Background fetch of Morph A/B does not change the active sounding preset.
- [ ] Morph transport, preset save/rename and Visual DSP export work in the Windows EXE.

The checklist describes recommended final acceptance checks; it is **not** a claim that every item has been independently re-tested on the renamed FINAL build.

## Documentation and resources

- **Main repository README:** feature overview, photos and sound demonstration.
- **User manual:** operation, modulation, presets and morphing.
- **MIDI CC Reference:** complete CC tables, banks and controller ranges.
- **Preset catalog:** `PRESET_CATALOG.csv` and its accompanying README, when included in the repository.
- **Release archive:** firmware and the accompanying release documentation.
- **Windows Editor v1.0.0 FINAL:** editor source, Python 3.12 build script and user manual.
- **Editor media:** screenshots and `media/editor/RTAL_FLANGER_EDITOR.gif`.

Check the repository file tree and this release's **Assets** section for the actual download names; documentation and media may be distributed separately from the firmware archive.

## Development and validation history

| Milestone | Result |
| --- | --- |
| RC3 FIX13F | Morph-index handling confirmed on Phoenix hardware |
| RC3 FIX13G FIX1 | Direct RAM preset-cache update confirmed on Phoenix hardware |
| V1.0 FINAL CANDIDATE 1 | Hardware operation confirmed |
| v1.0.0 FINAL | Original firmware release package prepared |
| C2 FIX7 | Background preset read confirmed on Phoenix hardware |
| v2.0.0 FINAL | Firmware release prepared from C2 FIX7; exact FINAL sketch requires independent compile/upload check |
| Windows Editor v1.0.0 FINAL | Windows EXE build workflow confirmed with Python 3.12 |

## Troubleshooting

| Issue | First checks |
| --- | --- |
| SD not ready | Card seating, formatting, Phoenix SD wiring and configuration |
| No sound | Audio connections, bypass/mix settings and startup status |
| Preset not retained | Save completion, correct slot and SD write access |
| Unexpected MIDI parameter | Selected **CC 32** bank and MIDI channel |
| No clock sync | External MIDI Clock source and relevant sync setting |
| Audio artifacts | DSP timing/underrun diagnostics and hardware configuration |

## Notes

This project uses **digital** BBD-inspired signal processing; it does not claim to contain an analog bucket-brigade device. Screenshots and demo preset names are illustrative and may differ from the contents of your SD card. Do not infer a software license from this README; consult the repository's actual `LICENSE` file, if provided.

---

**RTAL HQ FLANGER · Firmware v2.0.0 FINAL · Windows Editor v1.0.0 FINAL · Phoenix ESP32-S3**
