# RTAL HQ FLANGER — v1.0.0

**First major release · Digital stereo flanger & chorus · RTAL Phoenix ESP32-S3**

RTAL HQ FLANGER is a standalone digital stereo flanger and chorus effect for the RTAL Phoenix ESP32-S3 platform. Its BBD-inspired sound-shaping controls, graphical modulation display, 128-slot preset system, preset morphing and MIDI integration combine classic modulation textures with a modern hands-on workflow.

> **Verification status:** **V1.0 FINAL CANDIDATE 1** was confirmed working on Phoenix hardware. The separately labeled **V1.0 FINAL** firmware was prepared from that candidate, but a separate compile/upload test of that exact FINAL build has not been documented. Verify the release binary or sketch on your hardware before treating it as independently validated.

## Watch & listen

[▶ Watch the RTAL HQ FLANGER sound demonstration](https://github.com/user-attachments/assets/7ec1192f-9a88-45e2-88b6-416ba4f6f3eb)

The demonstration uses a separately recorded soundtrack; the original camera audio was removed. See the repository's main README for its preview image and interface gallery.

## What's included in v1.0.0

- **Stereo flanger and chorus**, with BBD-inspired timbre and feedback parameters.
- **Graphical MOTION display** for real-time modulation visualization.
- **Multiple LFO waveforms**, adjustable rate and depth, and supported synchronization options.
- **128 presets**, numbered 001–128, with on-device browsing and editable names.
- **Preset morphing** between selected sounds, with persistent morph configuration.
- **MIDI Program Change 0–127**, MIDI Control Change and MIDI Clock support.
- **OLED and rotary encoder** for standalone operation.
- **SD-card preset storage**, RAM preset caching and optimized save behavior.

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
2. Download the **v1.0.0 firmware archive** from this GitHub release.
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

The checklist describes the recommended final acceptance procedure; it is **not** a claim that every item was independently re-tested on the renamed FINAL build.

## Documentation and resources

- **Main repository README:** feature overview, photos and sound demonstration.
- **User manual:** operation, modulation, presets and morphing.
- **MIDI CC Reference:** complete CC tables, banks and controller ranges.
- **Preset catalog:** `PRESET_CATALOG.csv` and its accompanying README, when included in the repository.
- **Release archive:** firmware and the accompanying release documentation.

Check the repository file tree and this release's **Assets** section for the actual download names; documentation and media may be distributed separately from the firmware archive.

## Development and validation history

| Milestone | Result |
| --- | --- |
| RC3 FIX13F | Morph-index handling confirmed on Phoenix hardware |
| RC3 FIX13G FIX1 | Direct RAM preset-cache update confirmed on Phoenix hardware |
| V1.0 FINAL CANDIDATE 1 | Hardware operation confirmed |
| v1.0.0 FINAL | Release preparation completed; exact FINAL build should be verified separately |

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

**RTAL HQ FLANGER · v1.0.0 · Phoenix ESP32-S3**
