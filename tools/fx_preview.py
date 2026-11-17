#!/usr/bin/env python3
"""스킬 효과(FX)가 **본체 어디에 어떤 크기로 붙는지** 그림으로 보여 준다.

왜 이게 필요한가
----------------
이 환경(리눅스 컨테이너)은 `src/win` 을 **컴파일만 할 수 있고 실행할 수 없다.**
그래서 `fx_scale`·`fx_offset_x`·`fx_offset_y` 를 적어도 그것이 맞는지 볼 방법이 없었다.
`check_sheets.py` 는 시트 한 장의 칸·크기만 보고, 합성 결과는 보지 않는다.
이 도구는 `Renderer::drawFxSheet` 와 **같은 계산**으로 본체와 FX 를 겹쳐 한 장의 PNG 로 낸다.

특히 **캐릭터 크기(`charScale`, 0.5~10.0배)** 를 세 가지로 나란히 놓는다.
보정값은 "100% 기준 px" 이므로 `charScale` 을 곱해야 하고, 그러면 세 배율의 그림이
**서로 똑같이 보여야 한다.** 한 칸이라도 다르게 보이면 곱셈이 빠진 것이다.

**주의 — 2026-11-16 현재 코드는 이렇게 하지 않는다.**
`Renderer::drawFxSheet` 는 `cx = f.a.x; cy = f.a.y + f.offsetY` 로 보정값을 화면 px 로
그대로 쓰고, 좌우 뒤집기도 `false` 로 고정되어 있다. 이 도구는 **자동 큐 2번 항목에서
고쳐질 쪽(= 사람이 2026-11-16 21:03 에 정한 쪽)** 을 기준으로 만들었다:

    base = kDragonRadius * 3 * charScale
    h    = base * fx_scale * fx_render_scale * k(단계)
    cx   = a.x + fx_offset_x * charScale * dirSign      <- charScale 곱, dirSign 뒤집기
    cy   = a.y + fx_offset_y * charScale                <- charScale 곱

따라서 2번이 머지되기 전에는 **이 그림과 실제 게임 화면이 다르다.** 2번이 들어가면 같아진다.

쓰는 법
-------
    python3 tools/fx_preview.py --dragon revenant --char-scale 0.5,1.0,10.0 --out /tmp/fx.png
    python3 tools/fx_preview.py --body assets/dragons/revenant/skill_pierce.png \
        --fx assets/dragons/revenant/fx_pierce.png \
        --fx-scale 1.8 --fx-offset-x 130 --out /tmp/fx.png

`--dragon` 은 `assets/dragons/<키>/dragon.txt` 에서 `fx=` 가 적힌 스킬을 전부 찾아
스킬마다 두 줄(오른쪽 지향 / 뒤집은 것)을 만든다. `fx=` 를 쓰는 스킬이 하나도 없으면
그렸을 것이 없다고 알리고 **종료 코드 0** 으로 끝난다(검증 절차를 막지 않는다).

읽을 것이 없거나 시트 칸이 맞지 않으면 종료 코드 1.
"""
import argparse
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Pillow 가 필요하다: pip install pillow")
    sys.exit(1)

# include/core/Config.h 와 맞춰 둔 값. 저쪽이 바뀌면 이쪽도 바꾼다.
K_DRAGON_RADIUS = 34.0
K_CHAR_SCALE_MIN, K_CHAR_SCALE_MAX = 0.5, 10.0
# Renderer::drawDragon 의 칸 안 바닥 기준선
K_ANCHOR_FRAC = 240.0 / 256.0

CELL = 340          # 한 칸의 그림 영역(px)
PAD = 16
LABEL_H = 22
ROW_LABEL_W = 230
BG = (40, 42, 52, 255)
GRID = (96, 104, 130, 255)
ACCENT = (120, 220, 200, 255)


def font(size=13):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
              "/usr/share/fonts/TTF/DejaVuSans.ttf"):
        if Path(p).exists():
            try:
                return ImageFont.truetype(p, size)
            except OSError:
                pass
    return ImageFont.load_default()


FONT = font(13)
FONT_SMALL = font(11)


def load_cfg(path):
    """dragon.txt 를 {섹션: {키: 값}} 으로 읽는다. preview_ui.py 와 같은 방식."""
    cfg, sec = {}, None
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split(";")[0].strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            sec = line[1:-1]
            cfg.setdefault(sec, {})
            continue
        if "=" in line and sec:
            k, v = line.split("=", 1)
            cfg[sec][k.strip()] = v.strip()
    return cfg


class Sheet:
    """Sprites::loadSheet 와 같은 칸 나누기."""

    def __init__(self, path, frame_w=0, render_scale=1.0):
        self.path = Path(path)
        self.im = Image.open(self.path).convert("RGBA")
        w, h = self.im.size
        fw = frame_w if frame_w > 0 else h
        if fw > w:
            fw = w
        if fw <= 0 or w % fw != 0:
            raise ValueError(f"{self.path.name}: 너비 {w} 가 칸 너비 {fw} 로 나뉘지 않는다")
        self.frame_w, self.frame_h = fw, h
        self.frames = w // fw
        self.render_scale = render_scale

    def frame(self, idx):
        i = max(0, min(idx, self.frames - 1))
        return self.im.crop((i * self.frame_w, 0, (i + 1) * self.frame_w, self.frame_h))


def phase_look(phase):
    """Renderer.cpp 의 fxPhaseLook. 단계 중간(q=0.5)을 본다."""
    q = 0.5
    if phase == "windup":
        return 0.55 + 0.45 * q, 0.35 + 0.65 * q
    if phase == "tail":
        return 1.0 + 0.18 * q, 1.0 - q
    return 1.0, 1.0


def paste(canvas, img, cx, cy, w, h, flip, alpha, zoom, ox, oy):
    """월드 좌표(cx, cy 중심, w x h)를 zoom 배로 줄여 canvas 에 붙인다."""
    pw, ph = max(1, int(round(w * zoom))), max(1, int(round(h * zoom)))
    im = img.resize((pw, ph), Image.LANCZOS)
    if flip:
        im = im.transpose(Image.FLIP_LEFT_RIGHT)
    if alpha < 0.999:
        a = im.getchannel("A").point(lambda v: int(v * alpha))
        im.putalpha(a)
    canvas.alpha_composite(im, (int(round(ox + cx * zoom - pw / 2)),
                                int(round(oy + cy * zoom - ph / 2))))


def render_cell(body, fx, char_scale, dir_sign, p):
    """본체 + FX 한 칸. 본체 중심을 월드 (0,0) 으로 둔다."""
    cell = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    dr = ImageDraw.Draw(cell)
    k, alpha = phase_look(p["phase"])
    base = K_DRAGON_RADIUS * 3.0 * char_scale

    # --- 본체: drawDragon 의 render_scale 바닥 보정까지 같게 ---
    brs = body.render_scale
    body_h = base * brs
    body_w = body_h * body.frame_w / body.frame_h
    body_cy = -(K_ANCHOR_FRAC - 0.5) * base * (brs - 1.0)

    # --- FX: drawFxSheet ---
    fx_h = base * p["fx_scale"] * fx.render_scale * k
    fx_w = fx_h * fx.frame_w / fx.frame_h
    fx_cx = p["fx_offset_x"] * char_scale * dir_sign
    fx_cy = p["fx_offset_y"] * char_scale

    xs = [-body_w / 2, body_w / 2, fx_cx - fx_w / 2, fx_cx + fx_w / 2]
    ys = [body_cy - body_h / 2, body_cy + body_h / 2, fx_cy - fx_h / 2, fx_cy + fx_h / 2,
          base * 0.5]
    wmin, wmax, hmin, hmax = min(xs), max(xs), min(ys), max(ys)
    span = max(wmax - wmin, hmax - hmin) * 1.12
    zoom = (CELL - 2 * PAD) / span
    ox = PAD + (CELL - 2 * PAD) / 2 - (wmin + wmax) / 2 * zoom
    oy = PAD + (CELL - 2 * PAD) / 2 - (hmin + hmax) / 2 * zoom

    # 바닥선(발 위치)과 본체 중심 십자
    gy = oy + base * 0.5 * zoom
    dr.line([(2, gy), (CELL - 2, gy)], fill=(GRID[0], GRID[1], GRID[2], 150), width=1)
    body_sheet = body.frame(p["body_frame"])
    fx_sheet = fx.frame(p["fx_frame"])

    if p["fx_front"]:
        paste(cell, body_sheet, 0, body_cy, body_w, body_h, dir_sign < 0, 1.0, zoom, ox, oy)
        paste(cell, fx_sheet, fx_cx, fx_cy, fx_w, fx_h, dir_sign < 0, alpha, zoom, ox, oy)
    else:
        paste(cell, fx_sheet, fx_cx, fx_cy, fx_w, fx_h, dir_sign < 0, alpha, zoom, ox, oy)
        paste(cell, body_sheet, 0, body_cy, body_w, body_h, dir_sign < 0, 1.0, zoom, ox, oy)

    # 본체 중심 -> FX 중심. 보정값이 어디로 갔는지 눈으로 본다.
    bx, by = ox, oy
    fxp = (ox + fx_cx * zoom, oy + fx_cy * zoom)
    dr.line([(bx - 7, by), (bx + 7, by)], fill=GRID, width=1)
    dr.line([(bx, by - 7), (bx, by + 7)], fill=GRID, width=1)
    if abs(fx_cx) + abs(fx_cy) > 0.5:
        dr.line([(bx, by), fxp], fill=ACCENT, width=1)
    dr.ellipse([fxp[0] - 3, fxp[1] - 3, fxp[0] + 3, fxp[1] + 3], outline=ACCENT, width=1)
    dr.text((4, CELL - 15), f"x{char_scale:g}  view {zoom * 100:.0f}%  base {base:.0f}px",
            font=FONT_SMALL, fill=(200, 204, 216, 255))
    return cell


def build(entries, scales, out):
    """세로는 효과 x 방향, 가로는 캐릭터 배율. 그림 안 글자는 ASCII 로만 쓴다 —
    이 환경에 한글 TTF 가 없어 한글은 네모로 나온다(설명은 PR 본문·문서에 쓴다)."""
    rows = len(entries) * 2
    head = 52
    W = ROW_LABEL_W + len(scales) * CELL + PAD
    H = head + rows * CELL + PAD
    cv = Image.new("RGBA", (W, H), BG)
    dr = ImageDraw.Draw(cv)
    dr.text((PAD, 5), "FX preview - same math as Renderer::drawFxSheet "
                      "(fx_offset_* multiplied by charScale)", font=FONT,
            fill=(236, 238, 246, 255))
    dr.text((PAD, 22), "All three columns must look IDENTICAL. "
                       "If one differs, the charScale multiply is missing.",
            font=FONT_SMALL, fill=(170, 220, 206, 255))
    for ci, s in enumerate(scales):
        dr.text((ROW_LABEL_W + ci * CELL + 6, 34),
                f"charScale {s:g}", font=FONT, fill=ACCENT)
    ri = 0
    for e in entries:
        for dir_sign, tag in ((1.0, "facing right (a->b, dirSign +1)"),
                              (-1.0, "facing left  (dirSign -1, flip)")):
            y = head + ri * CELL
            dr.line([(PAD, y), (W - PAD, y)], fill=(70, 74, 90, 255), width=1)
            lines = [e["ascii"], tag,
                     f"fx_scale={e['fx_scale']:g} fx_render_scale={e['fx'].render_scale:g}",
                     f"fx_offset_x={e['fx_offset_x']:+g} fx_offset_y={e['fx_offset_y']:+g}",
                     f"fx sheet: {e['fx'].path.name} ({e['fx'].frames} frames)",
                     f"body    : {e['body'].path.name} ({e['body'].frames} frames)",
                     f"phase={e['phase']} fx_front={'yes' if e['fx_front'] else 'no'}"]
            for li, t in enumerate(lines):
                dr.text((PAD, y + 10 + li * 16), t, font=FONT_SMALL if li else FONT,
                        fill=(236, 238, 246, 255) if li == 0 else (186, 192, 208, 255))
            for ci, s in enumerate(scales):
                cv.alpha_composite(render_cell(e["body"], e["fx"], s, dir_sign, e),
                                   (ROW_LABEL_W + ci * CELL, y))
            ri += 1
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    cv.convert("RGB").save(out)
    print(f"그렸다: {out}  ({W}x{H}, 효과 {len(entries)}개 x 2방향 x 배율 {len(scales)}개)")


def from_dragon(key, args):
    d = Path("assets/dragons") / key
    txt = d / "dragon.txt"
    if not txt.exists():
        print(f"없다: {txt}")
        sys.exit(1)
    cfg = load_cfg(txt)
    entries = []
    for sec in sorted((s for s in cfg if s.startswith("skill.")),
                      key=lambda s: int(s.split(".")[1])):
        sk = cfg[sec]
        if not sk.get("fx"):
            continue
        fxp = d / f"fx_{sk['fx']}.png"
        if not fxp.exists():
            print(f"건너뜀 {sec} ({sk.get('name', '')}): {fxp.name} 이 없다")
            continue
        motion = sk.get("motion", "")
        bodyp = d / f"skill_{motion}.png" if motion else d / "attack.png"
        if not bodyp.exists():
            bodyp = d / "attack.png"
        if not bodyp.exists():
            bodyp = d / "idle.png"
        body = Sheet(bodyp, int(sk.get("motion_frame_w", 0) or 0),
                     float(sk.get("motion_render_scale", 1.0)))
        fx = Sheet(fxp, int(sk.get("fx_frame_w", 0) or 0),
                   float(sk.get("fx_render_scale", 1.0)))
        entries.append({
            "label": f"[{sec}] {sk.get('name', '')} / fx={sk['fx']}",
            "ascii": f"[{sec}] fx={sk['fx']} motion={motion or '-'}",
            "body": body, "fx": fx,
            "fx_scale": float(sk.get("fx_scale", 1.0)),
            "fx_offset_x": float(sk.get("fx_offset_x", 0.0)),
            "fx_offset_y": float(sk.get("fx_offset_y", 0.0)),
            "fx_front": (sk.get("fx_front", "yes").lower() in ("yes", "true", "1")),
            "phase": args.phase, "body_frame": args.body_frame, "fx_frame": args.fx_frame,
        })
    if not entries:
        print(f"{key}: fx= 가 적힌 스킬이 없다. 그릴 것이 없으므로 그냥 끝낸다.")
        print("(자동 큐 3번 항목이 fx= 를 배선하면 이 도구가 그림을 낸다)")
        sys.exit(0)
    return entries


def main():
    ap = argparse.ArgumentParser(description="FX 합성 미리보기 (본체 + 효과 시트)")
    ap.add_argument("--dragon", help="assets/dragons/<키> 의 dragon.txt 에서 fx= 스킬을 전부 본다")
    ap.add_argument("--body", help="본체 모션 시트 경로(직접 지정할 때)")
    ap.add_argument("--fx", help="FX 시트 경로(직접 지정할 때)")
    ap.add_argument("--fx-scale", type=float, default=1.0)
    ap.add_argument("--fx-offset-x", type=float, default=0.0, help="100%% 기준 px")
    ap.add_argument("--fx-offset-y", type=float, default=0.0, help="100%% 기준 px")
    ap.add_argument("--fx-render-scale", type=float, default=1.0)
    ap.add_argument("--fx-frame-w", type=int, default=0)
    ap.add_argument("--body-frame-w", type=int, default=0)
    ap.add_argument("--body-render-scale", type=float, default=1.0)
    ap.add_argument("--fx-back", action="store_true", help="fx_front=no 처럼 본체 뒤에 그린다")
    ap.add_argument("--body-frame", type=int, default=0)
    ap.add_argument("--fx-frame", type=int, default=0)
    ap.add_argument("--phase", choices=("windup", "hold", "tail"), default="hold")
    ap.add_argument("--char-scale", default="0.5,1.0,10.0",
                    help="가로로 나란히 놓을 캐릭터 배율 (기본 0.5,1.0,10.0)")
    ap.add_argument("--out", default="/tmp/fx.png")
    args = ap.parse_args()

    try:
        scales = [float(s) for s in args.char_scale.split(",") if s.strip()]
    except ValueError:
        print("--char-scale 은 0.5,1.0,10.0 처럼 숫자를 쉼표로 적는다")
        sys.exit(1)
    if not scales:
        print("--char-scale 이 비었다")
        sys.exit(1)
    for s in scales:
        if not (K_CHAR_SCALE_MIN <= s <= K_CHAR_SCALE_MAX):
            print(f"주의: {s:g} 배는 게임이 허용하는 범위"
                  f"({K_CHAR_SCALE_MIN:g}~{K_CHAR_SCALE_MAX:g}) 밖이다")

    try:
        if args.dragon:
            entries = from_dragon(args.dragon, args)
        else:
            if not (args.body and args.fx):
                print("--dragon 이나 (--body 와 --fx) 중 하나는 있어야 한다")
                sys.exit(1)
            entries = [{
                "label": f"{Path(args.fx).name}",
                "ascii": f"fx={Path(args.fx).name}",
                "body": Sheet(args.body, args.body_frame_w, args.body_render_scale),
                "fx": Sheet(args.fx, args.fx_frame_w, args.fx_render_scale),
                "fx_scale": args.fx_scale,
                "fx_offset_x": args.fx_offset_x, "fx_offset_y": args.fx_offset_y,
                "fx_front": not args.fx_back,
                "phase": args.phase, "body_frame": args.body_frame, "fx_frame": args.fx_frame,
            }]
    except (ValueError, OSError) as e:
        print(f"읽기 실패: {e}")
        sys.exit(1)
    build(entries, scales, args.out)


if __name__ == "__main__":
    main()
