#!/usr/bin/env python3
# 팀원이 만든 캐릭터 이미지를 시트 규격(docs/character-pipeline.md 4장)의
# 가로 한 줄 스프라이트 시트로 변환한다.
#
# 규격: 칸 256x256, 발 기준점 (128, 240), 오른쪽 지향.
# 왼쪽을 보는 원본(망령룡 작업 자료 등)은 --mirror 로 반전해서 넣는다.
#
# 규격을 모르고 만든 원본도 받는다:
#   - 낱장 프레임 폴더 (캔버스 크기가 같으면 가장 좋음)
#   - 이미 가로로 이어 붙인 시트 PNG 1장 (--cols 로 프레임 수 지정)
#   - 프레임마다 캔버스 크기가 다른 낱장들 (--align each 로 처리)
#
# 사용 예:
#   python tools/make_sheet.py 프레임폴더 -o assets/dragons/fire/walk.png
#   python tools/make_sheet.py 원본시트.png --cols 8 -o walk.png
#   python tools/make_sheet.py 프레임폴더 -o walk.png --mirror      # 왼쪽 지향 원본(망령룡)
#   python tools/make_sheet.py 크기제각각폴더 -o idle.png --align each
#
# 프레임은 파일 이름 순서로 이어 붙인다 (walk_01.png, walk_02.png ...).
#
# 흔들림 방지가 핵심이다: 기본(--align union)은 모든 프레임의 공통 윤곽 상자를
# 같은 배율로 줄이고 같은 위치에 놓아, 원본 캔버스 안의 프레임 간 움직임
# (들썩임 등)을 그대로 보존한다. 캔버스 크기가 제각각인 원본은 공통 기준이
# 없으므로 --align each 로 프레임마다 바닥 중앙을 맞춘다. 이 경우 원본에
# 있던 상하 움직임이 사라지고 미세한 흔들림이 생길 수 있으니, 가능하면
# 같은 캔버스 크기로 만들어 달라고 요청하는 것이 좋다.

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


def load_frames(src: Path, cols, mirror):
    """원본(폴더 또는 시트 1장)에서 프레임 목록 [(이름, 이미지)]를 만든다."""
    frames = []
    if src.is_dir():
        paths = sorted(q for q in src.iterdir()
                       if q.suffix.lower() == ".png" and q.is_file())
        if not paths:
            sys.exit(f"{src} 에 PNG가 없습니다.")
        for q in paths:
            frames.append((q.name, Image.open(q).convert("RGBA")))
    elif src.is_file():
        img = Image.open(src).convert("RGBA")
        w, h = img.size
        if cols is None:
            # 칸이 정사각형(폭이 높이의 배수)이면 프레임 수를 추정한다
            if w % h == 0 and w // h >= 2:
                cols = w // h
                print(f"[알림] --cols 미지정: 정사각형 칸으로 보고 {cols}프레임으로 자릅니다.")
            else:
                sys.exit("시트 1장을 입력할 때는 --cols 로 프레임 수를 지정하세요. "
                         f"(이미지 {w}x{h}, 폭이 높이의 배수가 아님)")
        if w % cols != 0:
            sys.exit(f"폭 {w}가 --cols {cols}로 나누어떨어지지 않습니다.")
        fw = w // cols
        for i in range(cols):
            frames.append((f"{src.stem}[{i}]", img.crop((i * fw, 0, (i + 1) * fw, h))))
    else:
        sys.exit(f"{src} 를 찾을 수 없습니다.")
    if mirror:
        frames = [(n, im.transpose(Image.FLIP_LEFT_RIGHT)) for n, im in frames]
    return frames


def main():
    p = argparse.ArgumentParser(description="낱장 프레임/원본 시트 -> 규격 스프라이트 시트")
    p.add_argument("src", help="프레임 PNG 폴더, 또는 이어 붙인 시트 PNG 1장")
    p.add_argument("-o", "--out", required=True, help="출력 시트 PNG 경로")
    p.add_argument("--cols", type=int, default=None,
                   help="입력이 시트 1장일 때 프레임 수")
    p.add_argument("--cell", type=int, default=256, help="출력 칸 크기 (기본 256)")
    p.add_argument("--ratio", type=float, default=0.70,
                   help="칸 높이 대비 캐릭터 높이 비율 (기본 0.70)")
    p.add_argument("--anchor-y", type=int, default=None,
                   help="바닥(발) y 좌표 (기본: 칸 256 기준 240, 칸 크기에 비례)")
    p.add_argument("--mirror", action="store_true",
                   help="좌우 반전 (왼쪽 지향 원본을 오른쪽 지향으로)")
    p.add_argument("--align", choices=["union", "each"], default="union",
                   help="union: 공통 윤곽 기준(기본, 움직임 보존). "
                        "each: 프레임마다 바닥 중앙 정렬(캔버스 크기가 제각각일 때)")
    args = p.parse_args()

    cell = args.cell
    anchor_y = args.anchor_y if args.anchor_y is not None else round(cell * 240 / 256)
    anchor_x = cell // 2

    frames = load_frames(Path(args.src), args.cols, args.mirror)

    boxes = []
    for name, img in frames:
        b = content_bbox(img)
        if b is None:
            sys.exit(f"{name} 가 완전히 투명합니다.")
        boxes.append(b)

    sizes = {im.size for _, im in frames}
    align = args.align
    if align == "union" and len(sizes) > 1:
        print(f"[알림] 프레임 캔버스 크기가 {len(sizes)}종류라 공통 기준을 잡을 수 없어 "
              "--align each 로 전환합니다. 상하 움직임이 사라지고 미세한 흔들림이 "
              "생길 수 있습니다.")
        align = "each"

    sheet = Image.new("RGBA", (cell * len(frames), cell), (0, 0, 0, 0))

    if align == "union":
        # 모든 프레임의 공통 윤곽 상자를 같은 배율·같은 위치로 (움직임 보존)
        ux0 = min(b[0] for b in boxes); uy0 = min(b[1] for b in boxes)
        ux1 = max(b[2] for b in boxes); uy1 = max(b[3] for b in boxes)
        uw, uh = ux1 - ux0, uy1 - uy0
        scale = (args.ratio * cell) / uh
        if round(uw * scale) > cell:
            scale = cell / uw * 0.98
            print(f"[알림] 폭이 칸을 넘어 배율을 낮췄습니다. "
                  f"실제 높이 비율: {uh * scale / cell:.2f}")
        sw, sh = round(uw * scale), round(uh * scale)
        left, top = anchor_x - sw / 2, anchor_y - sh
        if top < 0:
            print(f"[경고] 위가 {-top:.0f}px 잘립니다. --ratio 를 낮추세요.")
        for i, (name, img) in enumerate(frames):
            crop = img.crop((ux0, uy0, ux1, uy1)).resize((sw, sh), Image.LANCZOS)
            fcell = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
            fcell.alpha_composite(crop, (round(left), round(top)))
            sheet.alpha_composite(fcell, (cell * i, 0))
        out_h = sh
    else:
        # 프레임마다 자기 윤곽의 바닥 중앙을 기준점에 맞춘다.
        # 배율은 가장 큰 윤곽 높이 기준으로 하나만 써서 프레임 간 크기를 유지한다.
        max_h = max(b[3] - b[1] for b in boxes)
        max_w = max(b[2] - b[0] for b in boxes)
        scale = (args.ratio * cell) / max_h
        if round(max_w * scale) > cell:
            scale = cell / max_w * 0.98
            print(f"[알림] 폭이 칸을 넘어 배율을 낮췄습니다. "
                  f"실제 높이 비율: {max_h * scale / cell:.2f}")
        for i, ((name, img), b) in enumerate(zip(frames, boxes)):
            bw, bh = b[2] - b[0], b[3] - b[1]
            sw, sh = max(1, round(bw * scale)), max(1, round(bh * scale))
            crop = img.crop(b).resize((sw, sh), Image.LANCZOS)
            fcell = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
            fcell.alpha_composite(crop, (round(anchor_x - sw / 2), anchor_y - sh))
            sheet.alpha_composite(fcell, (cell * i, 0))
        out_h = round(max_h * scale)

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print(f"완료: {out}  프레임 {len(frames)}장, 칸 {cell}x{cell}, "
          f"캐릭터 높이 {out_h}px (칸의 {out_h / cell:.0%}), 바닥 y={anchor_y}, 정렬 {align}")
    print(f"dragon.txt 에 적을 값: frame_w={cell}")


if __name__ == "__main__":
    main()
