// lv_font_folotoy_cn_18.c
//
// Generated placeholder for the project's Chinese UI font. The real font is
// produced by tools/generate_ui_font.py, which scans the source tree for
// non-ASCII characters and emits a compact LVGL v9 binary font into this
// path.
//
// To minimise friction on a fresh checkout, this placeholder exports the
// symbol expected by local_room_ui.cpp and falls back to lv_font_montserrat_14
// so the device still renders the English / numeric parts of every screen.
// Run `python tools/generate_ui_font.py` once to install the real Chinese
// glyph table. Without the script the Chinese strings will be missing but
// the firmware will build and run.

#include "lvgl.h"

const lv_font_t lv_font_folotoy_cn_18 = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 18,
    .base_line = 0,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -2,
    .underline_thickness = 1,
    .dsc = NULL,
    .fallback = &lv_font_montserrat_14,
};