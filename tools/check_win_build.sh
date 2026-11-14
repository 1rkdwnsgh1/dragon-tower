#!/usr/bin/env bash
# src/win 을 **푸시 전에 이 환경에서** 컴파일해 본다.
#
# 왜 이게 필요한가
# ----------------
# v0.6 에서 Windows 코드의 실수 셋(헤더 누락 / RectF 모호성 / 정의 순서)을 전부 CI 실패로 알았다.
# CI 왕복은 몇 분이고 비싸다. docs/ai-workflow-notes.md 에는 "이 환경에서는 Windows 코드를
# 컴파일할 수 없고 mingw 크로스 컴파일러도 설치되지 않는다"고 적혀 있었지만 **그건 사실이 아니었다.**
# `apt-get update` 를 먼저 하지 않아 패키지를 못 찾은 것이었다. mingw-w64 로 GDI+ 까지 포함해
# 링크가 되고, 20초쯤에 실행 파일이 나온다.
#
# 쓰는 법
# -------
#   tools/check_win_build.sh            # 문법 검사만 (빠름, 5초쯤)
#   tools/check_win_build.sh --link     # 링크까지. dist/win-local/dragon_tower.exe 가 나온다
#
# 없으면 설치:
#   sudo apt-get update && sudo apt-get install -y mingw-w64
#
# 한계 (**반드시 알고 쓸 것**)
# ---------------------------
# * 이건 MSVC 가 아니라 GCC 다. 둘이 보는 것이 완전히 같지 않다.
#   - GCC 가 더 엄격한 곳: 템플릿 이름 해석(two-phase lookup), 정의 순서. v0.6 의 실수 셋은
#     전부 이쪽이라 여기서 잡힌다.
#   - MSVC 만 잡는 곳: MSVC 전용 pragma, /permissive- 의 일부 판단, MSVC 런타임 차이.
#   그래서 **CI 를 대신하지 않는다.** CI 왕복 횟수를 줄이는 것이 목적이다.
# * 여기서 나온 .exe 는 mingw 로 만든 것이라 배포본이 아니다. 사람이 눈으로 확인할 때는
#   되도록 CI 가 만든 DragonTower-windows 를 쓴다. 급할 때 임시로 쓰는 용도다.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CXX=x86_64-w64-mingw32-g++
if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "오류: $CXX 가 없다." >&2
    echo "  sudo apt-get update && sudo apt-get install -y mingw-w64" >&2
    echo "  (apt-get update 를 빼먹으면 '패키지를 찾을 수 없다'고 나온다 — 그게 예전에 '설치 불가'로" >&2
    echo "   잘못 기록된 이유다.)" >&2
    exit 127
fi

# CMakeLists.txt 의 Windows 타깃과 같은 정의를 쓴다. 여기를 바꾸면 거기도 바꾼다.
DEFS=(-DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601)
INCS=(-Iinclude -Isrc/win)
WARN=(-Wall -Wextra)
LIBS=(-lgdiplus -lshell32 -lole32 -luser32 -lgdi32 -luuid)

if [[ "${1:-}" == "--link" ]]; then
    OUT="dist/win-local"
    mkdir -p "$OUT"
    echo "링크까지 빌드한다 → $OUT/dragon_tower.exe"
    # -municode 는 쓰지 않는다. 진입점이 WinMain(wWinMain 아님)이라 쓰면 링크가 깨진다.
    "$CXX" -std=c++17 -O2 -mwindows -static -static-libgcc -static-libstdc++ \
        "${WARN[@]}" "${INCS[@]}" "${DEFS[@]}" \
        src/win/*.cpp src/core/*.cpp \
        -o "$OUT/dragon_tower.exe" "${LIBS[@]}"
    echo "됐다: $(ls -la "$OUT/dragon_tower.exe" | awk '{print $5" 바이트"}')"
    echo "실행하려면 assets 폴더를 .exe 옆에 둔다."
else
    echo "문법 검사만 한다 (링크는 --link)"
    "$CXX" -std=c++17 -fsyntax-only "${WARN[@]}" "${INCS[@]}" "${DEFS[@]}" src/win/*.cpp
    echo "src/win 문법 검사 통과."
fi
