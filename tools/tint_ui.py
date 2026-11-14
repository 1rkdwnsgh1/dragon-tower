#!/usr/bin/env python3
"""기본 UI 스킨의 조각을 색만 바꿔 다른 스킨을 만든다.

드래곤마다 UI 색을 맞추고 싶을 때 쓴다. 모양은 그대로 두고 **색상(hue)만** 돌리므로
명암·발광·투명도가 유지된다. 회색(채도 낮은 곳)은 건드리지 않아 금속 테두리는 그대로다.

  python tools/tint_ui.py --out assets/ui/shadow --hue -70 --pieces button,pill,toggle_on

색이 의미를 가지는 조각(체력 초록, 경험치 파랑)은 기본으로 제외한다 — 색을 바꾸면
"체력이 적다"는 신호가 흐려진다. 넣고 싶으면 --pieces 에 직접 적는다.
"""
import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow 가 필요합니다: pip install pillow")

import colorsys

DEFAULT_PIECES = ["panel", "button", "button_hover", "button_off", "pill", "pill_hover", "pill_off",
                  "toggle_on", "toggle_off", "toggle_disabled", "bar_frame", "check_on", "square", "square_off"]


def tint(img: Image.Image, hue_shift: float, sat_min: float) -> Image.Image:
    img = img.convert("RGBA")
    px = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            hh, ll, ss = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
            if ss < sat_min:
                continue  # 회색·금속은 그대로 둔다
            hh = (hh + hue_shift / 360.0) % 1.0
            nr, ng, nb = colorsys.hls_to_rgb(hh, ll, ss)
            px[x, y] = (round(nr * 255), round(ng * 255), round(nb * 255), a)
    return img


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--src", default="assets/ui/default", help="가져올 스킨 폴더 (기본 assets/ui/default)")
    p.add_argument("--out", required=True, help="만들 스킨 폴더")
    p.add_argument("--hue", type=float, required=True, help="색상을 돌릴 각도(-180~180). 청록->보라는 -70 정도")
    p.add_argument("--sat-min", type=float, default=0.12, help="이 채도 미만은 건드리지 않는다 (기본 0.12)")
    p.add_argument("--pieces", default=None, help="쉼표로 구분한 조각 이름. 생략하면 기본 목록")
    args = p.parse_args()

    src, out = Path(args.src), Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    names = args.pieces.split(",") if args.pieces else DEFAULT_PIECES
    made = 0
    for n in names:
        n = n.strip()
        f = src / f"{n}.png"
        if not f.exists():
            print(f"  건너뜀: {n}.png 없음")
            continue
        tint(Image.open(f), args.hue, args.sat_min).save(out / f"{n}.png")
        made += 1
        print(f"  {n}.png")
    print(f"{made}개 만들었습니다: {out}")
    print("ui.txt 는 복사되지 않습니다. 여백·fit 은 default 를 따르므로 보통 그대로 두면 됩니다.")


if __name__ == "__main__":
    main()
