#!/usr/bin/env python3
"""Draws the launcher cover, src/assets/cover.png (128x96): a speed chart
and the name, in the colours of the program. Run again to change it."""

import math
from PIL import Image, ImageDraw, ImageFont

S = 4  # drawn 4x larger, then reduced (smooth edges)
W, H = 128 * S, 96 * S
im = Image.new("RGB", (W, H))
d = ImageDraw.Draw(im)
for y in range(H):  # background gradient
    t = y / (H - 1)
    d.line([(0, y), (W, y)], fill=(int(16 - 10 * t), int(22 - 14 * t), int(46 - 26 * t)))

# chart: gradient area under a rising, wobbling curve
x0, x1, y0, y1 = 10 * S, 118 * S, 40 * S, 86 * S
pts = []
for i in range(41):
    t = i / 40
    v = 0.35 + 0.5 * t + 0.08 * math.sin(t * 13) + 0.05 * math.sin(t * 29)
    pts.append((x0 + (x1 - x0) * t, y1 - (y1 - y0) * v))
area = Image.new("L", (W, H), 0)
ImageDraw.Draw(area).polygon(pts + [(x1, y1), (x0, y1)], fill=255)
fade = Image.new("L", (W, H), 0)
fd = ImageDraw.Draw(fade)
for y in range(y0 - 20 * S, y1):
    fd.line([(0, y), (W, y)], fill=max(0, min(255, int(170 * (y1 - y) / (y1 - y0)))))
cyan = Image.new("RGB", (W, H), (46, 216, 255))
mask = Image.composite(fade, Image.new("L", (W, H), 0), area)
im = Image.composite(cyan, im, mask)
d = ImageDraw.Draw(im)
for gy in range(4):
    yy = y1 - (y1 - y0) * gy / 3
    for xx in range(x0, x1, 4 * S):
        d.line([(xx, yy), (xx + S, yy)], fill=(70, 80, 120), width=S // 2)
d.line(pts, fill=(46, 216, 255), width=2 * S, joint="curve")
ex, ey = pts[-1]
d.ellipse([ex - 3 * S, ey - 3 * S, ex + 3 * S, ey + 3 * S], fill=(236, 240, 255))

# SD card icon and title
cx, cy = 10 * S, 9 * S
d.rounded_rectangle([cx, cy, cx + 14 * S, cy + 18 * S], radius=2 * S, fill=(46, 216, 255))
d.polygon([(cx + 9 * S, cy), (cx + 15 * S, cy), (cx + 15 * S, cy + 6 * S)], fill=(16, 22, 46))
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 17 * S)
d.text((30 * S, 7 * S), "SD Bench", font=font, fill=(236, 240, 255))
im.resize((128, 96), Image.LANCZOS).save("src/assets/cover.png")
print("src/assets/cover.png")
