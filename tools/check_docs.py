#!/usr/bin/env python3
"""문서 구조 검사.

문서를 한 곳에만 적는 규칙은 사람이 지켜야 하는데, 실제로 안 지켜졌다 — 목록이
README·CLAUDE.md·AGENTS.md 세 곳에 갈라져 서로 달라졌고, 문서를 추가할 때마다 한 곳씩 빠졌다.
그래서 기계가 잡을 수 있는 두 가지만 검사한다.

1. 깨진 상대 링크 — 문서를 옮기고 링크를 안 고친 것
2. docs/ 의 문서가 docs/INDEX.md 에 등재되지 않은 것 — 색인에서 빠져 아무도 못 찾는 문서

실행: python3 tools/check_docs.py
"""
from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parent.parent
LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")

# INDEX 등재 검사에서 빼는 파일
INDEX_EXEMPT = {"INDEX.md", "STATE.md"}


def md_files() -> list[Path]:
    """검사 대상 문서. 복구한 원본 보존 문서는 고치지 않으므로 뺀다."""
    out = []
    for p in ROOT.rglob("*.md"):
        rel = p.relative_to(ROOT)
        parts = rel.parts
        if ".git" in parts or parts[0:1] == ("build",):
            continue
        # assets/references/**/recovered-*/ 는 잘못 업로드된 저장소에서 복구한 원본이다.
        # 원본 그대로 두는 것이 목적이므로 링크가 깨져 있어도 고치지 않는다.
        if any(d.startswith("recovered-") for d in parts):
            continue
        out.append(p)
    return sorted(out)


def check_links(files: list[Path]) -> list[str]:
    problems = []
    for f in files:
        for target in LINK.findall(f.read_text(encoding="utf-8")):
            target = target.strip()
            if target.startswith(("http://", "https://", "#", "mailto:")):
                continue
            path_part = unquote(target.split("#", 1)[0])
            if not path_part:
                continue
            resolved = (f.parent / path_part).resolve()
            if not resolved.exists():
                rel = f.relative_to(ROOT)
                problems.append(f"{rel}: 깨진 링크 → {target}")
    return problems


def check_index(files: list[Path]) -> list[str]:
    index = ROOT / "docs" / "INDEX.md"
    if not index.exists():
        return ["docs/INDEX.md 가 없다 — 문서 색인은 이 파일 하나다"]
    text = index.read_text(encoding="utf-8")
    problems = []
    for f in files:
        rel = f.relative_to(ROOT)
        if rel.parts[0] != "docs" or "archive" in rel.parts:
            continue
        if rel.name in INDEX_EXEMPT:
            continue
        # INDEX.md 기준 상대 경로로 등장하는지 본다
        needle = str(rel.relative_to("docs")).replace("\\", "/")
        if needle not in text:
            problems.append(f"{rel}: docs/INDEX.md 에 등재되지 않았다")
    return problems


def main() -> int:
    files = md_files()
    problems = check_links(files) + check_index(files)
    if problems:
        print(f"문서 검사 실패 — {len(problems)}건\n")
        for p in problems:
            print(f"  - {p}")
        print("\n고치는 방법: 문서를 옮겼으면 링크를 같은 PR 에서 고치고,")
        print("새 문서를 만들었으면 docs/INDEX.md 의 목록과 라우팅 표에 추가한다.")
        return 1
    print(f"문서 검사 통과 — {len(files)}개 파일")
    return 0


if __name__ == "__main__":
    sys.exit(main())
