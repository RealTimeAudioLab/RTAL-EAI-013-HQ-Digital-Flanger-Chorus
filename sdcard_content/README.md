# RTAL HQ FLANGER — Preset Catalog

This directory contains the **128-preset catalog** for RTAL HQ FLANGER V1.0. The accompanying `PRESET_CATALOG.csv` is a human-readable index of preset names and selected parameter values.

> **Important:** The CSV is a catalog/reference file, not a firmware image or a verified SD-card import format. Do not rename it to a preset file or assume that copying it to the SD card imports presets.

## Catalog at a glance

- **128 entries**, numbered **1–128**.
- **16 banks**, each containing **8 entries**.
- The `button` field identifies the position **1–8** within a bank; it does **not** establish that the current encoder-based firmware requires eight physical preset buttons.
- Names and numeric settings below are taken from the supplied CSV; their mapping to firmware-internal units has not been independently verified.

## Bank overview

| Bank | Programs | Preset family |
|---:|:---:|---|
| 01 | 001–008 | CLASSIC JET |
| 02 | 009–016 | ANALOG CHORUS |
| 03 | 017–024 | WIDE STEREO |
| 04 | 025–032 | DEEP BBD |
| 05 | 033–040 | METALLIC |
| 06 | 041–048 | SLOW MOTION |
| 07 | 049–056 | DUAL BBD |
| 08 | 057–064 | CROSS FEEDBACK |
| 09 | 065–072 | ENVELOPE SWEEP |
| 10 | 073–080 | PULSE RHYTHM |
| 11 | 081–088 | SPACE CHORUS |
| 12 | 089–096 | EXPERIMENTAL |
| 13 | 097–104 | SOFT SHIMMER |
| 14 | 105–112 | DARK DRONES |
| 15 | 113–120 | STEREO MOTION |
| 16 | 121–128 | HYBRID FX |

## CSV columns

| Column | Meaning |
|---|---|
| `program` | Catalog program number (1–128). |
| `bank` | Catalog bank (1–16). |
| `button` | Position in the catalog bank (1–8). |
| `name` | Preset name. |
| `time` | Stored time value. |
| `feedback` | Stored feedback value. |
| `lfo_rate` | Stored primary LFO rate value. |
| `lfo_amount` | Stored primary LFO modulation amount. |
| `mix` | Stored wet/dry mix value. |
| `lfo2_rate` | Stored second LFO rate value. |
| `lfo2_depth` | Stored second LFO depth value. |
| `lfo2_target` | Stored second LFO target selector value. |
| `cross` | Stored cross-feedback value. |
| `character` | Stored character selector value. |
| `dual_bbd` | Stored dual-BBD selector/value. |
| `env_follow` | Stored envelope-follow value. |
| `fb_damp` | Stored feedback damping value. |
| `st_motion` | Stored stereo-motion value. |

The catalog uses raw numeric values. For selectors such as `lfo2_target`, `character`, and `dual_bbd`, do not interpret a numeric code as a descriptive mode without checking the firmware. Likewise, time and LFO-rate values should not be labeled as milliseconds or hertz solely on the basis of this CSV.

## Browsing the catalog

Open `PRESET_CATALOG.csv` in a spreadsheet application or text editor to browse, sort, and filter the 128 entries. For example, filter by `bank` to see one family, or compare `feedback`, `mix`, and `lfo_amount` across presets.

### Program Change numbering

The catalog numbers programs **1–128**. MIDI Program Change messages commonly use **0–127** in the firmware; when the firmware follows that mapping, catalog program **1** corresponds to MIDI Program Change **0**, and catalog program **128** corresponds to MIDI Program Change **127**. Some MIDI controllers display Program Change as 1–128, so check the controller’s numbering convention.

## Example entries

| Program | Bank | Name | Time | Feedback | LFO rate | LFO amount | Mix |
|---:|---:|---|---:|---:|---:|---:|---:|
| 1 | 1 | CLASSIC JET 01 | 32 | 58 | 35 | 69 | 58 |
| 9 | 2 | ANALOG CHORUS 01 | 85 | 18 | 18 | 49 | 38 |
| 25 | 4 | DEEP BBD 01 | 87 | 64 | 25 | 65 | 68 |
| 49 | 7 | DUAL BBD 01 | 70 | 52 | 17 | 71 | 58 |
| 65 | 9 | ENVELOPE SWEEP 01 | 48 | 30 | 33 | 64 | 66 |
| 105 | 14 | DARK DRONES 01 | 74 | 88 | 15 | 46 | 59 |
| 128 | 16 | HYBRID FX 08 | 63 | 65 | 50 | 72 | 35 |

## Files

- [`PRESET_CATALOG.csv`](PRESET_CATALOG.csv) — full machine-readable catalog.
- `README.md` — this guide.

## Compatibility and preservation

Before changing firmware or SD-card contents, back up the instrument’s existing preset files. The CSV alone is not a backup of the full preset system: it lists selected parameters and does not contain all advanced settings, Morph configuration, or the SD-card cache/index files.

## Source and scope

This README was prepared from the supplied `PRESET_CATALOG.csv` (128 data rows, 18 columns). It describes the contents of that file, not a newly verified firmware feature or an import/export procedure.
