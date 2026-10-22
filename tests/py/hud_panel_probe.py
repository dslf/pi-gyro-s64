#!/usr/bin/env python3
"""Read a viewer BMP and report the HUD ink clusters (panels) with their bounding boxes and
coverage. Standalone probe, not part of the suite (no *_test.py name).

The HUD draws near-white glyphs on a dark translucent panel; the model shading stays darker, so
clustering bright pixels finds the panels without knowing their colour. The framebuffer is sRGB,
so panel backgrounds are not a fixed value - this probe works on luminance instead.

usage: python tests/py/hud_panel_probe.py <file.bmp> [bright_threshold]
"""
import sys


def read_bmp(path):
    d = open(path, 'rb').read()
    off = int.from_bytes(d[10:14], 'little')
    w = int.from_bytes(d[18:22], 'little')
    h = int.from_bytes(d[22:26], 'little')
    row = (w * 3 + 3) & ~3
    px = [[0] * w for _ in range(h)]
    for y in range(h):          # BMP rows are bottom-up -> index from the top
        base = off + (h - 1 - y) * row
        for x in range(w):
            i = base + x * 3
            px[y][x] = (d[i], d[i + 1], d[i + 2])
    return w, h, px


def lum(c):
    return (0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]) / 255


def main():
    path = sys.argv[1]
    bright = float(sys.argv[2]) if len(sys.argv) > 2 else 0.80
    w, h, px = read_bmp(path)
    rows = []
    for y in range(h):
        xs = [x for x in range(w) if lum(px[y][x]) > bright]
        rows.append((min(xs), max(xs)) if xs else None)
    runs, cur = [], None
    for y, r in enumerate(rows):
        if r is not None:
            if cur is None:
                cur = y
        elif cur is not None:
            runs.append((cur, y - 1))
            cur = None
    if cur is not None:
        runs.append((cur, h - 1))
    print(f"{path}: {w}x{h}, ink threshold {bright:.2f} relative luminance")
    for y0, y1 in runs:
        xs = [t for y in range(y0, y1 + 1) if rows[y] for t in rows[y]]
        x0, x1 = min(xs), max(xs)
        area = (x1 - x0 + 1) * (y1 - y0 + 1)
        ink = sum(1 for y in range(y0, y1 + 1) for x in range(x0, x1 + 1) if lum(px[y][x]) > bright)
        print(f"  ink cluster y {y0}..{y1} x {x0}..{x1} ({x1-x0+1}x{y1-y0+1}) ink {ink} px, "
              f"coverage {100*ink/area:.2f}%")


if __name__ == '__main__':
    main()
