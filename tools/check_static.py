#!/usr/bin/env python3
"""정적 분석을 돌리고 **새로 생긴 지적만** 실패시킨다.

왜 기준선(baseline)인가
-----------------------
처음 돌리면 지적이 수십 개 나온다. 전부 고치지 않으면 CI 가 영원히 빨간색이고,
빨간 CI 는 아무도 보지 않는다. 그래서 "지금 있는 것"을 기준선으로 적어 두고
**새로 생긴 것만** 막는다. 기준선은 줄어들기만 해야 한다.

지적을 `파일:검사이름` 으로만 적는다(줄 번호를 빼면 코드를 조금 옮겨도 기준선이 안 깨진다).
같은 파일에 같은 검사가 몇 건이든 한 줄로 센다 — 개수가 늘면 그것도 새 지적으로 본다.

쓰는 법
-------
    python3 tools/check_static.py --build-dir build            # 검사 (CI 가 이걸 쓴다)
    python3 tools/check_static.py --build-dir build --update   # 기준선 갱신 (고친 뒤에)

build-dir 에는 compile_commands.json 이 있어야 한다:
    cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tools" / "static-baseline.txt"

# 검사할 것. src/win 은 Windows 전용이라 compile_commands.json 에 없다 —
# mingw 플래그를 직접 줘서 따로 돈다(tools/check_win_build.sh 와 같은 플래그).
CORE_SOURCES = ["src/core", "tools/asset_check.cpp", "tests/core_tests.cpp", "tests/balance_sim.cpp"]
WIN_SOURCES = ["src/win"]

# style 은 **일부러 뺐다.** cppcheck 버전이 조금만 달라도 style 검사의 결과가 달라져서
# 기준선이 깨진다(실제로 이 환경 2.13 과 CI 러너의 버전이 달라 4건이 어긋났다).
# 게다가 style 지적은 전부 "const 를 붙여라" 류라 버그가 아니다.
# warning/performance/portability 는 버전 사이에서 훨씬 안정적이다.
CPPCHECK_ARGS = [
    "--enable=warning,performance,portability",
    "--inline-suppr",
    "--std=c++17",
    "-Iinclude",
    "-Isrc/win",
    "--suppress=missingIncludeSystem",
    "--suppress=unusedFunction",      # 헤더에만 쓰이는 함수가 전부 걸린다
    "--suppress=useStlAlgorithm",     # 게임 루프를 알고리즘으로 바꾸라는 권고. 버그가 아니다
    "-q",
    "--template={file}:{id}",
]


def expand(patterns):
    out = []
    for p in patterns:
        path = ROOT / p
        if path.is_dir():
            out += sorted(str(f.relative_to(ROOT)) for f in path.glob("*.cpp"))
        elif path.exists():
            out.append(p)
    return out


def rel(path: str) -> str:
    """절대 경로를 저장소 기준 상대 경로로."""
    try:
        return str(Path(path).resolve().relative_to(ROOT))
    except (ValueError, OSError):
        return path


def run_cppcheck(findings: Counter) -> None:
    if not shutil.which("cppcheck"):
        print("  cppcheck 없음 — 건너뜀 (apt-get install cppcheck)", file=sys.stderr)
        return
    files = expand(CORE_SOURCES + WIN_SOURCES)
    p = subprocess.run(["cppcheck", *CPPCHECK_ARGS, *files],
                       cwd=ROOT, capture_output=True, text=True)
    for line in p.stderr.splitlines():
        line = line.strip()
        if not line or ":" not in line:
            continue
        f, _, check = line.rpartition(":")
        if not check or " " in check:
            continue
        findings[f"{rel(f)}:cppcheck/{check}"] += 1


def run_clang_tidy(build_dir: Path, findings: Counter) -> None:
    if not shutil.which("clang-tidy"):
        print("  clang-tidy 없음 — 건너뜀", file=sys.stderr)
        return
    cc = build_dir / "compile_commands.json"
    if not cc.exists():
        print(f"  {cc} 없음 — clang-tidy 건너뜀 "
              f"(cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)", file=sys.stderr)
        return
    # 검사 이름에 대문자가 들어가는 것이 있다 (clang-analyzer-deadcode.DeadStores).
    # 소문자만 받으면 clang-analyzer 계열을 통째로 놓친다 — 그게 가장 중요한 묶음이다.
    pat = re.compile(r"^(.+?):\d+:\d+:\s+(?:warning|error):\s+.*\[([A-Za-z0-9.,_-]+)\]\s*$")
    files = expand(CORE_SOURCES)
    p = subprocess.run(["clang-tidy", "-p", str(build_dir), "--quiet", *files],
                       cwd=ROOT, capture_output=True, text=True)
    for line in p.stdout.splitlines():
        m = pat.match(line.strip())
        if not m:
            continue
        f, checks = m.group(1), m.group(2)
        # 한 줄에 검사 이름이 여럿 붙는 경우가 있다(별칭). 첫 번째만 센다.
        findings[f"{rel(f)}:clang-tidy/{checks.split(',')[0]}"] += 1


def load_baseline() -> Counter:
    c = Counter()
    if not BASELINE.exists():
        return c
    for line in BASELINE.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        sig, _, n = line.rpartition(" ")
        if sig and n.isdigit():
            c[sig] = int(n)
    return c


def save_baseline(findings: Counter) -> None:
    lines = [
        "# 정적 분석 기준선. tools/check_static.py 가 읽는다.",
        "# 형식: <파일>:<도구>/<검사이름> <건수>",
        "#",
        "# 이 목록은 **줄어들기만 해야 한다.** 새 지적이 생기면 CI 가 막는다.",
        "# 고친 뒤에는 `python3 tools/check_static.py --build-dir build --update` 로 갱신한다.",
        "",
    ]
    for sig in sorted(findings):
        lines.append(f"{sig} {findings[sig]}")
    BASELINE.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--update", action="store_true", help="기준선을 지금 상태로 갱신한다")
    args = ap.parse_args()

    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = ROOT / build_dir

    findings: Counter = Counter()
    print("cppcheck …")
    run_cppcheck(findings)
    print("clang-tidy …")
    run_clang_tidy(build_dir, findings)
    print(f"지적 {sum(findings.values())}건 / 종류 {len(findings)}가지")

    if args.update:
        save_baseline(findings)
        print(f"기준선을 갱신했다: {BASELINE.relative_to(ROOT)}")
        return 0

    base = load_baseline()
    new = {s: n for s, n in findings.items() if n > base.get(s, 0)}
    gone = {s: n for s, n in base.items() if n > findings.get(s, 0)}

    if gone:
        print("\n줄어든 지적 (기준선을 갱신하면 좋다):")
        for s in sorted(gone):
            print(f"  - {s}: {base[s]} -> {findings.get(s, 0)}")

    if new:
        print("\n새로 생긴 지적 — 이것 때문에 실패한다:")
        for s in sorted(new):
            print(f"  + {s}: {base.get(s, 0)} -> {new[s]}")
        print("\n고치거나, 정말 괜찮다면 그 줄 위에 이유를 적고")
        print("  // NOLINTNEXTLINE(검사이름)  또는  // cppcheck-suppress 검사이름")
        print("를 붙인 뒤 --update 로 기준선을 갱신한다.")
        return 1

    print("새 지적 없음.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
