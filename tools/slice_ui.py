#!/usr/bin/env python3
"""UI 원본 시트에서 조각을 잘라 UI 스킨 폴더를 만든다.

원본은 투명 배경 PNG 한 장에 여러 조각이 흩어져 있다. 아래 표에 적은 좌표로 잘라서
`assets/ui/<스킨>/` 에 이름을 붙여 저장한다. **좌표를 표로 남기는 이유**는 같은 원본에서
다시 자를 수 있게 하기 위해서다. 눈으로 보고 손으로 자르면 다음에 재현할 수 없다.

쓰는 법:
    python tools/slice_ui.py --out assets/ui/default \\
        --icons 아이콘.png --panels 버튼.png --bars 체력바.png --frame 창틀.png

좌표는 원본 이미지 크기를 기준으로 한다(표의 `size` 와 다르면 비율로 환산한다).
잘라낸 뒤에는 조각의 실제 내용에 맞춰 여백을 다시 다듬는다(trim).
"""

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow가 필요합니다: pip install Pillow")

ALPHA_MIN = 24

# 원본별 (기준 크기, {저장이름: (x1, y1, x2, y2)})
SHEETS = {
    "panels": ((1672, 941), {
        # 가로 버튼 3가지 상태. 9조각으로 늘려 쓰기 좋은 단순한 형태를 골랐다.
        "button": (63, 612, 465, 718),
        "button_hover": (510, 596, 936, 733),
        "button_off": (967, 612, 1351, 718),
        # 작은 알약형 버튼(좁은 자리용)
        "pill": (60, 779, 301, 860),
        "pill_hover": (333, 771, 579, 868),
        "pill_off": (607, 778, 848, 860),
        # 토글 스위치
        "toggle_off": (892, 786, 1029, 853),
        "toggle_on": (1057, 780, 1212, 858),
        "toggle_disabled": (1239, 786, 1371, 851),
        # 작은 네모 버튼(아이콘 자리)
        "square": (1401, 763, 1467, 830),
        "square_hover": (1489, 762, 1557, 830),
        "square_off": (1578, 763, 1644, 830),
        # 제목 띠(장식이 큰 것)
        "title": (39, 79, 1384, 243),
    }),
    "bars": ((1672, 941), {
        "bar_frame": (618, 172, 1144, 297),       # 빈 막대 틀
        "bar_fill_hp": (1170, 120, 1655, 180),    # 초록(체력)
        "bar_fill_hp_mid": (1168, 190, 1654, 244),
        "bar_fill_exp": (1168, 438, 1656, 494),   # 하늘(경험치)
        "bar_fill_auto": (1168, 506, 1656, 563),  # 금색(자동 경험치)
        "bar_fill_low": (1163, 677, 1656, 735),   # 빨강(위험)
        "bar_frame_head": (5, 35, 606, 316),      # 용머리 장식이 붙은 틀(큰 자리용)
    }),
    "frame": ((1122, 1402), {
        "panel": (6, 4, 1115, 1386),              # 창 배경(9조각)
    }),
    "icons": ((1254, 1254), {
        "icon_menu": (30, 209, 281, 470),
        "icon_settings": (335, 215, 589, 462),
        "icon_hunt": (653, 196, 914, 483),
        "icon_skill": (965, 216, 1227, 467),
        "icon_auto": (35, 530, 276, 757),
        "icon_tower": (342, 493, 581, 757),
        "icon_codex": (635, 538, 912, 757),
        "icon_exit": (983, 501, 1217, 752),
        "icon_orb": (26, 818, 298, 1058),
        "icon_arrow": (379, 831, 551, 1070),
        "icon_toggle_on": (614, 859, 911, 1013),
        "icon_toggle_off": (938, 859, 1229, 1013),
    }),
}


def trim(img: Image.Image) -> Image.Image:
    """투명한 바깥 여백을 걷어낸다."""
    alpha = img.getchannel("A").point(lambda a: 255 if a > ALPHA_MIN else 0)
    box = alpha.getbbox()
    return img.crop(box) if box else img


def slice_sheet(path: Path, kind: str, out: Path):
    base, pieces = SHEETS[kind]
    img = Image.open(path).convert("RGBA")
    sx = img.size[0] / base[0]
    sy = img.size[1] / base[1]
    if abs(sx - 1) > 0.01 or abs(sy - 1) > 0.01:
        print(f"[알림] {path.name}: 기준 크기 {base} 와 달라 좌표를 {sx:.3f}x{sy:.3f} 로 환산합니다.")
    made = 0
    for name, (x1, y1, x2, y2) in pieces.items():
        box = (round(x1 * sx), round(y1 * sy), round(x2 * sx), round(y2 * sy))
        piece = trim(img.crop(box))
        if piece.size[0] < 4 or piece.size[1] < 4:
            print(f"[경고] {name}: 잘라낸 결과가 비어 있습니다. 좌표를 확인하세요.")
            continue
        piece.save(out / f"{name}.png")
        made += 1
        print(f"  {name}.png  {piece.size[0]}x{piece.size[1]}")
    return made


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", required=True, help="스킨 폴더 (예: assets/ui/default)")
    for k in SHEETS:
        p.add_argument(f"--{k}", help=f"{k} 원본 PNG")
    args = p.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    total = 0
    for k in SHEETS:
        src = getattr(args, k)
        if not src:
            continue
        print(f"[{k}] {src}")
        total += slice_sheet(Path(src), k, out)
    if total == 0:
        sys.exit("자른 것이 없습니다. --icons / --panels / --bars / --frame 중 하나는 주세요.")
    print(f"완료: {out} 에 {total}개")


if __name__ == "__main__":
    main()
