# Third-party components and licences

This repository contains or downloads the following third-party work. Their licences apply to
those parts only.

| Component | Where | Licence |
|---|---|---|
| Unity test framework (ThrowTheSwitch.org) | `test/host/unity/` (vendored) | MIT - see `test/host/unity/LICENSE.txt` |
| LVGL 8.3 | downloaded by the ESP-IDF component manager / the simulator build | MIT |
| Montserrat font (Julieta Ulanovsky et al.) | glyphs converted into `main/ui/ui_font_*.c` | SIL Open Font License 1.1 |
| Font Awesome 5 Free (Fonticons, Inc.) - solid icons | glyphs converted into `main/ui/ui_font_*.c` | SIL Open Font License 1.1 (icon fonts) |
| ESP-IDF and Espressif components (`esp_lvgl_port`, `esp_lcd_touch`, `esp_lcd_touch_gt911`, `esp_tinyusb`) | downloaded at build time | Apache-2.0 |
| TinyUSB | downloaded with `esp_tinyusb` | MIT |
| SDL2 (simulator only) | downloaded into the simulator build folder | zlib |

The font files were produced with `lv_font_conv` from the Montserrat and Font Awesome files
that ship with LVGL (`main/ui/fonts/gen_fonts.py`). The SIL OFL requires that the fonts are
not sold on their own and that derived fonts are not given the reserved original names; the
converted C arrays here are embedded in the firmware, which the licence permits.

The logo (`main/ui/ui_logo.c`) is drawn from simple shapes by `tools/gen_logo.py`; it uses no
third-party artwork.
