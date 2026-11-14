#!/usr/bin/env python3
"""스프라이트 시트를 **숫자로 재고 그림으로 보여 준다.**

왜 이게 필요한가
----------------
이 저장소에서 가장 많이 되돌아간 작업이 시트 변환이다. 기록에 남은 것만:

* v0.3 — 상태마다 다른 배율을 써서 비행이 대기의 73% 크기가 됐다.
* v0.3 — 원본 캔버스 기준을 믿어 배율이 목표의 37%까지 떨어졌다.
* v0.4 — 2048x1024 시트를 128칸으로 잘못 봐서 엉뚱한 칸을 잘랐다.
         "출력이 나왔으니 됐다"고 넘어갈 뻔했다.
* v1.4 — ranged_sheet.png 가 1774x887 이라 444 칸으로 나뉘지 않았다.

전부 같은 교훈으로 끝난다: **성공 메시지만으로는 모른다. 숫자로 재고 눈으로 확인해야 한다.**
make_sheet.py 는 만드는 쪽이고, 이 도구는 **이미 들어간 시트가 맞는지 보는 쪽**이다.

하는 일
-------
1. 모든 시트에 대해 `너비 % 칸너비 == 0` 을 확인한다 (v0.4·v1.4 가 밟은 함정).
   칸 너비는 dragon.txt/enemy.txt 의 `frame_w` 를 쓰고, 없으면 높이(정사각형)로 본다.
2. 상태마다 **내용의 높이**를 재서 칸 높이에 대한 비율(%)을 낸다.
   기준 상태(기본 idle)와 비교해 차이가 크면 경고한다 (v0.3 의 공통 배율 문제).
   `render_scale` 이 적혀 있으면 그것을 곱한 뒤의 값도 함께 보여 준다 —
   그려지는 실제 크기는 그쪽이다.
쓰이지 않는 스킬 시트·아이콘은 **이 도구가 보지 않는다** — tools/asset_check.cpp 가
   v1.6(PR #29)부터 표로 적는다. 같은 것을 두 도구가 말하면 중복이 된다.

3. `--out 파일.png` 를 주면 **대조표 PNG** 를 만든다. 상태별 첫 프레임을 나란히 놓고
   칸 경계와 바닥선을 그려서, 크기·바닥 정렬이 어긋난 것이 눈에 바로 보이게 한다.
   PR 본문에 붙이면 사람이 Windows 를 켜지 않고도 확인할 수 있다.

쓰는 법
-------
    python3 tools/check_sheets.py                      # assets 전체 검사
    python3 tools/check_sheets.py --only revenant      # 한 폴더만
    python3 tools/check_sheets.py --out /tmp/sheets.png
    python3 tools/check_sheets.py --tolerance 0.08     # 허용 차이(기본 10%p)

어긋난 것이 있으면 종료 코드 1. CI 에서 쓸 수 있다.
"""
import argparse
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow 가 필요하다: pip install Pillow")

ROOT = Path(__file__).resolve().parent.parent
# 게임이 실제로 읽는 상태 시트. src/core/Species.cpp 의 animStateName 과 같아야 한다.
GAME_STATES = ["idle", "move", "attack", "hit", "death", "fly", "rest"]
# 대조표에 넣을 순서.
STATE_ORDER = ["idle", "move", "fly", "rest", "attack", "hit", "death"]
# 자동으로 크기를 비교할 상태.
#
# **왜 idle·move 뿐인가**: docs/character-pipeline.md 와 v0.3 기록이 "크기 측정을 윤곽 전체로
# 하면 날개 펼침에 휘둘린다 — 같은 신체 부위로 재야 한다"로 끝나 있다. 이 도구는 알파 윤곽만
# 보므로 **누운 자세(rest·death)·날개 펼침(fly)·치명타 자세(hit·attack)를 비교하면
# 그 교훈을 그대로 다시 밟는다.** 서 있는 자세 둘만 비교하고, 나머지는 숫자와 대조표로
# 사람에게 보여 준다.
COMPARABLE_STATES = {"idle", "move"}


def parse_ini(path: Path) -> dict:
    """dragon.txt / enemy.txt 를 {구역: {키: 값}} 으로. 게임 파서와 같은 규칙(; # 주석)."""
    out: dict = {"": {}}
    section = ""
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.split(";", 1)[0].split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip()
            out.setdefault(section, {})
            continue
        if "=" in line:
            k, _, v = line.partition("=")
            out.setdefault(section, {})[k.strip()] = v.strip()
    return out


def as_float(d: dict, key: str, default):
    try:
        return float(d[key])
    except (KeyError, ValueError, TypeError):
        return default


def content_height(img: Image.Image):
    """알파가 있는 부분의 높이와 아래쪽 y. 없으면 (0, None)."""
    if img.mode != "RGBA":
        img = img.convert("RGBA")
    box = img.getchannel("A").getbbox()
    if not box:
        return 0, None
    return box[3] - box[1], box[3]


def state_of(png: Path) -> str:
    """파일 이름에서 상태 이름을 뽑는다. skill_<이름>.png 는 'skill:<이름>'."""
    n = png.stem
    if n.startswith("skill_"):
        return "skill:" + n[len("skill_"):]
    return n


def anim_for(cfg: dict, state: str) -> dict:
    """그 상태의 설정. 스킬 모션은 [skill.N] 의 motion_* 키를 쓴다."""
    if state.startswith("skill:"):
        want = state.split(":", 1)[1]
        for sec, kv in cfg.items():
            if sec.startswith("skill.") and kv.get("motion") == want:
                return {"frame_w": kv.get("motion_frame_w"),
                        "render_scale": kv.get("motion_render_scale")}
        return {}
    return cfg.get(f"anim.{state}", {})


def skill_motions(cfg: dict) -> set:
    """데이터 파일이 motion= 으로 가리키는 스킬 모션 이름들. 게임은 이것만 읽는다."""
    out = set()
    for sec, kv in cfg.items():
        if sec.startswith("skill.") and kv.get("motion"):
            out.add(kv["motion"])
    return out


def check_folder(folder: Path, data_name: str, tolerance: float,
                 problems: list, notes: list) -> list:
    """한 캐릭터 폴더를 검사하고 상태별 측정값을 돌려준다.

    problems 에는 **게임이 실제로 거부하는 것**만 넣는다(격자가 나뉘지 않는 시트).
    자세 때문에 생기는 크기 차이는 notes 로 보내 사람이 보게 한다.
    """
    cfg = parse_ini(folder / data_name) if (folder / data_name).exists() else {"": {}}
    motions = skill_motions(cfg)
    rows = []
    for png in sorted(folder.glob("*.png")):
        if png.stem.startswith("icon_") or png.stem.startswith("fx_"):
            continue  # 아이콘·효과는 칸으로 나뉘지 않는다
        state = state_of(png)
        # 게임이 읽지 않는 PNG 는 픽셀을 재 봐야 의미가 없으니 건너뛴다.
        #
        # **쓰이지 않는 스킬 시트·아이콘은 여기서 알리지 않는다.** tools/asset_check.cpp 가
        # v1.6(PR #29)부터 스킬별로 아이콘·모션·효과 시트의 유무와 대체 규칙을 표로 적는다.
        # 같은 것을 두 도구가 말하면 한쪽만 고치고 지나가게 된다 — 그쪽이 기준이다.
        if state.startswith("skill:"):
            if state.split(":", 1)[1] not in motions:
                continue
        elif state not in GAME_STATES:
            # 상태 이름도 skill_ 도 아닌 PNG. asset_check 는 스킬 관련 파일만 보므로
            # 이쪽은 여기서 알린다 (shadow/dragon.png 가 422KB 짜리 사장 파일이었다).
            notes.append(f"{folder.name}/{png.name}: 상태 시트도 스킬 모션도 아닌 이름이라 "
                         f"게임이 읽지 않는다 ({png.stat().st_size // 1024} KB). "
                         f"읽히는 상태 시트는 {'/'.join(GAME_STATES)}.png 다")
            continue
        try:
            img = Image.open(png).convert("RGBA")
        except Exception as e:  # 깨진 PNG 도 알려 줘야 한다
            problems.append(f"{folder.name}/{png.name}: 열 수 없다 ({e})")
            continue
        w, h = img.size
        a = anim_for(cfg, state)
        fw_raw = a.get("frame_w")
        try:
            fw = int(float(fw_raw)) if fw_raw else h
        except ValueError:
            fw = h
        if fw <= 0 or fw > w:
            fw = h

        # (1) 격자 나누기 — v0.4·v1.4 가 밟은 함정
        if fw <= 0 or w % fw != 0:
            problems.append(
                f"{folder.name}/{png.name}: 너비 {w} 가 칸 너비 {fw} 로 나뉘지 않는다 "
                f"({w} % {fw} = {w % fw}). 게임도 이 시트를 거부한다.")
            continue
        frames = w // fw

        # (2) 내용 높이 — 첫 칸으로 잰다(모든 칸을 재면 애니메이션 움직임에 휘둘린다)
        first = img.crop((0, 0, fw, h))
        ch, bottom = content_height(first)
        rs = as_float(a, "render_scale", 1.0)
        rows.append({
            "file": png.name, "state": state, "w": w, "h": h, "fw": fw,
            "frames": frames, "content_h": ch, "bottom": bottom,
            "ratio": (ch / h) if h else 0.0, "render_scale": rs,
            "drawn": (ch / h * rs) if h else 0.0, "path": png,
        })

    # (3) 서 있는 자세끼리만 크기 비교 — v0.3 의 공통 배율 문제.
    #     자세가 다른 상태는 윤곽 높이로 비교하면 안 된다(COMPARABLE_STATES 주석 참고).
    ref = next((r for r in rows if r["state"] == "idle"), None)
    if ref and ref["drawn"] > 0:
        for r in rows:
            if r is ref or r["state"] not in COMPARABLE_STATES:
                continue
            diff = r["drawn"] - ref["drawn"]
            if abs(diff) > tolerance:
                problems.append(
                    f"{folder.name}/{r['file']}: 서 있는 자세끼리 그려지는 크기가 다르다 — "
                    f"{r['state']} {r['drawn']:.0%} 대 idle {ref['drawn']:.0%} "
                    f"({abs(diff) * 100:.0f}%p). render_scale={r['render_scale']:.2f} 을 조정하거나 "
                    f"make_sheet.py --set 으로 같은 배율에 함께 변환할 것.")
    return rows


def contact_sheet(all_rows: dict, out: Path, cell: int = 180) -> None:
    """상태별 첫 칸을 나란히 놓고 칸 경계와 바닥선을 그린다."""
    def order(state):
        base = state.split(":", 1)[0]
        return (STATE_ORDER.index(base) if base in STATE_ORDER else len(STATE_ORDER), state)

    names = [n for n in sorted(all_rows) if all_rows[n]]
    if not names:
        return
    cols = max(len(all_rows[n]) for n in names)
    pad, label_h, header_w = 6, 16, 110
    W = header_w + cols * (cell + pad) + pad
    H = pad + len(names) * (cell + label_h + pad)
    canvas = Image.new("RGBA", (W, H), (26, 28, 34, 255))
    d = ImageDraw.Draw(canvas)

    for row_i, name in enumerate(names):
        rows = sorted(all_rows[name], key=lambda r: order(r["state"]))
        y = pad + row_i * (cell + label_h + pad)
        d.text((pad, y + cell // 2), name, fill=(220, 225, 235, 255))
        for col_i, r in enumerate(rows):
            x = header_w + col_i * (cell + pad)
            try:
                img = Image.open(r["path"]).convert("RGBA").crop((0, 0, r["fw"], r["h"]))
            except Exception:
                continue
            img = img.resize((cell, cell), Image.LANCZOS)
            canvas.alpha_composite(img, (x, y))
            # 칸 경계
            d.rectangle([x, y, x + cell - 1, y + cell - 1], outline=(70, 76, 92, 255))
            # 바닥선 — 상태끼리 어긋나면 여기서 바로 보인다
            if r["bottom"] is not None:
                by = y + int(r["bottom"] / r["h"] * cell)
                d.line([x, by, x + cell - 1, by], fill=(255, 120, 120, 200))
            d.text((x + 3, y + cell + 2),
                   f"{r['state']} {r['drawn']:.0%} x{r['frames']}",
                   fill=(180, 190, 210, 255))

    out.parent.mkdir(parents=True, exist_ok=True)
    canvas.convert("RGB").save(out)
    print(f"대조표: {out}  ({W}x{H})")
    print("  빨간 선이 내용의 바닥이다. 상태끼리 어긋나면 바닥 정렬이 틀린 것이고,")
    print("  % 숫자가 서로 다르면 render_scale 을 포함한 그려지는 크기가 다른 것이다.")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", default=str(ROOT / "assets"))
    ap.add_argument("--only", default=None, help="폴더 이름 하나만 검사")
    ap.add_argument("--tolerance", type=float, default=0.10,
                    help="상태 간 그려지는 크기 차이 허용값 (기본 0.10 = 10%%p)")
    ap.add_argument("--out", default=None, help="대조표 PNG 경로")
    ap.add_argument("--quiet", action="store_true", help="표를 찍지 않고 문제만 알린다")
    args = ap.parse_args()

    assets = Path(args.assets)
    problems: list = []
    notes: list = []
    all_rows: dict = {}

    for kind, data_name in (("dragons", "dragon.txt"), ("enemies", "enemy.txt")):
        base = assets / kind
        if not base.is_dir():
            continue
        for folder in sorted(base.iterdir()):
            if not folder.is_dir() or folder.name.startswith(("_", ".")):
                continue
            if args.only and folder.name != args.only:
                continue
            rows = check_folder(folder, data_name, args.tolerance, problems, notes)
            if rows:
                all_rows[f"{kind[:-1]}/{folder.name}"] = rows

    if not args.quiet:
        for name in sorted(all_rows):
            print(f"\n{name}")
            print(f"  {'파일':<22} {'칸':>12} {'칸수':>5} {'내용높이':>8} {'scale':>6} {'그려짐':>7}")
            for r in sorted(all_rows[name], key=lambda r: r["state"]):
                print(f"  {r['file']:<22} {str(r['fw']) + 'x' + str(r['h']):>12} "
                      f"{r['frames']:>5} {r['ratio']:>7.0%} {r['render_scale']:>6.2f} "
                      f"{r['drawn']:>6.0%}")

    if args.out:
        contact_sheet(all_rows, Path(args.out))

    print()
    if notes:
        print(f"알림 {len(notes)}건 (게임 동작과 무관. 정리하면 좋다):")
        for n in notes:
            print(f"  . {n}")
        print()
    if problems:
        print(f"문제 {len(problems)}건 — 게임이 거부하거나 크기가 어긋난다:")
        for p in problems:
            print(f"  - {p}")
        return 1
    print(f"시트 {sum(len(v) for v in all_rows.values())}개 — 문제 없음.")
    print("자세가 다른 상태(rest·death·fly·hit·attack)의 크기는 자동으로 판정하지 않는다.")
    print("위 표의 '그려짐' 값과 대조표를 사람이 본다 — 윤곽 높이로는 누운 자세·날개 펼침을 잴 수 없다.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
