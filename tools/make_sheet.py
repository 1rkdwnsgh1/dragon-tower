#!/usr/bin/env python3
# 낱장 프레임 PNG들을 캐릭터 시트 규격(docs/character-pipeline.md 4장)의
# 가로 한 줄 스프라이트 시트로 변환한다.
#
# 규격: 칸 256x256, 발 기준점 (128, 240), 왼쪽 지향.
# 오른쪽을 보는 원본은 --mirror 로 반전해서 넣는다.
#
# 사용 예:
#   python tools/make_sheet.py 프레임폴더 -o assets/dragons/revenant/walk.png
#   python tools/make_sheet.py 프레임폴더 -o idle.png --mirror   # 오른쪽 지향 원본
#
# 프레임은 파일 이름 순서로 이어 붙인다 (walk_01.png, walk_02.png ...).
#
# 흔들림 방지가 핵심이다: 프레임마다 따로 줄이거나 따로 정렬하면
# 재생할 때 캐릭터가 덜컹거린다. 그래서 모든 프레임의 공통 윤곽 상자
# (union bbox)를 구해 같은 배율로 줄이고 같은 위치에 놓는다.
# 원본 캔버스 안에서의 프레임 간 움직임(들썩임 등)은 그대로 보존된다.

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow가 필요합니다: pip install Pillow")

ALPHA_MIN = 8  # 이 값 이하의 알파는 빈 공간으로 본다 (희미한 잔티 무시)


def content_bbox(img: Image.Image):
    """알파가 ALPHA_MIN을 넘는 영역의 윤곽 상자. 완전히 비면 None."""
    alpha = img.getchannel("A").point(lambda a: 255 if a > ALPHA_MIN else 0)
    return alpha.getbbox()


def main():
    p = argparse.ArgumentParser(description="낱장 프레임 -> 가로 스프라이트 시트")
    p.add_argument("frames_dir", help="프레임 PNG가 들어 있는 폴더")
    p.add_argument("-o", "--out", required=True, help="출력 시트 PNG 경로")
    p.add_argument("--cell", type=int, default=256, help="칸 크기 (기본 256)")
    p.add_argument("--ratio", type=float, default=0.70,
                   help="칸 높이 대비 캐릭터(공통 윤곽) 높이 비율 (기본 0.70)")
    p.add_argument("--anchor-y", type=int, default=None,
                   help="바닥(발) y 좌표 (기본: 칸 256 기준 240, 칸 크기에 비례)")
    p.add_argument("--mirror", action="store_true",
                   help="좌우 반전 (오른쪽 지향 원본을 왼쪽 지향으로)")
    args = p.parse_args()

    cell = args.cell
    anchor_y = args.anchor_y if args.anchor_y is not None else round(cell * 240 / 256)
    anchor_x = cell // 2

    frames_dir = Path(args.frames_dir)
    paths = sorted(q for q in frames_dir.iterdir()
                   if q.suffix.lower() == ".png" and q.is_file())
    if not paths:
        sys.exit(f"{frames_dir} 에 PNG가 없습니다.")

    frames = []
    size = None
    for q in paths:
        img = Image.open(q).convert("RGBA")
        if args.mirror:
            img = img.transpose(Image.FLIP_LEFT_RIGHT)
        if size is None:
            size = img.size
        elif img.size != size:
            sys.exit(f"프레임 크기가 다릅니다: {q.name} {img.size} != {size}. "
                     "같은 캔버스 크기의 프레임만 섞을 수 있습니다.")
        frames.append((q.name, img))

    # 모든 프레임의 공통 윤곽 상자
    boxes = []
    for name, img in frames:
        b = content_bbox(img)
        if b is None:
            sys.exit(f"{name} 가 완전히 투명합니다.")
        boxes.append(b)
    ux0 = min(b[0] for b in boxes)
    uy0 = min(b[1] for b in boxes)
    ux1 = max(b[2] for b in boxes)
    uy1 = max(b[3] for b in boxes)
    uw, uh = ux1 - ux0, uy1 - uy0

    # 공통 배율: 공통 윤곽 높이 = ratio * cell
    scale = (args.ratio * cell) / uh
    sw, sh = round(uw * scale), round(uh * scale)
    if sw > cell:
        # 가로로 긴 동작(비행 등)은 높이 비율대로 하면 옆이 잘릴 수 있다.
        # 배율을 낮춰 폭을 칸 안에 맞추고 알려 준다.
        scale = cell / uw * 0.98
        sw, sh = round(uw * scale), round(uh * scale)
        print(f"[알림] 폭이 칸을 넘어 배율을 낮췄습니다. 실제 높이 비율: {sh / cell:.2f}")

    # 공통 윤곽의 가로 중심을 anchor_x에, 바닥을 anchor_y에
    left = anchor_x - sw / 2
    top = anchor_y - sh
    if top < 0:
        print(f"[경고] 위가 {-top:.0f}px 잘립니다. --ratio 를 낮추세요.")

    sheet = Image.new("RGBA", (cell * len(frames), cell), (0, 0, 0, 0))
    for i, (name, img) in enumerate(frames):
        # 프레임 전체를 같은 배율·같은 위치 규칙으로 변환해 프레임 간 움직임을 보존
        crop = img.crop((ux0, uy0, ux1, uy1))
        crop = crop.resize((sw, sh), Image.LANCZOS)
        fcell = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
        fcell.alpha_composite(crop, (round(left), round(top)))
        sheet.alpha_composite(fcell, (cell * i, 0))

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print(f"완료: {out}  프레임 {len(frames)}장, 칸 {cell}x{cell}, "
          f"캐릭터 높이 {sh}px (칸의 {sh / cell:.0%}), 바닥 y={anchor_y}")
    print(f"dragon.txt 에 적을 값: frame_w={cell}")


if __name__ == "__main__":
    main()
