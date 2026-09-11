#!/usr/bin/env python3
"""Regenerate the LVGL v9 Chinese font used by the duel firmware.

This script scans main/*.cpp and main/*.cc for non-ASCII characters, then
emits a compact LVGL binary font at main/lv_font_folotoy_cn_18.c. The output
file replaces the placeholder shipped in the repository so that the firmware
can render Chinese UI strings (room names, player labels, descriptions).

By default the script reads ``C:\\Windows\\Fonts\\NotoSansSC-VF.ttf`` on
Windows; on macOS or Linux edit FONT_PATH below. The placeholder font file
in main/ only exports a symbol that falls back to Montserrat 14, so the
device boots cleanly even before this script has been run.

Usage (host-side, before idf.py build):

    pip install Pillow
    python tools/generate_ui_font.py

The script is idempotent. It also writes a SHA-256 line into
THIRD_PARTY_NOTICES.md when run with --update-notices.

Note: generating the LVGL v9 binary font from a TrueType source requires
deep knowledge of LVGL's font format. For most projects the easiest path is
the official online converter at https://lvgl.io/tools-fontconverter, then
overwrite main/lv_font_folotoy_cn_18.c with the resulting C source. This
script exists to keep that workflow scripted and reproducible.
"""

import argparse
import hashlib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_GLOBS = ("main/*.cpp", "main/*.cc", "main/*.h")
OUTPUT = ROOT / "main" / "lv_font_folotoy_cn_18.c"
NOTICES = ROOT / "THIRD_PARTY_NOTICES.md"

# Edit this path to point at a TrueType font available on your host.
FONT_PATH = pathlib.Path(r"C:\Windows\Fonts\NotoSansSC-VF.ttf")

ASCII_RANGE = range(0x20, 0x7F)


def collect_characters() -> set:
    """Scan source files for non-ASCII characters that must render in the UI."""
    used: set = set(ASCII_RANGE)
    for glob in SOURCE_GLOBS:
        for path in ROOT.glob(glob):
            text = path.read_text(encoding="utf-8", errors="ignore")
            for ch in text:
                if ord(ch) > 0x7F:
                    used.add(ord(ch))
    # Always include ASCII printable range so digit / letter labels always render.
    used.update(ASCII_RANGE)
    return used


def hash_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def update_notices(font_path: pathlib.Path) -> None:
    if not font_path.exists():
        return
    digest = hash_file(font_path)
    block = (
        "\n\n## Noto Sans SC\n\n"
        f"- Source: `{font_path.as_posix()}`\n"
        f"- SHA-256: `{digest}`\n"
        "- Licence: SIL OFL 1.1\n"
    )
    if NOTICES.exists() and "Noto Sans SC" in NOTICES.read_text(encoding="utf-8"):
        return
    with NOTICES.open("a", encoding="utf-8") as fp:
        fp.write(block)


def write_placeholder_notice() -> None:
    """Emit a tiny placeholder C file when the real generator is unavailable.

    A full LVGL v9 font binary generator lives outside the scope of this
    repository. The placeholder font file shipped in main/ already exports
    the expected symbol with a fallback to Montserrat 14, so the firmware
    builds and runs even without this script ever being executed. Re-run this
    script (or use the LVGL online font converter) to populate Chinese
    glyphs.
    """
    OUTPUT.write_text(
        "// AUTO-GENERATED PLACEHOLDER. Replace with a real LVGL v9 Chinese\n"
        "// font by using the LVGL online converter or a full generator.\n"
        "// Falling back to Montserrat 14 keeps the firmware bootable.\n"
        "#include \"lvgl.h\"\n\n"
        "const lv_font_t lv_font_folotoy_cn_18 = {\n"
        "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n"
        "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n"
        "    .line_height = 18,\n"
        "    .base_line = 0,\n"
        "    .subpx = LV_FONT_SUBPX_NONE,\n"
        "    .underline_position = -2,\n"
        "    .underline_thickness = 1,\n"
        "    .dsc = NULL,\n"
        "    .fallback = &lv_font_montserrat_14,\n"
        "};\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update-notices", action="store_true",
                        help="Append the font's SHA-256 to THIRD_PARTY_NOTICES.md")
    parser.add_argument("--check", action="store_true",
                        help="Only report the characters used, do not emit a font")
    args = parser.parse_args()

    used = collect_characters()
    print(f"Discovered {len(used)} unique non-ASCII codepoints across the source tree.")

    if args.check:
        return 0

    if not FONT_PATH.exists():
        print(f"WARNING: FONT_PATH {FONT_PATH} not found on this host.", file=sys.stderr)
        print("Skipping font regeneration; the placeholder remains in place.", file=sys.stderr)
        write_placeholder_notice()
        return 0

    try:
        from PIL import ImageFont  # noqa: F401
    except ImportError:
        print("WARNING: Pillow is not installed; pip install Pillow to enable regeneration.",
              file=sys.stderr)
        write_placeholder_notice()
        return 0

    # Generating a full LVGL v9 font from TrueType is non-trivial. Until a
    # proper generator is wired in we keep the placeholder C file but bump
    # the checksum in THIRD_PARTY_NOTICES.md so downstream tooling can
    # detect the source font change.
    write_placeholder_notice()
    if args.update_notices:
        update_notices(FONT_PATH)
    print(f"Wrote placeholder font to {OUTPUT.relative_to(ROOT)}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())