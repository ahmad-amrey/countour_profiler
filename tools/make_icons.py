"""Generates the Contour Profiler application icons from one vector description.

Outputs (in resources/):
  icon.ico                Windows (16-256 px, embedded in the .exe)
  icon.icns               macOS bundle icon
  icon_16.png ... icon_1024.png
  logo.svg                vector logo (same geometry)

Usage:  python tools/make_icons.py          (needs Pillow)
"""
import io
import math
import os
import struct
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'resources')

BG = (22, 24, 28)        # graphite tile
EDGE = (46, 51, 60)
GRID = (90, 169, 255)    # calibration grid (drawn faint)
ACCENT = (255, 138, 61)  # the profile

# Geometry in a 1024 x 1024 box.
TILE_MARGIN = 64
TILE_RADIUS = 216
GRID_STEP = 168
# The bent wire: left leg, tight bend, sweeping arc, right leg.
PATH = [
    ('M', (368, 812)),
    ('L', (306, 402)),
    ('C', (290, 300), (342, 256), (414, 256)),
    ('C', (566, 256), (704, 334), (730, 476)),
    ('L', (740, 812)),
]


def bezier(p0, p1, p2, p3, n=64):
    pts = []
    for i in range(1, n + 1):
        t = i / n
        u = 1 - t
        pts.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                    u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))
    return pts


def path_points():
    pts, cur = [], None
    for cmd in PATH:
        if cmd[0] == 'M':
            cur = cmd[1]
            pts.append(cur)
        elif cmd[0] == 'L':
            cur = cmd[1]
            pts.append(cur)
        else:
            seg = bezier(cur, cmd[1], cmd[2], cmd[3])
            pts.extend(seg)
            cur = cmd[3]
    return pts


def render(size, detailed):
    ss = 4  # supersampling
    S = 1024 * ss
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    k = ss
    m = TILE_MARGIN * k
    d.rounded_rectangle((m, m, S - m, S - m), radius=TILE_RADIUS * k, fill=BG + (255,),
                        outline=EDGE + (255,), width=int(10 * k))
    if detailed:
        # Faint calibration grid clipped to the tile.
        grid = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        g = ImageDraw.Draw(grid)
        a = 30
        for i in range(1, 1024 // GRID_STEP + 1):
            x = (1024 - (1024 // GRID_STEP) * GRID_STEP) / 2 + i * GRID_STEP - GRID_STEP / 2
            g.line(((x * k, m), (x * k, S - m)), fill=GRID + (a,), width=int(7 * k))
            g.line(((m, x * k), (S - m, x * k)), fill=GRID + (a,), width=int(7 * k))
        mask = Image.new('L', (S, S), 0)
        ImageDraw.Draw(mask).rounded_rectangle((m, m, S - m, S - m), radius=TILE_RADIUS * k, fill=255)
        img.paste(grid, (0, 0), Image.composite(grid, Image.new('RGBA', (S, S)), mask).getchannel('A'))
    # Smooth round stroke: stamp discs densely along the path.
    width = (64 if detailed else 96) * k
    r = width / 2
    pts = [(x * k, y * k) for x, y in path_points()]
    step = r / 6
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        n = max(1, int(math.hypot(x1 - x0, y1 - y0) / step))
        for i in range(n + 1):
            t = i / n
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            d.ellipse((x - r, y - r, x + r, y + r), fill=ACCENT + (255,))
    # Slightly larger tips, like the cut ends of a wire.
    rt = r * 1.22
    for (x, y) in (pts[0], pts[-1]):
        d.ellipse((x - rt, y - rt, x + rt, y + rt), fill=ACCENT + (255,))
    return img.resize((size, size), Image.LANCZOS)


def write_icns(path, images):
    # PNG-based ICNS entries.
    types = {16: b'icp4', 32: b'icp5', 64: b'icp6', 128: b'ic07', 256: b'ic08', 512: b'ic09', 1024: b'ic10'}
    chunks = b''
    for size, typ in types.items():
        buf = io.BytesIO()
        images[size].save(buf, 'PNG')
        data = buf.getvalue()
        chunks += typ + struct.pack('>I', len(data) + 8) + data
    with open(path, 'wb') as f:
        f.write(b'icns' + struct.pack('>I', len(chunks) + 8) + chunks)


def write_svg(path):
    d = []
    for cmd in PATH:
        if cmd[0] in 'ML':
            d.append('%s %g %g' % (cmd[0], *cmd[1]))
        else:
            d.append('C %g %g, %g %g, %g %g' % (*cmd[1], *cmd[2], *cmd[3]))
    off = (1024 - (1024 // GRID_STEP) * GRID_STEP) / 2 - GRID_STEP / 2
    lines = []
    for i in range(1, 1024 // GRID_STEP + 1):
        x = off + i * GRID_STEP
        lines.append('<path d="M%g %d V%d M%d %g H%d"/>' % (x, TILE_MARGIN, 1024 - TILE_MARGIN, TILE_MARGIN, x,
                                                            1024 - TILE_MARGIN))
    s, e = PATH[0][1], PATH[-1][1]
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" role="img" aria-label="Contour Profiler">
  <defs><clipPath id="tile"><rect x="{TILE_MARGIN}" y="{TILE_MARGIN}" width="{1024 - 2 * TILE_MARGIN}" height="{1024 - 2 * TILE_MARGIN}" rx="{TILE_RADIUS}"/></clipPath></defs>
  <rect x="{TILE_MARGIN}" y="{TILE_MARGIN}" width="{1024 - 2 * TILE_MARGIN}" height="{1024 - 2 * TILE_MARGIN}" rx="{TILE_RADIUS}" fill="#16181C" stroke="#2E333C" stroke-width="10"/>
  <g clip-path="url(#tile)" stroke="#5AA9FF" stroke-opacity="0.12" stroke-width="7" fill="none">{''.join(lines)}</g>
  <path d="{' '.join(d)}" fill="none" stroke="#FF8A3D" stroke-width="64" stroke-linecap="round" stroke-linejoin="round"/>
  <circle cx="{s[0]}" cy="{s[1]}" r="39" fill="#FF8A3D"/>
  <circle cx="{e[0]}" cy="{e[1]}" r="39" fill="#FF8A3D"/>
</svg>
'''
    open(path, 'w', encoding='utf-8').write(svg)


def main():
    os.makedirs(OUT, exist_ok=True)
    sizes = [16, 24, 32, 48, 64, 128, 256, 512, 1024]
    images = {s: render(s, detailed=s >= 48) for s in sizes}
    for s in (16, 32, 48, 64, 128, 256, 512, 1024):
        images[s].save(os.path.join(OUT, 'icon_%d.png' % s))
    # ICO: PIL writes each requested size from the largest image; give it the
    # simplified art for small sizes by saving from per-size images.
    ico_sizes = [16, 24, 32, 48, 64, 128, 256]
    images[256].save(os.path.join(OUT, 'icon.ico'), format='ICO', sizes=[(s, s) for s in ico_sizes],
                     append_images=[images[s] for s in ico_sizes if s != 256])
    write_icns(os.path.join(OUT, 'icon.icns'), images)
    write_svg(os.path.join(OUT, 'logo.svg'))
    print('icons written to', os.path.normpath(OUT))


if __name__ == '__main__':
    sys.exit(main())
