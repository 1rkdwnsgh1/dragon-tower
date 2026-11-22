"""Draw the three original Nether Soulfire RGBA sprite strips."""
from argparse import ArgumentParser
from math import cos, pi, sin
from pathlib import Path

from PIL import Image, ImageDraw


CELL = 128
SS = 3
FRAMES = 12
OUT = Path(__file__).resolve().parents[1] / "assets/dragons/revenant"
COLORS = ((22, 175, 156, 100), (54, 224, 190, 218), (193, 255, 229, 240))


def flame(draw, root, length, width, angle, curl):
    def point(u, v):
        return tuple(round(c * SS) for c in (
            root[0] + cos(angle) * u - sin(angle) * v,
            root[1] + sin(angle) * u + cos(angle) * v,
        ))

    for scale, color in zip((1, .75, .39), COLORS):
        l, w, c = length * scale, width * scale, curl * scale
        coords = [(0, 0), (.15*l, -w), (.38*l, -.57*w), (.68*l, c-w*.3),
                  (l, c), (.72*l, c+w*.45), (.48*l, w*.28), (.28*l, w), (0, w*.35)]
        draw.polygon([point(u, v) for u, v in coords], fill=color)


def wake_art(board, box, width):
    """Extract the teal strokes from the flattened B-panel layer examples."""
    crop = board.crop(box).convert("RGB")
    art = Image.new("RGBA", crop.size)
    for y in range(crop.height):
        for x in range(crop.width):
            r, g, b = crop.getpixel((x, y))
            strength = min(1, max(0, (g - r - 4) / 27))
            alpha = strength * min(1, max(0, (g - 21) / 90))
            if alpha:
                art.putpixel((x, y), (r, g, b, round(alpha * 255)))
    return art.resize((width, round(width * crop.height / crop.width)), Image.Resampling.LANCZOS)


def wake_strip(art, name, left, middle, anchored_right):
    strip = Image.new("RGBA", (CELL * FRAMES, CELL))
    for i in range(FRAMES):
        phase = 2 * pi * i / FRAMES
        for x in range(art.width):
            u = x / (art.width - 1)
            free_end = 1 - u if anchored_right else abs(2 * u - 1)
            offset = round(5 * free_end * sin(phase - 4 * free_end))
            top = middle - art.height // 2 + offset
            strip.paste(art.crop((x, 0, x + 1, art.height)), (i * CELL + left + x, top))
    strip.save(OUT / f"fx_soulfire_{name}.png", optimize=True)


def mote(draw, t):
    flame(draw, (56, 65), 27+5*sin(t), 4.8, -.13, 4*cos(t))
    flame(draw, (53, 65), 13, 2.2, .15, 0)


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("concept", type=Path, help="Approved 1672×941 A/B/C concept board")
    parser.add_argument("mouth", type=Path, help="Approved 1536×1024 mouth concept board")
    args = parser.parse_args()
    with Image.open(args.concept) as board:
        if board.size != (1672, 941):
            parser.error("expected the original 1672×941 concept board")
        rear = wake_art(board, (592, 643, 757, 724), 106)
        front = wake_art(board, (778, 651, 930, 721), 102)
    wake_strip(rear, "wing", 7, 68, True)
    wake_strip(front, "wing_front", 13, 64, False)
    with Image.open(args.mouth) as board:
        if board.size != (1536, 1024):
            parser.error("expected the original 1536×1024 mouth concept board")
        for name, box, width, left, middle in (
            ("orbit", (211, 627, 308, 702), 96, 16, 64),
            ("core", (369, 631, 431, 694), 64, 32, 63),
            ("crown", (484, 615, 586, 705), 108, 2, 66),
            ("fragments", (625, 632, 716, 697), 102, 14, 64),
        ):
            wake_strip(wake_art(board, box, width), name, left, middle, False)
    strip = Image.new("RGBA", (CELL * FRAMES, CELL))
    for i in range(FRAMES):
        frame = Image.new("RGBA", (CELL*SS, CELL*SS))
        mote(ImageDraw.Draw(frame), 2*pi*i/FRAMES)
        strip.paste(frame.resize((CELL, CELL), Image.Resampling.LANCZOS), (CELL*i, 0))
    strip.save(OUT / "fx_soulfire_mote.png", optimize=True)


if __name__ == "__main__":
    main()
