#!/usr/bin/env python3
"""Generate antialiased Inter UI glyphs plus procedural CP437 frame glyphs."""
from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont

CELL_W, CELL_H = 16, 22
FIRST, LAST = 32, 126
COVERAGE_LEVELS = 15
FONT_CANDIDATES = (
    "/usr/share/fonts/rsms-inter-fonts/Inter-Regular.otf",
    "/usr/share/fonts/rsms-inter-fonts/Inter-Regular.ttf",
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
        for size in range(22, 11, -1):
            font = ImageFont.truetype(path, size)
            max_advance = max(font.getlength(chr(cp))
                             for cp in range(FIRST, LAST + 1))
            ascent, descent = font.getmetrics()
            if max_advance <= CELL_W - 1 and ascent + descent <= CELL_H:
                return font, ascent, descent
    raise SystemExit(
        "Inter is unavailable or does not fit. Install fonts-inter/rsms-inter-fonts and Pillow."
    )


def blank():
    return [[0 for _ in range(CELL_W)] for _ in range(CELL_H)]


def put(glyph, x, y, coverage=COVERAGE_LEVELS):
    if 0 <= x < CELL_W and 0 <= y < CELL_H:
        glyph[y][x] = coverage


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
                        put(glyph, x, y, max(1, density * COVERAGE_LEVELS // 8))
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


def raster_ascii(font, ascent, descent):
    glyphs = {}
    baseline = (CELL_H - (ascent + descent)) // 2 + ascent
    for cp in range(FIRST, LAST + 1):
        image = Image.new("L", (CELL_W, CELL_H), 0)
        draw = ImageDraw.Draw(image)
        draw.fontmode = "L"
        draw.text((0, baseline - ascent), chr(cp), font=font, fill=255, stroke_width=0)
        glyphs[cp] = [
            [
                min(COVERAGE_LEVELS,
                    (image.getpixel((x, y)) * COVERAGE_LEVELS + 127) // 255)
                for x in range(CELL_W)
            ]
            for y in range(CELL_H)
        ]
    return glyphs


def generate(destination):
    font, ascent, descent = load_font()
    ascii_glyphs = raster_ascii(font, ascent, descent)
    cp437 = make_cp437()
    ordered = list(range(FIRST, LAST + 1)) + sorted(cp437)
    indexes = {codepoint: index for index, codepoint in enumerate(ordered)}
    glyphs = {**ascii_glyphs, **cp437}

    lines = [
        "/* font_data.c - antialiased Inter-derived native UI glyphs.",
        " *",
        " * ASCII glyphs are rasterised from Inter Regular (Inter Project Authors,",
        " * 2016-2023; SIL Open Font License 1.1) into 4-bit grayscale coverage.",
        " * CP437 frame and block glyphs are procedural, with no external font data.",
        " * Runtime text rendering alpha-blends the embedded glyphs into any supported",
        " * framebuffer pixel format. No filesystem or runtime font fetch is needed.",
        " *",
        f" * Fixed cell: {CELL_W}x{CELL_H}; coverage values 0..{COVERAGE_LEVELS}.",
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

    lines += ["};", "", "static const u8 font_glyphs[][GLYPH_H * GLYPH_W] = {"]
    for cp in ordered:
        label = chr(cp) if FIRST <= cp <= LAST else f"CP437 0x{cp:02X}"
        lines.append(f"    {{ /* {label} */")
        flattened = [value for row in glyphs[cp] for value in row]
        for start in range(0, len(flattened), CELL_W):
            values = flattened[start:start + CELL_W]
            lines.append("        " + ", ".join(str(value) for value in values) + ",")
        lines.append("    },")

    count = len(ordered)
    lines += [
        "};",
        "",
        "const u8* font_glyph_data(u16 index) {",
        "    if (index >= font_glyph_count) return font_glyphs[0];",
        "    return font_glyphs[index];",
        "}",
        "",
        f"const u16 font_glyph_count = {count};",
        "",
    ]
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(lines))
    partial = sum(1 for row in ascii_glyphs[ord("A")] for value in row
                  if 0 < value < COVERAGE_LEVELS)
    print(
        f"Wrote {destination}: {len(ascii_glyphs)} ASCII + {len(cp437)} CP437 glyphs; "
        f"Inter size={font.size}px; partial A coverage pixels={partial}; "
        f"cell={CELL_W}x{CELL_H}"
    )


if __name__ == "__main__":
    generate(Path(sys.argv[1] if len(sys.argv) > 1 else "kernel/font_data.c"))
