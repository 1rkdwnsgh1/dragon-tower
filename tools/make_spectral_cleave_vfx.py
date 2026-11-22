"""Build the Spectral Wing Cleave cast and impact RGBA strips from its existing blade art."""

import math
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ASSETS = Path(__file__).resolve().parents[1] / "assets" / "dragons" / "revenant"
PREFIX = "fx_spectral_cleave_"
CELL = 256
MINT = (83, 240, 218)
TEAL = (29, 174, 151)
IVORY = (222, 247, 224)
DARK = (19, 113, 111)


def opacity(image, strength):
    faded = image.copy()
    faded.putalpha(image.getchannel("A").point(lambda value: round(value * strength)))
    return faded


def polygon(draw, points, rgb, alpha):
    draw.polygon(points, fill=(*rgb, round(alpha)))


def tapered_ribbon(draw, points, width, strength):
    count = len(points)
    for i, (a, b) in enumerate(zip(points, points[1:])):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        if length < 0.01:
            continue
        start = width * math.sin(math.pi * i / (count - 1)) ** .85
        end = width * math.sin(math.pi * (i + 1) / (count - 1)) ** .85
        nx, ny = -dy / length, dx / length
        polygon(draw, [(a[0] + nx * start, a[1] + ny * start),
                       (b[0] + nx * end, b[1] + ny * end),
                       (b[0] - nx * end, b[1] - ny * end),
                       (a[0] - nx * start, a[1] - ny * start)], TEAL, 185 * strength)
    draw.line(points[2:-2], fill=(*MINT, round(210 * strength)), width=max(1, round(width * .55)))


def shard(draw, x, y, angle, length, width, strength):
    dx, dy = math.cos(angle), math.sin(angle)
    nx, ny = -dy, dx
    tip = (x + dx * length, y + dy * length)
    shoulder = (x + dx * length * .52 + nx * width, y + dy * length * .52 + ny * width)
    flank = (x + dx * length * .28 - nx * width * .72, y + dy * length * .28 - ny * width * .72)
    notch = (x + dx * length * .39 + nx * width * .12, y + dy * length * .39 + ny * width * .12)
    polygon(draw, [(x, y), shoulder, tip, notch, flank], TEAL, 225 * strength)
    polygon(draw, [flank, notch, tip, (x + dx * length * .41, y + dy * length * .41)], DARK, 175 * strength)
    draw.line([shoulder, tip], fill=(*MINT, round(242 * strength)), width=max(1, round(width * .24)))
    draw.line([notch, tip], fill=(*IVORY, round(214 * strength)), width=max(1, round(width * .12)))


def glow(image, radius, strength):
    halo = Image.new("RGBA", image.size, MINT + (0,))
    halo.putalpha(image.getchannel("A").filter(ImageFilter.GaussianBlur(radius)).point(
        lambda value: round(value * strength)))
    halo.alpha_composite(image)
    return halo


def blade_art():
    layers = []
    for key in ("membrane", "edge"):
        path = ASSETS / (PREFIX + key + ".png")
        layers.append(Image.open(path).convert("RGBA").crop((CELL, 0, 2 * CELL, CELL)))
    layers[0].alpha_composite(layers[1])
    return layers[0].crop(layers[0].getbbox())


def draw_blade(canvas, art, center, size, angle, strength):
    blade = art.resize(size, Image.Resampling.LANCZOS)
    blade = opacity(blade, strength).rotate(angle, Image.Resampling.BICUBIC, expand=True)
    canvas.alpha_composite(blade, (round(center[0] - blade.width / 2), round(center[1] - blade.height / 2)))


def cast_frame(i, blade):
    # Logical coordinates of the body cell are offset by 52 on each axis.
    rear, front = (Image.new("RGBA", (360, 360)) for _ in range(2))
    flow = (0, .12, .27, .43, .60, .76, .87, .98, 1, 1, .96, .89, .76, .51, .24, 0)[i]
    if not flow:
        return rear, front
    for canvas, phase in ((rear, math.pi), (front, 0)):
        draw = ImageDraw.Draw(canvas)
        for strand in range(3):
            points = []
            for segment in range(28):
                angle = phase + .32 + (2.54 - strand * .31) * segment / 27 + strand * .23
                x = 180 + (139 - strand * 24) * math.cos(angle)
                y = 286 + (8 + strand * 3) * math.sin(angle)
                y += 5.5 * math.sin(angle * 4 + i * .67 + strand)
                points.append((x, y))
            tapered_ribbon(draw, points, 5.5 - strand * 1.35, flow * (1 - strand * .19))
    rng = random.Random(431)
    for n in range(15):
        x = 53 + rng.random() * 252
        age = (i + n * .76) % 9 / 9
        y = 285 - age * (12 + n % 5 * 7)
        shard(ImageDraw.Draw(front if n % 3 == 0 else rear), x + age * 14, y, -.51,
              5 + n % 6 * 2, 1.3 + n % 3, flow * (1 - age) * .7)
    if 3 <= i <= 12:
        # The silhouette begins low and nearly horizontal, then pivots upward at the near wing.
        poses = [(221, 275, 38, 35, -60, .25), (236, 266, 57, 58, -66, .39),
                 (243, 263, 75, 73, -62, .49), (251, 252, 81, 93, -47, .54),
                 (258, 239, 90, 115, -31, .65), (266, 224, 101, 145, -20, .76),
                 (270, 209, 111, 161, -10, .90), (272, 199, 120, 172, 0, .96),
                 (270, 200, 124, 180, 7, 1), (267, 205, 111, 172, 13, .29)]
        x, y, w, h, angle, strength = poses[i - 3]
        draw_blade(rear, blade, (x - 29, y + 11), (w, h), angle - 13, strength * .42)
        drive = (0, 8, 13, 15, 15, 14, 13, 9, 5, 0)[i - 3]
        draw_blade(front, blade, (x + drive, y), (w + drive, h), angle, strength)
        draw = ImageDraw.Draw(front)
        if 4 <= i <= 8:
            points = [(x - 50 + 96 * j / 28,
                       287 - (21 + (i - 4) * 8) * (j / 28) ** 2.5)
                      for j in range(29)]
            tapered_ribbon(draw, points, 6.5, strength)
        for n in range(5):
            shard(draw, x - 27 - n * 16, y + 17 + n * 10, -.30 - .16 * n,
                  11 + n * 4, 2 + n % 3, strength * .62)
    if 4 <= i <= 14:
        # A broad thin rear wake leads from the tail back toward the raised wing.
        points = [(180 - 115 * math.sin(math.pi * j / 34),
                   284 - (105 + 6 * i) * j / 34) for j in range(35)]
        tapered_ribbon(ImageDraw.Draw(rear), points, 4.5, flow * .54)
    return glow(rear, 5, .48), glow(front, 4, .55)


def impact_frame(i, blade):
    rear, front = (Image.new("RGBA", (256, 256)) for _ in range(2))
    reach = (.32, .50, .65, .83, .99, 1.02, 1.07, 1.10)[i]
    power = (.35, .55, .72, 1, 1, .62, .33, .10)[i]
    ox, oy = 128, 128
    blade_alpha = (.53, .72, .86, .90, .72, .28, .08, .0)[i]
    draw_blade(rear, blade, (145 + 19 * reach, 132 - 17 * reach),
               (round(70 + 65 * reach), round(92 + 127 * reach)), 22, blade_alpha)
    rng = random.Random(372)
    draw = ImageDraw.Draw(front)
    branches = [(-2.72, 42), (-1.74, 45), (-.68, 49), (.36, 45), (1.37, 52), (2.26, 43)]
    for direction, length in branches:
        points = [(ox, oy)]
        for step in range(1, 6):
            distance = length * reach * step / 5
            delta = direction + rng.uniform(-.31, .31)
            points.append((ox + distance * math.cos(delta), oy + distance * math.sin(delta)))
        if i < 6:
            draw.line(points, fill=(*TEAL, round(210 * power)), width=4)
            draw.line(points, fill=(*IVORY, round(245 * power)), width=1 if i < 3 else 2)
            x, y = points[2]
            shard(draw, x, y, direction + .56, length * .27, 1.4, power)
    for n in range(24):
        angle = n * math.tau / 24 + rng.uniform(-.13, .13)
        length = rng.uniform(38, 72) if n % 4 == 0 else rng.uniform(8, 30)
        width = rng.uniform(5, 11) if n % 4 == 0 else rng.uniform(1.5, 4)
        if i < 3 and n % 4 != 0:
            continue
        distance = min(97, 5 + reach ** 3 * (22 + n % 5 * 8) + max(0, i - 4) * 9)
        x, y = ox + distance * math.cos(angle), oy + distance * math.sin(angle)
        layer = rear if n % 3 == 0 else front
        spike_length = min(115 - distance, length * reach * (1 - .09 * max(0, i - 4)))
        shard(ImageDraw.Draw(layer), x, y, angle, spike_length,
              width * reach, power * (.95 if n % 4 == 0 else .72))
    if 3 <= i <= 5:
        burst = (.86, 1, .45)[i - 3]
        for angle, length, width in [(-2.62, 99, 6), (-1.23, 94, 8), (-.49, 83, 7),
                                      (.83, 87, 5), (2.59, 112, 7)]:
            dx, dy = math.cos(angle), math.sin(angle)
            inset = 7 if i < 5 else 24
            shard(draw, ox + dx * inset, oy + dy * inset, angle,
                  length - inset, width, burst)
    if 2 <= i <= 5:
        size = (0, 0, 8, 16, 19, 8, 0, 0)[i]
        polygon(draw, [(ox - size, oy + 1), (ox - 3, oy - 4), (ox + 3, oy - size),
                       (ox + 5, oy - 3), (ox + size, oy + 3), (ox + 4, oy + 5),
                       (ox - 2, oy + size), (ox - 4, oy + 5)], IVORY, 248 * power)
    return glow(rear, 4, .55), glow(front, 5, .82)


def save_strip(frames, name):
    sheet = Image.new("RGBA", (CELL * len(frames), CELL))
    for i, frame in enumerate(frames):
        cell = frame.resize((CELL, CELL), Image.Resampling.LANCZOS)
        sheet.paste(cell, (i * CELL, 0))
    sheet.save(ASSETS / (PREFIX + name + ".png"), optimize=True)


def main():
    blade = blade_art()
    for phase, count, generate in (("cast", 16, cast_frame), ("hit", 8, impact_frame)):
        rear, front = zip(*(generate(i, blade) for i in range(count)))
        save_strip(rear, phase + "_rear")
        save_strip(front, phase + "_front")


if __name__ == "__main__":
    main()
