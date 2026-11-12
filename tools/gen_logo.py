#!/usr/bin/env python3
"""Generate the boot/About logo (main/ui/ui_logo.c) - pure Python, no image files, no fonts.

The mark is a thermometer and a water droplet drawn from simple shapes (signed distance
functions); the words ENVIRONMENT / LOGGER use a built-in 5x7 block font. Everything is
rendered with 4x4 supersampling for smooth edges.

Two variants are written: ui_logo_light (dark text, for the light theme) and ui_logo_dark
(light text, for the dark theme). The accent colours match the UI theme
(components/envlogger_core/src/el_ui_status.c, EL_UI_TONE_ACCENT).

Output: LV_IMG_CF_TRUE_COLOR_ALPHA (RGB565 little-endian + 8-bit alpha, LV_COLOR_16_SWAP = 0),
the colour format of the device and the PC simulator.

    python tools/gen_logo.py            (writes main/ui/ui_logo.c)
    python tools/gen_logo.py --preview DIR  (also writes preview images DIR/logo_light.pnm, logo_dark.pnm)
"""
import math
import os
import sys

W, H = 340, 76
SS = 4                                   # supersampling per axis

VARIANTS = {
    # name: (mark accent, droplet accent, text line 1, text line 2)
    "ui_logo_light": ((0x0B, 0x75, 0x77), (0x2B, 0xB5, 0xB0), (0x17, 0x24, 0x2B), (0x0B, 0x75, 0x77)),
    "ui_logo_dark":  ((0x2B, 0xB5, 0xB0), (0x7E, 0xD8, 0xD3), (0xE8, 0xED, 0xF2), (0x2B, 0xB5, 0xB0)),
}

# 5x7 block font, only the letters needed.
FONT = {
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    "N": ["#...#", "##..#", "#.#.#", "#.#.#", "#..##", "#...#", "#...#"],
    "V": ["#...#", "#...#", "#...#", "#...#", ".#.#.", ".#.#.", "..#.."],
    "I": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "M": ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "G": [".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."],
}


# ---------------------------------------------------------------- shapes (distance < 0 = inside)
def sd_circle(x, y, cx, cy, r):
    return math.hypot(x - cx, y - cy) - r


def sd_round_box(x, y, cx, cy, hw, hh, r):
    qx = abs(x - cx) - hw + r
    qy = abs(y - cy) - hh + r
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r


def sd_droplet(x, y, cx, cy, r):
    """A circle of radius r at (cx, cy) with a pointed top (tangent cone)."""
    d = sd_circle(x, y, cx, cy, r)
    tip_y = cy - 2.0 * r
    if y < cy:                                       # cone between the tip and the circle
        t = (cy - y) / (cy - tip_y)
        half = r * (1.0 - t) * 1.05
        dc = abs(x - cx) - half
        if t <= 1.0:
            d = min(d, dc)
    return d


def coverage(d):
    return 1.0 if d <= -0.5 else 0.0 if d >= 0.5 else 0.5 - d


# ---------------------------------------------------------------- layers
def mark_layer(px, py):
    """(alpha_thermometer, alpha_droplet) at pixel-space point."""
    # thermometer: stem + bulb, with a hollow stem showing the "mercury"
    outer = min(sd_round_box(px, py, 22, 30, 7.5, 24, 7.5), sd_circle(px, py, 22, 58, 12))
    inner = min(sd_round_box(px, py, 22, 30, 3.5, 20, 3.5), sd_circle(px, py, 22, 58, 8))
    fill = min(sd_round_box(px, py, 22, 44, 3.5, 12, 3.5), sd_circle(px, py, 22, 58, 8))
    ring = max(outer, -inner)                        # outline of the glass
    therm = min(ring, fill)
    drop = sd_droplet(px, py, 47, 54, 10)
    return therm, drop


def text_cells(word, x0, y0, cell):
    """List of (cx, cy, half) squares for a word drawn with FONT."""
    out = []
    for i, ch in enumerate(word):
        rows = FONT[ch]
        for r, row in enumerate(rows):
            for c, on in enumerate(row):
                if on == "#":
                    out.append((x0 + (i * 6 + c) * cell + cell / 2.0, y0 + r * cell + cell / 2.0, cell / 2.0))
    return out


LINE1 = text_cells("ENVIRONMENT", 76, 4, 4.0)
LINE2 = text_cells("LOGGER", 76, 42, 4.0)


def sd_cells(px, py, cells):
    d = 1e9
    for cx, cy, h in cells:
        if abs(px - cx) > h + 2 or abs(py - cy) > h + 2:
            continue
        d = min(d, sd_round_box(px, py, cx, cy, h + 0.15, h + 0.15, 0.6))
    return d


def render(colors):
    therm_c, drop_c, t1_c, t2_c = colors
    img = []
    for y in range(H):
        row = []
        for x in range(W):
            acc = [0.0, 0.0, 0.0, 0.0]               # premultiplied r, g, b, a
            for sy in range(SS):
                for sx in range(SS):
                    px = x + (sx + 0.5) / SS
                    py = y + (sy + 0.5) / SS
                    layers = []
                    if px < 70:
                        t, dr = mark_layer(px, py)
                        layers.append((t, therm_c))
                        layers.append((dr, drop_c))
                    else:
                        layers.append((sd_cells(px, py, LINE1), t1_c))
                        layers.append((sd_cells(px, py, LINE2), t2_c))
                    for d, c in layers:
                        if d < 0:
                            acc[0] += c[0]; acc[1] += c[1]; acc[2] += c[2]; acc[3] += 1.0
                            break
            n = SS * SS
            a = acc[3] / n
            if a > 0:
                r, g, b = (acc[0] / acc[3], acc[1] / acc[3], acc[2] / acc[3])
            else:
                r = g = b = 0
            row.append((int(round(r)), int(round(g)), int(round(b)), int(round(a * 255))))
        img.append(row)
    return img


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_c = os.path.join(here, "..", "main", "ui", "ui_logo.c")
    preview_dir = sys.argv[sys.argv.index("--preview") + 1] if "--preview" in sys.argv else None
    lines = ["/* Generated by tools/gen_logo.py - do not edit. Thermometer + droplet mark and the words",
             " * ENVIRONMENT LOGGER, drawn from simple shapes. */",
             '#include "lvgl.h"', "",
             "#if LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP != 0",
             '#error "ui_logo.c is RGB565 without byte swap: re-run tools/gen_logo.py for this colour format"',
             "#endif", ""]
    for name, colors in VARIANTS.items():
        img = render(colors)
        data = []
        for row in img:
            for r, g, b, a in row:
                v = rgb565(r, g, b) if a else 0
                data += [v & 0xFF, v >> 8, a]
        lines.append("static const uint8_t %s_map[] = {" % name)
        for i in range(0, len(data), 24):
            lines.append("    " + ",".join("0x%02x" % v for v in data[i:i + 24]) + ",")
        lines.append("};")
        lines.append("const lv_img_dsc_t %s = {" % name)
        lines.append("    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .header.always_zero = 0, .header.reserved = 0,")
        lines.append("    .header.w = %d, .header.h = %d," % (W, H))
        lines.append("    .data_size = sizeof %s_map, .data = %s_map," % (name, name))
        lines.append("};")
        lines.append("")
        if preview_dir:
            bg = (0xF3, 0xF5, 0xF8) if name.endswith("light") else (0x0E, 0x13, 0x18)
            with open(os.path.join(preview_dir, name.replace("ui_", "") + ".pnm"), "wb") as f:
                f.write(b"P6 %d %d 255\n" % (W, H))
                for row in img:
                    for r, g, b, a in row:
                        k = a / 255.0
                        f.write(bytes(int(round(c * k + bc * (1 - k))) for c, bc in zip((r, g, b), bg)))
    with open(out_c, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines))
    print("wrote", os.path.normpath(out_c))


if __name__ == "__main__":
    main()
