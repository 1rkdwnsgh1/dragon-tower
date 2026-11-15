#!/usr/bin/env bash
# 코퍼스가 커지는 것을 막는다. CI 가 부른다.
#
# **왜 스크립트까지 필요한가**: README 에 "작업 폴더를 따로 주라"고 적는 것만으로는 막히지 않았다.
# 2026-11-14에 754개 3.3MB, 11-15에 443개 2MB 가 두 번 들어갔다. 둘 다 libFuzzer 가
# 저장소 폴더에 쓴 입력을 `git add -A` 가 담은 것이다. 글로 적은 규칙은 바쁠 때 안 읽힌다.
#
# 한도는 넉넉하다 — 씨앗·회귀 입력만 두면 한참 밑이다. 걸리면 거의 확실히 사고다.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."  # 저장소 루트

MAX_FILES=60
MAX_KB=512

files=$(find tests/fuzz/corpus -type f | wc -l)
kb=$(du -sk tests/fuzz/corpus | cut -f1)

printf '코퍼스: 파일 %d개 (한도 %d), %d KB (한도 %d KB)\n' "$files" "$MAX_FILES" "$kb" "$MAX_KB"

fail=0
if [ "$files" -gt "$MAX_FILES" ]; then echo "파일 수가 한도를 넘었다." >&2; fail=1; fi
if [ "$kb" -gt "$MAX_KB" ]; then echo "용량이 한도를 넘었다." >&2; fail=1; fi

if [ "$fail" -ne 0 ]; then
    cat >&2 <<'MSG'

퍼저를 저장소 코퍼스 폴더에 대고 돌렸을 가능성이 높다. libFuzzer 는 인수로 준 폴더에
새로 찾은 입력을 계속 써 넣는다. tests/fuzz/README.md 의 "작업 폴더를 따로 준다"를 볼 것.

고치는 법: 씨앗(seed-*)·손으로 쓴 것(hand-*)·회귀 입력(regression-*)만 남기고 지운다.
자란 코퍼스가 정말 필요하면 저장소가 아니라 Actions 캐시에 둔다.
MSG
    echo "" >&2
    echo "가장 큰 폴더:" >&2
    du -sk tests/fuzz/corpus/* | sort -rn | head -5 >&2
    exit 1
fi
echo "좋다."
