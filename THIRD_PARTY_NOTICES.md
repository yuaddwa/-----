# Third-Party Notices

This firmware depends on a number of third-party components and assets. Each
is used under its original licence, reproduced or linked below for
compliance.

## Components pulled by ESP-IDF Component Manager

Declared in `main/idf_component.yml`. Versions are pinned exactly as listed.

| Component | Version | Licence |
|---|---|---|
| `espressif/button` (espressif/button) | 4.2.0 | Apache 2.0 |
| `espressif/esp_lvgl_port` | 2.8.0~1 | Apache 2.0 |
| `espressif/esp_codec_dev` | 1.5.11 | Apache 2.0 |
| `lvgl/lvgl` | 9.5.0 | MIT |

The ESP-IDF framework itself (Kconfig, FreeRTOS, Wi-Fi driver, LCD driver)
is shipped under its own Apache 2.0 licence by Espressif Systems.

## Fonts

### LVGL Montserrat 14

Built into LVGL. Distributed under the MIT licence alongside
`lvgl/lvgl` 9.5.0. See `https://lvgl.io/` for full attribution.

### Noto Sans SC (Optional, regenerated via `tools/generate_ui_font.py`)

The Chinese glyph table used by the duel UI is regenerated on demand from
a host-installed TrueType font. By default the script reads
`C:\Windows\Fonts\NotoSansSC-VF.ttf`; macOS / Linux hosts must point
`FONT_PATH` inside the script at the equivalent TTF. Edit `FONT_PATH` and
run:

```
pip install Pillow
python tools/generate_ui_font.py --update-notices
```

The script records the source SHA-256 in this file. Re-run whenever you
swap the source TTF so downstream tooling can detect the change.

Noto Sans SC is distributed under the SIL Open Font Licence 1.1, copyright
the Noto Project Authors. See `https://fonts.google.com/noto/specimen/Noto+Sans+SC`.

## Brand notice

UNO is a trademark of Mattel, Inc. This project is an unofficial,
community-built fan work. It is not affiliated with, endorsed by, or
sponsored by Mattel.