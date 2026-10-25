#!/usr/bin/env python3
"""Anti-aliased bitmap fonts for SD Bench (src/font_data.h).

Renders DejaVu Sans (Bitstream Vera licence, free to embed) with Pillow at
the sizes the interface uses, as 4-bit alpha glyphs: ASCII 32-126 plus the
accented letters of French. Run again only to change fonts or sizes:

    python3 tools/make_font.py > src/font_data.h
"""

import sys
from PIL import Image, ImageDraw, ImageFont

FONTS = [
    # name, file, pixel size
    ("font_small", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11),
    ("font_medium", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 14),
    ("font_large", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 30),
]
CHARS = [chr(c) for c in range(32, 127)] + list("éèêëàâçùûôîïÉÈÀ°µ…·×")


def glyphs(path, size):
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    out = []
    for ch in CHARS:
        advance = round(font.getlength(ch))
        box = font.getbbox(ch)  # relative to the origin at the ascent line top
        x0, y0, x1, y1 = box
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        pixels = []
        if w and h:
            im = Image.new("L", (w, h), 0)
            ImageDraw.Draw(im).text((-x0, -y0), ch, font=font, fill=255)
            pixels = [(v + 8) // 17 for v in im.tobytes()]  # 0-15
        out.append((ord(ch), advance, w, h, x0, y0, pixels))
    return ascent, descent, out


def main():
    w = sys.stdout.write
    w("/* Made by tools/make_font.py from DejaVu Sans (Bitstream Vera licence):\n")
    w("   4-bit alpha glyphs, two pixels a byte (first in the high nibble). */\n")
    w("#ifndef FONT_DATA_H\n#define FONT_DATA_H\n\n#include \"gfx.h\"\n\n")
    for name, path, size in FONTS:
        ascent, descent, gl = glyphs(path, size)
        data = bytearray()
        table = []
        for code, adv, gw, gh, x0, y0, px in gl:
            table.append((code, adv, gw, gh, x0, y0, len(data)))
            if px:
                if len(px) % 2:
                    px = px + [0]
                for i in range(0, len(px), 2):
                    data.append(px[i] << 4 | px[i + 1])
        w(f"static const uint8_t {name}_bits[{len(data)}] = {{\n")
        for i in range(0, len(data), 24):
            w("\t" + ",".join(str(b) for b in data[i:i + 24]) + ",\n")
        w("};\n\n")
        w(f"static const gfx_glyph_t {name}_glyphs[{len(table)}] = {{\n")
        for code, adv, gw, gh, x0, y0, off in table:
            w(f"\t{{ {code}, {adv}, {gw}, {gh}, {x0}, {y0}, {off} }},\n")
        w("};\n\n")
        w(f"static const gfx_font_t {name} = {{ {ascent + descent}, {ascent}, {len(table)}, "
          f"{name}_glyphs, {name}_bits }};\n\n")
    w("#endif\n")


if __name__ == "__main__":
    main()
