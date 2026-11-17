#!/usr/bin/env python3
"""Draws the 3DS banner, icon and banner tune from TINY's boot screen.

    python3 tools/make_gfx.py    # assets/banner.png, icon.png, banner.wav

Pure python (zlib only), so it runs anywhere without Pillow.
"""

import math
import os
import struct
import zlib

# the same five lines TINY.C draws at boot
LOGO = [
    "#####  ###  #   #  #   #",
    "  #     #   ##  #   # # ",
    "  #     #   # # #    #  ",
    "  #     #   #  ##    #  ",
    "  #    ###  #   #    #  ",
]
ROW_COLORS = [(0, 250, 220), (0, 250, 220), (70, 140, 255), (255, 90, 210), (255, 90, 210)]


def blank(w, h, rgb):
    return [bytearray(rgb * w) for _ in range(h)]


def paint_logo(rows, ox, oy, scale):
    for y, line in enumerate(LOGO):
        rgb = ROW_COLORS[y]
        for x, ch in enumerate(line):
            if ch != "#":
                continue
            for dy in range(scale):
                row = rows[oy + y * scale + dy]
                for dx in range(scale):
                    i = (ox + x * scale + dx) * 3
                    if 0 <= i < len(row) - 2:
                        row[i:i + 3] = bytes(rgb)


def write_png(path, rows):
    h = len(rows)
    w = len(rows[0]) // 3
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(raw, 9))
    out += chunk(b"IEND", b"")
    with open(path, "wb") as fp:
        fp.write(out)


def write_wav(path, samples, rate=22050):
    data = b"".join(struct.pack("<h", max(-32767, min(32767, int(s * 32767))))
                    for s in samples)
    with open(path, "wb") as fp:
        fp.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt ")
        fp.write(struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16))
        fp.write(b"data" + struct.pack("<I", len(data)) + data)


def chime(rate=22050):
    """The boot chime, as square waves - the PC speaker, for the home menu."""
    out = []
    for hz, ms, vol in ((392, 110, 0.20), (523, 110, 0.20), (659, 110, 0.22),
                        (784, 110, 0.22), (1047, 130, 0.24), (1568, 260, 0.20)):
        n = int(rate * ms / 1000)
        period = max(2, rate // hz)
        fade = max(1, n // 12)
        for i in range(n):
            env = min(1.0, i / fade, (n - i) / fade)
            out.append(vol * env * (1.0 if (i % period) < period // 2 else -1.0))
    out.extend([0.0] * int(rate * 0.12))          # a little silence at the end
    return out


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    assets = os.path.join(here, "..", "assets")
    os.makedirs(assets, exist_ok=True)

    # banner: 256x128, the logo at 6 pixels a cell, on a dark blue field
    w, h = 256, 128
    rows = blank(w, h, (6, 10, 26))
    for y in range(h):                       # a faint vertical glow
        k = (h - y) / h
        for x in range(w):
            i = x * 3
            rows[y][i + 0] = min(255, 6 + int(10 * k))
            rows[y][i + 1] = min(255, 10 + int(16 * k))
            rows[y][i + 2] = min(255, 26 + int(34 * k))
    paint_logo(rows, (w - 24 * 6) // 2, 34, 6)
    for x in range(40, 216):                 # a cyan rule under the logo
        rows[86][x * 3 + 0] = 0
        rows[86][x * 3 + 1] = 120
        rows[86][x * 3 + 2] = 140
    write_png(os.path.join(assets, "banner.png"), rows)

    # icon: 48x48, the same logo at 2 pixels a cell
    w, h = 48, 48
    rows = blank(w, h, (6, 10, 26))
    paint_logo(rows, (w - 24 * 2) // 2, 19, 2)
    write_png(os.path.join(assets, "icon.png"), rows)

    write_wav(os.path.join(assets, "banner.wav"), chime())
    print("wrote banner.png, icon.png and banner.wav in", os.path.normpath(assets))


if __name__ == "__main__":
    main()
