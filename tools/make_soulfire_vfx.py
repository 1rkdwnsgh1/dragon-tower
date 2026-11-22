"""Draw the three original Nether Soulfire RGBA sprite strips."""
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


def wing(draw, t):
    flame(draw, (100, 64), 91, 6, pi, 11*sin(t))
    flame(draw, (95, 67), 69, 4, pi-.15, 7*sin(t+1.7))
    flame(draw, (92, 61), 51, 2.5, pi+.16, 5*cos(t))


def mote(draw, t):
    flame(draw, (56, 65), 27+5*sin(t), 4.8, -.13, 4*cos(t))
    flame(draw, (53, 65), 13, 2.2, .15, 0)


def crown(draw, t):
    for i, angle in enumerate((-.99, -.57, -.28, .21, .49, .87)):
        motion = sin(t + i*1.81)
        length = (34 if i in (0, 5) else 46) + 7*motion
        flame(draw, (64, 64), length, 4.6, angle + .13*motion, 6*cos(t+i))
    flame(draw, (61, 64), 33, 7, 0, 1.5*sin(t))


def main():
    for name, render in (("wing", wing), ("mote", mote), ("crown", crown)):
        strip = Image.new("RGBA", (CELL * FRAMES, CELL))
        for i in range(FRAMES):
            frame = Image.new("RGBA", (CELL*SS, CELL*SS))
            render(ImageDraw.Draw(frame), 2*pi*i/FRAMES)
            strip.paste(frame.resize((CELL, CELL), Image.Resampling.LANCZOS), (CELL*i, 0))
        strip.save(OUT / f"fx_soulfire_{name}.png", optimize=True)


if __name__ == "__main__":
    main()
