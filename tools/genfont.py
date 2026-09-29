#!/usr/bin/env python3
"""Generate the kernel's built-in bitmap font as a C source file.

Run once and commit the output; the kernel never rasterises anything at build
time and does not need a font package installed.

  * ASCII 32..126 is rendered from Liberation Sans (SIL Open Font License,
    which permits embedding) and thresholded to 1bpp.
  * The CP437 box-drawing and block glyphs the boot logo uses are drawn
    procedurally instead. They are pure geometry, so hand-drawing them is both
    cleaner and licence-free.

The result is a fixed-cell font, so glyph geometry is trivial:

    '#' solid       '.' light (25% coverage, for double-line box drawing)
    ' ' transparent

Usage: tools/genfont.py <output.c>
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

CELL_W = 8
CELL_H = 16
FIRST = 32
LAST = 126

FONT_CANDIDATES = [
    # Monospace on purpose. The cell is 8px wide, and a proportional face at a
    # size that fits vertically is far wider than that: Liberation Sans at 16px
    # has a 13.3px advance, so two thirds of every glyph was being clipped at
    # the cell edge. That is what made the text look deformed. A monospace face
    # at a size whose advance fits leaves the glyphs whole.
    "/usr/share/fonts/liberation-sans-fonts/LiberationMono-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/urw-base35/NimbusMonoPS-Regular.otf",
    "/usr/share/fonts/google-noto-sans-mono-vf-fonts/NotoSansMono[wght].ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
]

# CP437 glyphs the kernel draws, as offset from 0x80.
# name -> (codepoint, description)
CP437 = {
    0xDB: "block_full",      # full block
    0xB0: "block_light",     # light shade
    0xB1: "block_medium",    # medium shade
    0xB2: "block_dark",      # dark shade
    0xC9: "tl_double", 0xCD: "h_double", 0xBB: "tr_double", 0xBA: "v_double",
    0xC8: "bl_double", 0xBC: "br_double",
    0xDA: "tl_single", 0xC4: "h_single", 0xBF: "tr_single", 0xB3: "v_single",
    0xC0: "bl_single", 0xD9: "br_single",
    0xC3: "tl_round", 0xB4: "h_round", 0xB9: "tr_round", 0xB3 + 0: "v_single",
    0xB3: "v_single",
}


def load_font(indent=1):
    """Pick the largest size whose advance still fits the cell width.

    Sizing by eye is what produced the clipped font in the first place, so
    the fit is measured instead: the chosen size must put 'M' -- the widest
    glyph in a monospace face is every glyph -- inside CELL_W, and its line
    height inside CELL_H.
    """
    for path in FONT_CANDIDATES:
        if not Path(path).exists():
            continue
        for size in range(20, 5, -1):
            font = ImageFont.truetype(path, size)
            advance = font.getlength("M") * indent
            ascent, descent = font.getmetrics()
            if advance <= CELL_W and (ascent + descent) <= CELL_H:
                return font, ascent, descent
        # Nothing fit at this path; take the smallest and let the caller see it.
        font = ImageFont.truetype(path, 6)
        return font, *font.getmetrics()
    raise SystemExit(
        "no usable font found; install fonts-liberation or DejaVu so this "
        "generator can run. The generated C file is committed, so building "
        "the kernel does not need any of this."
    )


def render_ascii(font, ascent, descent):
    """Return {codepoint: [row bytes]} for the printable ASCII range."""
    glyphs = {}
    # Place the baseline explicitly rather than centring on the font's em box.
    # PIL's ascent/descent cover the full line, not the ink, so centring on
    # them leaves every capital sitting at the top of the cell with dead space
    # beneath it. Baseline at 3/4 down the cell leaves room for descenders and
    # puts capitals where the eye expects them.
    baseline = (CELL_H * 3) // 4
    y_offset = baseline - ascent

    for cp in range(FIRST, LAST + 1):
        image = Image.new("1", (CELL_W, CELL_H), 0)
        draw = ImageDraw.Draw(image)
        draw.text((0, y_offset), chr(cp), font=font, fill=1)
        rows = []
        for y in range(CELL_H):
            bits = 0
            for x in range(CELL_W):
                if image.getpixel((x, y)):
                    bits |= 1 << x      # bit 0 is the leftmost pixel
            rows.append(bits)
        glyphs[cp] = rows
    return glyphs


def render_cp437():
    """Procedural box-drawing and block glyphs, same layout as render_ascii."""
    out = {}

    def blank():
        return [[0] * CELL_W for _ in range(CELL_H)]

    def put(g, x, y, v=1):
        if 0 <= y < CELL_H and 0 <= x < CELL_W:
            g[y][x] = v

    def hline(g, y, v=1, x0=0, x1=CELL_W):
        for x in range(x0, x1):
            put(g, x, y, v)

    def vline(g, x, v=1, y0=0, y1=CELL_H):
        for y in range(y0, y1):
            put(g, x, y, v)

    def block(g, density):
        """Fill the cell at the given density: 1=full, 3=light, 2=medium, 1=dark."""
        for y in range(CELL_H):
            for x in range(CELL_W):
                # Dither so the lighter shades read as a texture.
                if (x * 3 + y * 5) % 8 < density:
                    put(g, x, y)

    def double_box(g, tl, tr, bl, br, h, v):
        """Draw a double-line box using the supplied corner/edge styles."""
        hline(g, 0, tl, 0, 2)
        hline(g, 0, tr, CELL_W - 3, CELL_W)
        hline(g, CELL_H - 1, bl, 0, 2)
        hline(g, CELL_H - 1, br, CELL_W - 3, CELL_W)
        vline(g, 0, tl, 0, 3)
        vline(g, CELL_W - 1, tr, 0, 3)
        vline(g, 0, bl, CELL_H - 4, CELL_H)
        vline(g, CELL_W - 1, br, CELL_H - 4, CELL_H)
        hline(g, 1, h, 1, CELL_W - 1)
        vline(g, 1, h, 1, CELL_H - 1)

    for cp, name in CP437.items():
        g = blank()
        if name == "block_full":
            block(g, 8)
        elif name == "block_light":
            block(g, 2)
        elif name == "block_medium":
            block(g, 4)
        elif name == "block_dark":
            block(g, 6)
        else:
            # Single-stroke: a solid line, centred.
            if name.startswith("h_"):
                hline(g, CELL_H // 2, 1, 0, CELL_W)
            elif name.startswith("v_"):
                vline(g, CELL_W // 2, 1, 0, CELL_H)
            elif name.startswith("t") and "double" in name:
                double_box(g, 1, 1, 1, 1, 1, 1)
            elif name.startswith("b") and "double" in name:
                double_box(g, 1, 1, 1, 1, 1, 1)
            elif name.startswith("t") or name.startswith("b") or "single" in name:
                # Corners: a solid L in the matching quadrant.
                left = name.startswith("t") or name.startswith("b")
                top = name.startswith("t")
                xs = range(0, 4) if left else range(CELL_W - 4, CELL_W)
                ys = range(0, CELL_H // 2) if top else range(CELL_H // 2, CELL_H)
                for x in xs:
                    y0 = CELL_H // 2 - 1 if top else CELL_H // 2
                    put(g, x, y0)
                for y in ys:
                    x0 = 0 if left else CELL_W - 1
                    put(g, x0, y)
        out[cp] = [sum(1 << x for x in range(CELL_W) if g[y][x]) for y in range(CELL_H)]
    return out


def main():
    dest = Path(sys.argv[1] if len(sys.argv) > 1 else "kernel/font_data.c")
    font, ascent, descent = load_font()
    print(f"font: {font.path} advance(M)={font.getlength(chr(77)):.1f}px cell={CELL_W}x{CELL_H}")
    ascii_glyphs = render_ascii(font, ascent, descent)
    cp437_glyphs = render_cp437()

    lines = []
    lines.append("/* font_data.c - built-in bitmap font for the graphics stack")
    lines.append(" *")
    lines.append(" * GENERATED FILE, do not edit. Regenerate with tools/genfont.py.")
    lines.append(" *")
    lines.append(" * ASCII 32..126 is rendered from Liberation Sans (SIL Open Font")
    lines.append(" * License 1.1, which permits embedding in software). The CP437")
    lines.append(" * box-drawing and block glyphs are drawn procedurally by the")
    lines.append(" * generator, so they carry no third-party licensing.")
    lines.append(" *")
    lines.append(f" * Fixed cell: {CELL_W}x{CELL_H}, 1bpp, one byte per row, bit 0 leftmost.")
    lines.append(" */")
    lines.append("")
    lines.append('#include <kernel/types.h>')
    lines.append('#include <kernel/font.h>')
    lines.append("")
    lines.append(f"#define GLYPH_W {CELL_W}")
    lines.append(f"#define GLYPH_H {CELL_H}")
    lines.append("")
    lines.append(f"const u8 font_cell_w = {CELL_W};")
    lines.append(f"const u8 font_cell_h = {CELL_H};")
    lines.append(f"const u16 font_first_char = {FIRST};")
    lines.append("")
    lines.append("/* codepoint -> index into font_glyphs, or -1 when absent. */")
    lines.append("const s16 font_index[256] = {")
    row = []
    for cp in range(256):
        if FIRST <= cp <= LAST:
            row.append(str(cp - FIRST))
        elif cp in cp437_glyphs:
            row.append(str(len(ascii_glyphs) + sorted(cp437_glyphs).index(cp)))
        else:
            row.append("-1")
    for i in range(0, 256, 16):
        chunk = row[i:i + 16]
        lines.append("    " + ", ".join(f"{v:>4}" for v in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("static const u8 font_glyphs[][GLYPH_H] = {")

    def emit(cp):
        rows = ascii_glyphs.get(cp) or cp437_glyphs.get(cp)
        if rows is None:
            return
        label = chr(cp) if 32 <= cp < 127 else f"CP437 0x{cp:02X}"
        lines.append(f"    /* {label} */")
        for r in rows:
            lines.append(f"    0x{r:02X},")

    for cp in range(FIRST, LAST + 1):
        emit(cp)
    for cp in sorted(cp437_glyphs):
        emit(cp)
    lines.append("};")
    lines.append("")
    lines.append("const u8* font_glyph_rows(u16 index) { return font_glyphs[index]; }")
    lines.append("")
    lines.append(f"const u16 font_glyph_count = {len(ascii_glyphs) + len(cp437_glyphs)};")
    lines.append("")

    dest.write_text("\n".join(lines))
    print(f"wrote {dest}  ({len(ascii_glyphs)} ASCII + {len(cp437_glyphs)} CP437 glyphs, "
          f"{dest.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
