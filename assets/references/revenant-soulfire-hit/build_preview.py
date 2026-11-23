from math import sin, tau
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

HERE = Path(__file__).resolve().parent
ASSETS = HERE.parents[1]
DRAGON = ASSETS / "dragons" / "revenant"
CELL = 128
RESAMPLE = Image.Resampling.LANCZOS


def tile(sheet, index, size):
    return sheet.crop((index * size, 0, (index + 1) * size, size))


def opacity(image, amount):
    out = image.copy()
    out.putalpha(out.getchannel("A").point(lambda a: round(a * amount)))
    return out


def flame(variant, phase, width, height, mirror=False):
    sheet = SHEETS[variant]
    frame = tile(sheet, 3 + phase % 8, CELL).crop((12, 24, 116, 105))
    alpha = frame.getchannel("A")
    frame.putalpha(Image.frombytes("L", frame.size, bytes(
        round(a * min(1, (frame.height - 1 - i // frame.width) / 7))
        for i, a in enumerate(alpha.tobytes())
    )))
    if mirror:
        frame = ImageOps.mirror(frame)
    return frame.resize((width, height), RESAMPLE)


def make_layers(option, phase):
    rear, front = (Image.new("RGBA", (CELL, CELL)) for _ in range(2))
    sway = round(3 * sin(tau * phase / 8))
    if option == "a":
        rear.alpha_composite(flame("c", phase, 51, 87), (5 + sway, 10))
        front.alpha_composite(flame("c", phase + 3, 34, 51, True), (84 - sway, 71))
    else:
        rear.alpha_composite(flame("b", phase, 69, 87), (3 + sway, 10))
        rear.alpha_composite(flame("c", phase + 3, 47, 75, True), (76 - sway, 30))
        front.alpha_composite(flame("b", phase + 5, 60, 54, True), (39, 71))
    return rear, front


SHEETS = {v: Image.open(DRAGON / f"fx_soulfire_{v}_rear.png").convert("RGBA") for v in "bc"}
LAYERS = {}
for option in "ab":
    for phase in range(8):
        LAYERS[option, phase] = make_layers(option, phase)
    for layer, label in enumerate(("rear", "front")):
        strip = Image.new("RGBA", (8 * CELL, CELL))
        for phase in range(8):
            frame = LAYERS[option, phase][layer]
            assert frame.getbbox() and frame.getbbox()[0] > 0 and frame.getbbox()[2] < CELL
            strip.paste(frame, (phase * CELL, 0))
        strip.save(HERE / f"hit-{option}-{label}.png", optimize=True)

goblin = tile(Image.open(ASSETS / "enemies/goblin/idle.png").convert("RGBA"), 0, 256)
goblin = goblin.crop(goblin.getbbox())
ground = tile(Image.open(DRAGON / "fx_soulfire_a_rear.png").convert("RGBA"), 5, 128)
ground = ground.crop(ground.getbbox())
board = Image.new("RGBA", (1000, 640), (18, 25, 33, 255))
draw = ImageDraw.Draw(board)
title = ImageFont.load_default(27)
label = ImageFont.load_default(16)
caption = ImageFont.load_default(13)
draw.text((28, 19), "Nether Soulfire  /  Damage attachment study", font=title, fill="#e5f3f3")
draw.text((28, 57), "A  Split wisps     B  Dense flames     C  Ground fire only", font=label, fill="#9db8be")


def fit_layer(pic, x, foot, height, alpha, scale=1):
    width = max(1, round(height * .92 * scale))
    size = max(1, round(height * .92 * scale))
    image = opacity(pic.resize((width, size), RESAMPLE), alpha)
    board.alpha_composite(image, (round(x - size * .5), round(foot - height * .29 - size * 104 / 128)))


def enemy(x, foot, height, phase, option=None, amount=1, scale=1, facing=1):
    if option:
        rear, front = LAYERS[option, phase]
        if facing == -1:
            rear, front = ImageOps.mirror(rear), ImageOps.mirror(front)
        rear_alpha, front_alpha = (.80, .63) if option == "a" else (.92, .84)
        fit_layer(rear, x - (.17 * height * facing if option == "a" else 0), foot, height, amount * (1 if height < 40 else rear_alpha), scale)
    body = goblin if facing == 1 else ImageOps.mirror(goblin)
    board.alpha_composite(body.resize((round(height * body.width / body.height), height), RESAMPLE), (round(x-height*body.width/body.height*.5), foot-height))
    if option and height >= 40:
        fit_layer(front, x, foot, height, amount * front_alpha, scale)


def panel(x, y, w, h, text):
    draw.rounded_rectangle((x, y, x+w, y+h), radius=10, fill="#24313a", outline="#3a5059")
    draw.text((x+12, y+8), text, font=caption, fill="#c2d6d8")


def bed(x, foot, width):
    effect = opacity(ground.resize((width, round(width*.32)), RESAMPLE), .65)
    board.alpha_composite(effect, (x-width//2, foot-effect.height+7))


for i, opt in enumerate(("a", "b", None)):
    x = 28 + 320*i
    panel(x, 92, 304, 188, ("A  /  light", "B  /  heavy", "C  /  no confirmed damage")[i])
    for j, size in enumerate((32, 64, 112)):
        center = x + (41, 118, 225)[j]
        bed(center, 244, (55, 82, 119)[j])
        enemy(center, 244, size, j+1, opt, facing=1 if j % 2 == 0 else -1)
        draw.text((center, 258), f"H {size}", anchor="mt", font=caption, fill="#b6cbd0")

for i, (name, alpha, scale, phase) in enumerate((
    ("Contact  /  0 ms", .50, .80, 0),
    ("Attack  /  60 ms", .875, .95, 0),
    ("Sustain  /  260 ms", 1, 1, 2),
    ("Release  /  +50 ms", .50, .93, 3),
    ("Clear  /  +100 ms", 0, .86, 4),
)):
    x = 28 + i*192
    panel(x, 299, 176, 144, name)
    bed(x+88, 425, 106)
    enemy(x+88, 416, 96, phase, "a", alpha, scale)

panel(28, 461, 944, 151, "Same ground strip  /  damage confirmed for targets 1 and 3")
for x in range(130, 880, 118):
    bed(x, 585, 164)
for i, (x, size, opt) in enumerate(((260, 88, "a"), (433, 78, None), (577, 114, "a"), (733, 44, None))):
    enemy(x, 580, size, 2*i, opt, facing=1 if i % 2 == 0 else -1)
    draw.text((x, 590), str(i+1), anchor="mt", font=caption, fill="#b9d2d3")
board.convert("RGB").save(HERE / "comparison.png", optimize=True)
