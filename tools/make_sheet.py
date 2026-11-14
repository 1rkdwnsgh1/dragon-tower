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
import math
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow가 필요합니다: pip install Pillow")

ALPHA_MIN = 8  # 이 값 이하의 알파는 빈 공간으로 본다 (희미한 잔티 무시)


def drop_background(img: Image.Image, spec: str, tol: int) -> Image.Image:
    """단색 배경을 지운다. 테두리에서 번져 들어가며 지우므로 몸통 안쪽의
    비슷한 색은 남는다(단순 색 비교로 지우면 뼈·회색 부분에 구멍이 난다).

    spec: 'none'(지우지 않음), 'auto'(네 모서리에서 추정), '#RRGGBB'
    """
    if spec == "none":
        return img
    px = img.load()
    w, h = img.size
    if spec == "auto":
        corners = [px[0, 0], px[w - 1, 0], px[0, h - 1], px[w - 1, h - 1]]
        corners = [c[:3] for c in corners]
        bg = max(set(corners), key=corners.count)
    else:
        s = spec.lstrip("#")
        if len(s) != 6:
            sys.exit(f"--bg 값이 이상합니다: {spec} (none / auto / #RRGGBB)")
        bg = tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))

    def near(c):
        return abs(c[0] - bg[0]) <= tol and abs(c[1] - bg[1]) <= tol and abs(c[2] - bg[2]) <= tol

    # 테두리에서 시작해 배경색이 이어지는 곳만 따라간다
    seen = bytearray(w * h)
    stack = []
    for x in range(w):
        for y in (0, h - 1):
            if near(px[x, y]):
                stack.append((x, y))
                seen[y * w + x] = 1
    for y in range(h):
        for x in (0, w - 1):
            if not seen[y * w + x] and near(px[x, y]):
                stack.append((x, y))
                seen[y * w + x] = 1
    while stack:
        x, y = stack.pop()
        px[x, y] = (0, 0, 0, 0)
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < w and 0 <= ny < h and not seen[ny * w + nx] and near(px[nx, ny]):
                seen[ny * w + nx] = 1
                stack.append((nx, ny))
    return img


def content_bbox(img: Image.Image):
    """알파가 ALPHA_MIN을 넘는 영역의 윤곽 상자. 완전히 비면 None."""
    alpha = img.getchannel("A").point(lambda a: 255 if a > ALPHA_MIN else 0)
    return alpha.getbbox()


def parse_grid(spec):
    """'파일.png@행x열:행번호,열범위' 를 (경로, 행수, 열수, 행, [열...]) 로 읽는다.

    여러 종이 행으로, 동작이 열로 들어간 격자 시트에서 한 종의 칸만 꺼낸다.
    격자 크기(@행x열)는 반드시 적는다. 칸 크기를 추측하면 틀리기 쉽다
    (2048x1024 는 128칸으로도 256칸으로도 나뉜다).
    행·열 번호는 1부터 센다. 칸 크기가 정수로 나누어떨어지지 않아도 된다.

    예:
      monsters-desk.png@4x8:2,1-4   4행8열 격자의 2행 1~4열
      monsters.png@4x4:3,2-3        4행4열 격자의 3행 2~3열
    """
    if "@" not in spec or ":" not in spec:
        return None
    path, rest = spec.split("@", 1)
    if ":" not in rest:
        return None
    grid, sel = rest.split(":", 1)
    if "x" not in grid.lower():
        return None
    gr, gc = grid.lower().split("x", 1)
    parts = sel.split(",")
    if len(parts) < 2:
        return None
    try:
        nrows, ncols, row = int(gr), int(gc), int(parts[0])
    except ValueError:
        return None
    cols = []
    for p in parts[1:]:
        p = p.strip()
        if "-" in p:
            x, y = p.split("-", 1)
            cols.extend(range(int(x), int(y) + 1))
        else:
            cols.append(int(p))
    return Path(path), nrows, ncols, row, cols


def load_grid(path: Path, nrows: int, ncols: int, row: int, cols, mirror, bg="none", bg_tol=24):
    """격자 시트에서 지정한 행·열의 칸만 꺼내 프레임 목록으로 만든다."""
    img = Image.open(path).convert("RGBA")
    w, h = img.size
    cw, ch = w / ncols, h / nrows
    if row < 1 or row > nrows:
        sys.exit(f"{path}: {nrows}행 격자인데 {row}행을 요청했습니다.")
    frames = []
    for c in cols:
        if c < 1 or c > ncols:
            sys.exit(f"{path}: {ncols}열 격자인데 {c}열을 요청했습니다.")
        box = (round((c - 1) * cw), round((row - 1) * ch), round(c * cw), round(row * ch))
        f = drop_background(img.crop(box), bg, bg_tol)
        if mirror:
            f = f.transpose(Image.FLIP_LEFT_RIGHT)
        frames.append((f"{path.name}[{row},{c}]", f))
    return frames


def load_frames(src: Path, cols, mirror, bg="none", bg_tol=24):
    """원본(폴더 또는 시트 1장)에서 프레임 목록 [(이름, 이미지)]를 만든다."""
    frames = []
    if src.is_dir():
        paths = sorted(q for q in src.iterdir()
                       if q.suffix.lower() == ".png" and q.is_file())
        if not paths:
            sys.exit(f"{src} 에 PNG가 없습니다.")
        for q in paths:
            frames.append((q.name, drop_background(Image.open(q).convert("RGBA"), bg, bg_tol)))
    elif src.is_file():
        img = drop_background(Image.open(src).convert("RGBA"), bg, bg_tol)
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


def union_box(boxes):
    return (min(b[0] for b in boxes), min(b[1] for b in boxes),
            max(b[2] for b in boxes), max(b[3] for b in boxes))


def multi(args, cell, anchor_x, anchor_y):
    """한 드래곤의 여러 상태를 같은 배율·같은 위치로 변환한다.

    상태마다 따로 변환하면 비행처럼 윤곽이 큰 동작이 더 작게 그려진다
    (날개를 펼쳐 윤곽 높이가 커지는데 그 높이를 --ratio 에 맞추므로).
    여기서는 기준 상태의 윤곽으로 배율을 한 번 정하고 모든 상태에 그대로 쓴다.
    원본 캔버스 안에서의 상태 간 위치 차이도 그대로 보존된다.
    """
    sets = {}
    for item in args.set:
        if "=" not in item:
            sys.exit(f"--set 형식은 상태=경로 입니다: {item}")
        state, path = item.split("=", 1)
        sets[state.strip()] = path.strip()

    loaded = {}
    for state, path in sets.items():
        grid = parse_grid(path)
        if grid:
            frames = load_grid(*grid, args.mirror, args.bg, args.bg_tol)
        else:
            frames = load_frames(Path(path), args.cols, args.mirror, args.bg, args.bg_tol)
        boxes = []
        for name, img in frames:
            b = content_bbox(img)
            if b is None:
                sys.exit(f"{name} 가 완전히 투명합니다.")
            boxes.append(b)
        loaded[state] = (frames, union_box(boxes))

    tweak = {}
    for item in args.scale_state:
        if "=" not in item:
            sys.exit(f"--scale-state 형식은 상태=배수 입니다: {item}")
        k, v = item.split("=", 1)
        if k.strip() not in loaded:
            sys.exit(f"--scale-state {k.strip()} 는 --set 에 없습니다.")
        try:
            tweak[k.strip()] = float(v)
        except ValueError:
            sys.exit(f"--scale-state 배수를 읽을 수 없습니다: {v}")

    ref = args.ref or ("idle" if "idle" in loaded else next(iter(loaded)))
    if ref not in loaded:
        sys.exit(f"--ref {ref} 는 --set 에 없습니다.")

    # 1) 기준 상태의 윤곽 높이를 --ratio 로 맞추는 배율
    rx0, ry0, rx1, ry1 = loaded[ref][1]
    scale = (args.ratio * cell) / (ry1 - ry0)

    # 2) 모든 상태가 칸 안에 들어가도록 배율의 상한을 구한다.
    #    상태마다 바닥을 anchor_y 에 맞추므로, 각 상태가 그 선 위에 들어가야 한다.
    limits = []
    for state, (_, (ux0, uy0, ux1, uy1)) in loaded.items():
        t = tweak.get(state, 1.0)
        limits.append(anchor_y / (uy1 - uy0) / t)             # 바닥선 위에 들어가야 한다
        limits.append(2.0 * anchor_x / (ux1 - ux0) / t)       # 가로 중심 기준으로 칸 안에
        limits.append(2.0 * (cell - anchor_x) / (ux1 - ux0) / t)
    cap = min(limits) * 0.995
    if cap < scale:
        print(f"[알림] 가장 큰 상태가 칸을 넘어 공통 배율을 {scale:.4f} → {cap:.4f} 로 낮췄습니다. "
              f"기준 상태 높이가 칸의 {(ry1 - ry0) * cap / cell:.0%}가 됩니다(목표 {args.ratio:.0%}).")
        scale = cap

    # 3) 배율은 모든 상태가 공유하고(크기가 변하지 않게), 위치는 상태마다
    #    자기 윤곽의 바닥 중앙을 기준점에 맞춘다.
    #    원본 캔버스 안의 상태 간 위치 차이는 믿지 않는다. 패키지가 따로 만들어져
    #    기준이 다를 수 있고, 공중에 떠 보이게 하는 것은 게임이 화면에서 처리한다
    #    (날 때는 화면 위쪽으로 올라가고 그림자가 작아진다).

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    for state, (frames, (ux0, uy0, ux1, uy1)) in loaded.items():
        st_scale = scale * tweak.get(state, 1.0)
        # 이 상태의 바닥 중앙을 기준점으로. 프레임 간 움직임은 보존된다.
        ox = anchor_x - ((ux0 + ux1) * 0.5) * st_scale
        oy = anchor_y - uy1 * st_scale
        sheet = Image.new("RGBA", (cell * len(frames), cell), (0, 0, 0, 0))
        for i, (name, img) in enumerate(frames):
            w = max(1, round(img.width * st_scale))
            h = max(1, round(img.height * st_scale))
            small = img.resize((w, h), Image.LANCZOS)
            # 칸보다 큰 원본을 음수 좌표에도 놓을 수 있게 넉넉한 임시 캔버스를 쓴다
            pad = max(w, h, cell)
            tmp = Image.new("RGBA", (cell + 2 * pad, cell + 2 * pad), (0, 0, 0, 0))
            tmp.alpha_composite(small, (round(ox) + pad, round(oy) + pad))
            fcell = tmp.crop((pad, pad, pad + cell, pad + cell))
            sheet.alpha_composite(fcell, (cell * i, 0))
        path = out_dir / f"{state}.png"
        sheet.save(path)
        bh = round((uy1 - uy0) * st_scale)
        mark = " (기준)" if state == ref else (f" x{tweak[state]:g}" if state in tweak else "")
        print(f"완료: {path}  프레임 {len(frames)}장, 윤곽 높이 {bh}px{mark}")
    print(f"공통 배율 {scale:.4f}, 칸 {cell}x{cell}, 바닥 y={anchor_y}")
    print(f"dragon.txt 에 적을 값: frame_w={cell}")
    return 0


def main():
    p = argparse.ArgumentParser(description="낱장 프레임/원본 시트 -> 규격 스프라이트 시트")
    p.add_argument("src", nargs="?", help="프레임 PNG 폴더, 또는 이어 붙인 시트 PNG 1장")
    p.add_argument("-o", "--out", required=True,
                   help="출력 시트 PNG 경로. --set 을 쓸 때는 출력 폴더")
    p.add_argument("--set", action="append", default=[], metavar="상태=경로",
                   help="한 드래곤의 여러 상태를 한 번에 변환한다(권장). "
                        "예: --set idle=프레임/대기 --set fly=프레임/비행. "
                        "격자 시트는 '파일.png@행x열:행번호,열범위' 로 칸을 지정한다 "
                        "(예: --set move=monsters-desk.png@4x8:2,1-4). 번호는 1부터 센다. "
                        "모든 상태에 같은 배율·같은 위치를 써서 상태가 바뀔 때 크기가 변하지 않는다.")
    p.add_argument("--scale-state", action="append", default=[], metavar="상태=배수",
                   help="--set 과 함께. 그 상태만 공통 배율에 배수를 곱한다. "
                        "원본 그림 자체가 다른 상태보다 작거나 크게 그려졌을 때만 쓴다. "
                        "예: --scale-state fly=1.08")
    p.add_argument("--ref", default=None, metavar="상태",
                   help="--set 과 함께. 크기 기준이 되는 상태(기본: idle, 없으면 첫 번째). "
                        "이 상태의 윤곽 높이가 --ratio 가 되고 나머지는 같은 배율을 쓴다.")
    p.add_argument("--cols", type=int, default=None,
                   help="입력이 시트 1장일 때 프레임 수")
    p.add_argument("--cell", type=int, default=256, help="출력 칸 크기 (기본 256)")
    p.add_argument("--ratio", type=float, default=0.70,
                   help="칸 높이 대비 캐릭터 높이 비율 (기본 0.70)")
    p.add_argument("--anchor-y", type=int, default=None,
                   help="바닥(발) y 좌표 (기본: 칸 256 기준 240, 칸 크기에 비례)")
    p.add_argument("--bg", default="none", metavar="none|auto|#RRGGBB",
                   help="단색 배경을 지운다. auto는 네 모서리에서 추정 (기본 none)")
    p.add_argument("--bg-tol", type=int, default=24,
                   help="배경색으로 볼 색 차이 허용치 (기본 24)")
    p.add_argument("--mirror", action="store_true",
                   help="좌우 반전 (왼쪽 지향 원본을 오른쪽 지향으로)")
    p.add_argument("--align", choices=["union", "each"], default="union",
                   help="union: 공통 윤곽 기준(기본, 움직임 보존). "
                        "each: 프레임마다 바닥 중앙 정렬(캔버스 크기가 제각각일 때)")
    args = p.parse_args()

    cell = args.cell
    anchor_y = args.anchor_y if args.anchor_y is not None else round(cell * 240 / 256)
    anchor_x = cell // 2

    if args.set:
        return multi(args, cell, anchor_x, anchor_y)
    if not args.src:
        sys.exit("프레임 폴더를 지정하거나 --set 을 쓰세요.")
    print("[알림] 상태를 하나씩 따로 변환하면 상태마다 캐릭터 크기가 달라질 수 있습니다.\n"
          "       한 드래곤의 여러 상태는 --set 으로 한 번에 변환하세요.")

    frames = load_frames(Path(args.src), args.cols, args.mirror, args.bg, args.bg_tol)

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
