"""Paint pose-matched RGBA sheets for the Revenant's Nether Soulfire."""
from math import pi, sin
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

CELL = 256
AA = 3
COUNT = 40
OUT = Path(__file__).resolve().parents[1] / "assets/dragons/revenant"

# Near-wing outer edges on the 256px motion cells, chest to tips.
WINGS = [
    (71, 24), (59, 51), (37, 77), (33, 98), (70, 177),
    (82, 193), (43, 104), (45, 65), (56, 51), (71, 24),
]


def bezier(points, t):
    u = 1 - t
    weights = (u**3, 3*u*u*t, 3*u*t*t, t**3)
    return tuple(sum(p[axis] * w for p, w in zip(points, weights))
                 for axis in (0, 1))


def tongue(image, path, width, opacity):
    """Paint a tapered turquoise flame with a translucent fringe and mint core."""
    outline = [[], []]
    for step in range(25):
        t = step / 24
        x, y = bezier(path, t)
        x1, y1 = bezier(path, min(1, t + 0.01))
        dx, dy = x1 - x, y1 - y
        length = max(0.01, (dx*dx + dy*dy)**0.5)
        taper = max(0, sin(pi*t))**0.72 * (1 - 0.64*t)
        for side, edge in zip((-1, 1), outline):
            edge.append(((x - side*dy/length*width*taper)*AA,
                         (y + side*dx/length*width*taper)*AA))
    silhouette = outline[0] + outline[1][::-1]
    layer = Image.new("RGBA", image.size)
    ImageDraw.Draw(layer).polygon(silhouette, fill=(20, 193, 151, int(155*opacity)))
    image.alpha_composite(layer.filter(ImageFilter.GaussianBlur(2.2*AA)))
    image.alpha_composite(layer)
    core = Image.new("RGBA", image.size)
    ash = [bezier(path, (step + 2) / 24) for step in range(14)]
    ImageDraw.Draw(core).line([(x*AA, y*AA) for x, y in ash],
                              fill=(134, 251, 209, int(155*opacity)),
                              width=max(1, int(width*AA*0.32)))
    image.alpha_composite(core)


def paint(frame):
    rear, front = (Image.new("RGBA", (CELL*AA, CELL*AA)) for _ in range(2))
    pulse = sin(2*pi*frame/10)
    if frame < 20:
        chest_y = 182 if frame < 8 else 153 if frame == 8 else 147 if frame < 14 else 137
        wing = (42, 67) if frame < 14 else (58, 183) if frame < 16 else (83, 39)
        spread = min(1, frame / 13)
        opacity = min(1, frame / 4)
        feed = 0
    elif frame < 30:
        chest_y, wing = 135, WINGS[frame - 20]
        spread, opacity, feed = 1, 1, 0.88
    else:
        chest_y = (135, 146, 154, 156, 174, 169, 187, 189, 175, 172)[frame - 30]
        wing = (72, 48) if frame == 30 else (39, 82)
        spread = max(0.28, 1 - (frame - 30)/10)
        opacity, feed = max(0, (39 - frame)/9), 0
    cx, cy = 141, chest_y
    wx, wy = wing
    reach_x = cx + (wx - cx)*spread
    reach_y = cy + (wy - cy)*spread
    if frame < 20:
        # The small warm-up flame curls up from the existing painted chest core.
        ignition = min(1, frame / 5) * min(1, (20 - frame) / 9)
        tongue(front, [(cx-4, cy+6), (cx+14, cy+2),
                       (cx-10, cy-8), (cx+5, cy-20)], 8, ignition*0.82)
    # Long tongues flow outward from the back of the ribs with the wing stroke.
    tongue(rear, [(cx, cy), (cx-27, cy-19*spread), (reach_x+7, reach_y-8),
                  (reach_x-8*spread, reach_y+6*pulse)], 13*spread, opacity*0.79)
    tongue(rear, [(cx-6, cy+7), (cx-39*spread, cy+19*spread),
                  (cx-74*spread, cy-4*spread), (cx-103*spread, cy+8*pulse)],
           9*spread, opacity*0.88)
    tongue(rear, [(cx-12, cy-2), (cx-37*spread, cy-10*spread),
                  (reach_x-5, reach_y+32*spread), (reach_x-11*spread, reach_y+18*spread)],
           8*spread, opacity*0.62)
    # The leading edge crosses only below the ribs. A second thin flame feeds the jaw.
    tongue(front, [(cx+10, cy-5), (cx+13, cy+19*spread),
                   (cx-22*spread, cy+20*spread), (cx-40*spread, cy+9+pulse*5*spread)],
           6, opacity*0.67)
    if feed:
        tongue(front, [(cx+4, cy+4), (cx+16, cy-14), (cx+37, cy-16+pulse*2),
                       (192, 149)], 4.2, feed)
    return [im.resize((CELL, CELL), Image.Resampling.LANCZOS) for im in (rear, front)]


def main():
    sheets = [Image.new("RGBA", (CELL*COUNT, CELL)) for _ in range(2)]
    for frame in range(COUNT):
        for sheet, art in zip(sheets, paint(frame)):
            sheet.paste(art, (frame * CELL, 0))
    for name, sheet in zip(("rear", "front"), sheets):
        sheet.save(OUT / f"fx_soulfire_wrap_{name}.png", optimize=True)


if __name__ == "__main__":
    main()
