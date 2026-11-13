#!/usr/bin/env python3
"""Generate CharisOS's embedded Inter bitmap font and CP437 UI glyphs."""
from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont

CELL_W, CELL_H = 12, 22
FIRST, LAST = 32, 126
FONT_CANDIDATES = (
    "/usr/share/fonts/opentype/inter/Inter-Regular.otf",
    "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
)

CP437_NAMES = {
    0xB0: "light", 0xB1: "medium", 0xB2: "dark", 0xB3: "v",
    0xB4: "righttee", 0xB9: "bottomtee", 0xBA: "vdouble",
    0xBB: "trdouble", 0xBC: "brdouble", 0xBF: "tr", 0xC0: "bl",
    0xC3: "lefttee", 0xC4: "h", 0xC8: "bldouble", 0xC9: "tldouble",
    0xCD: "hdouble", 0xD9: "br", 0xDA: "tl", 0xDB: "full",
}


def load_font():
    for path in FONT_CANDIDATES:
        if not Path(path).exists():
            continue
        for size in range(18, 8, -1):
            font = ImageFont.truetype(path, size)
            max_advance = max(font.getlength(chr(cp))
                             for cp in range(FIRST, LAST + 1))
            ascent, descent = font.getmetrics()
            if max_advance <= CELL_W and ascent + descent <= CELL_H:
                return font, ascent, descent
    raise SystemExit(
        "Inter is unavailable or does not fit. Install fonts-inter and Pillow."
    )


def blank():
    return [[0 for _ in range(CELL_W)] for _ in range(CELL_H)]


def put(glyph, x, y):
    if 0 <= x < CELL_W and 0 <= y < CELL_H:
        glyph[y][x] = 1


def hline(glyph, y, x0=0, x1=CELL_W):
    for x in range(x0, x1):
        put(glyph, x, y)


def vline(glyph, x, y0=0, y1=CELL_H):
    for y in range(y0, y1):
        put(glyph, x, y)


def make_cp437():
    out = {}
    for cp, name in CP437_NAMES.items():
        glyph = blank()
        middle_x, middle_y = CELL_W // 2, CELL_H // 2
        if name == "full":
            for y in range(CELL_H):
                for x in range(CELL_W):
                    put(glyph, x, y)
        elif name in ("light", "medium", "dark"):
            density = {"light": 2, "medium": 4, "dark": 6}[name]
            for y in range(CELL_H):
                for x in range(CELL_W):
                    if (x * 3 + y * 5) % 8 < density:
                        put(glyph, x, y)
        elif name in ("h", "hdouble"):
            hline(glyph, middle_y)
            if name == "hdouble":
                hline(glyph, middle_y + 2)
        elif name in ("v", "vdouble"):
            vline(glyph, middle_x)
            if name == "vdouble":
                vline(glyph, middle_x + 2)
        elif name in (
            "tl", "tr", "bl", "br",
            "tldouble", "trdouble", "bldouble", "brdouble",
        ):
            left = name.startswith("tl") or name.startswith("bl")
            top = name.startswith("tl") or name.startswith("tr")
            if left:
                hline(glyph, middle_y, 0, middle_x + 1)
            else:
                hline(glyph, middle_y, middle_x, CELL_W)
            if top:
                vline(glyph, middle_x, 0, middle_y + 1)
            else:
                vline(glyph, middle_x, middle_y, CELL_H)
            if name.endswith("double"):
                if left:
                    hline(glyph, middle_y + 2, 0, middle_x + 1)
                else:
                    hline(glyph, middle_y + 2, middle_x, CELL_W)
                if top:
                    vline(glyph, middle_x + 2, 0, middle_y + 1)
                else:
                    vline(glyph, middle_x + 2, middle_y, CELL_H)
        elif name in ("lefttee", "righttee", "bottomtee"):
            vline(glyph, middle_x)
            if name == "lefttee":
                hline(glyph, middle_y, 0, middle_x + 1)
            else:
                hline(glyph, middle_y, middle_x, CELL_W)
        out[cp] = glyph
    return out


def generate(destination):
    font, ascent, descent = load_font()
    ascii_glyphs = {}
    baseline = (CELL_H - (ascent + descent)) // 2 + ascent
    for cp in range(FIRST, LAST + 1):
        image = Image.new("1", (CELL_W, CELL_H), 0)
        ImageDraw.Draw(image).text(
            (0, baseline - ascent), chr(cp), font=font, fill=1
        )
        ascii_glyphs[cp] = [
            [1 if image.getpixel((x, y)) else 0 for x in range(CELL_W)]
            for y in range(CELL_H)
        ]

    cp437 = make_cp437()
    ordered = list(range(FIRST, LAST + 1)) + sorted(cp437)
    indexes = {codepoint: index for index, codepoint in enumerate(ordered)}
    glyphs = {**ascii_glyphs, **cp437}

    lines = [
        "/* font_data.c - built-in Inter-derived native UI bitmap font.",
        " *",
        " * Glyph pixels are rasterised from Inter Regular (Inter Project Authors,",
        " * 2016-2023; SIL Open Font License 1.1). The glyph bitmap is embedded in",
        " * this source, so no runtime font file or disk mount is required.",
        " * CP437 panel and block glyphs are procedural and carry no font dependency.",
        " *",
        f" * Fixed cell: {CELL_W}x{CELL_H}, 1bpp stored as u16 rows; bit 0 is leftmost.",
        " */",
        "",
        "#include <kernel/types.h>",
        "#include <kernel/font.h>",
        "",
        f"#define GLYPH_W {CELL_W}",
        f"#define GLYPH_H {CELL_H}",
        "",
        f"const u8 font_cell_w = {CELL_W};",
        f"const u8 font_cell_h = {CELL_H};",
        f"const u16 font_first_char = {FIRST};",
        "",
        "/* Codepoint to glyph index, or -1 when no glyph is bundled. */",
        "const s16 font_index[256] = {",
    ]
    for start in range(0, 256, 16):
        values = [str(indexes.get(cp, -1)) for cp in range(start, start + 16)]
        lines.append("    " + ", ".join(f"{value:>4}" for value in values) + ",")

    lines += ["};", "", "static const u16 font_glyphs[][GLYPH_H] = {"]
    for cp in ordered:
        label = chr(cp) if FIRST <= cp <= LAST else f"CP437 0x{cp:02X}"
        lines.append(f"    /* {label} */")
        for row in glyphs[cp]:
            bits = sum(1 << x for x, pixel in enumerate(row) if pixel)
            lines.append(f"    0x{bits:03X},")

    count = len(ordered)
    lines += [
        "};",
        "",
        "const u16* font_glyph_rows(u16 index) {",
        "    if (index >= font_glyph_count) return font_glyphs[0];",
        "    return font_glyphs[index];",
        "}",
        "",
        f"const u16 font_glyph_count = {count};",
        "",
    ]
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(lines))
    print(
        f"Wrote {destination}: {len(ascii_glyphs)} ASCII + {len(cp437)} CP437 "
        f"glyphs; Inter advance W={font.getlength('W'):.2f}px; "
        f"cell={CELL_W}x{CELL_H}"
    )


if __name__ == "__main__":
    generate(Path(sys.argv[1] if len(sys.argv) > 1 else "kernel/font_data.c"))
