#!/usr/bin/env python3
"""문서 구조 검사.

문서 규칙은 사람이 지켜야 하는데 실제로 안 지켜졌다 — 목록이 README·CLAUDE.md·AGENTS.md 세 곳에
갈라져 서로 달라졌고, 문서를 옮기면 링크가 깨진 채 남았다. 그래서 기계가 잡을 수 있는 것만 검사한다.

1. 깨진 상대 링크 — 문서를 옮기고 링크를 안 고친 것
2. docs/ 의 문서가 docs/INDEX.md 에 등재되지 않은 것 — 색인에서 빠져 아무도 못 찾는 문서
3. archive 역류 — 평시에 읽는 문서가 docs/archive/ 를 가리키는 것
   (archive 를 읽지 않기로 했는데 살아 있는 문서가 링크로 끌어오면 규칙이 무의미해진다)
4. STATE.md 갱신 — 코드·문서를 고치는 PR 이 docs/STATE.md 를 함께 고쳤는지 (--pr-base 를 줄 때만)
5. codemap.md 갱신 — src/·include/ 를 고치는 PR 이 docs/codemap.md 를 함께 고쳤는지 (같음)

실행:
    python3 tools/check_docs.py                      # 1~3
    python3 tools/check_docs.py --pr-base origin/main  # 1~5 (CI 의 PR 검사)

검사를 건너뛰어야 하는 PR 은 커밋 메시지에 trailer 를 넣는다 — 줄 머리에 와야 한다.
    State-Skip: <이유>     (에셋만 바꿈, 오타 수정 등 상태가 그대로일 때)
    Codemap-Skip: <이유>   (코드를 고쳤지만 무엇이 어디에 있는지는 안 바뀔 때)
"""
from __future__ import annotations

import argparse
import datetime as dt
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parent.parent
LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")

# INDEX 등재 검사에서 빼는 파일
INDEX_EXEMPT = {"INDEX.md", "STATE.md"}

# archive 를 가리켜도 되는 문서. 보관 이유와 원본 위치를 적는 것이 이들의 역할이다.
ARCHIVE_LINK_OK = {
    "docs/INDEX.md",
    "docs/decisions.md",
    "docs/token-budget.md",
    "docs/archive/README.md",
    "docs/ai-workflow-notes.md",
    "docs/implementation-notes.md",
    "docs/changelog.md",
}

# STATE.md 가 이 일수보다 오래 묵으면 실패시킨다.
STATE_STALE_DAYS = 14


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


def check_archive_backflow(files: list[Path]) -> list[str]:
    """평시에 읽는 문서가 archive 를 가리키면 "읽지 않는다"가 무의미해진다."""
    problems = []
    for f in files:
        rel = f.relative_to(ROOT).as_posix()
        if rel in ARCHIVE_LINK_OK or "archive" in f.relative_to(ROOT).parts:
            continue
        for target in LINK.findall(f.read_text(encoding="utf-8")):
            path_part = unquote(target.strip().split("#", 1)[0])
            if not path_part or path_part.startswith(("http", "mailto:")):
                continue
            resolved = (f.parent / path_part).resolve()
            try:
                inside = resolved.relative_to(ROOT / "docs" / "archive")
            except ValueError:
                continue
            problems.append(
                f"{rel}: archive 를 가리킨다 → {target}\n"
                f"      archive 는 평시에 읽지 않는다. 결정은 docs/decisions.md 를,"
                f" 상태는 docs/STATE.md 를 가리켜라 (대상: {inside})"
            )
    return problems


def _git(*args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=ROOT, capture_output=True, text=True, check=False
    ).stdout


def _pr_changes(pr_base: str, what: str) -> tuple[list[str], str] | None:
    """base 와의 차이에서 (바뀐 파일 목록, 커밋 메시지 전문) 을 돌려준다.

    얕은 체크아웃이면 공통 조상을 못 찾는다. 이것은 규칙 위반이 아니라 환경 문제이므로
    막지 않고 경고만 하고 None 을 돌려준다 — 환경 문제로 PR 을 막으면 검사 자체를 지우게 된다.
    """
    merge_base = _git("merge-base", pr_base, "HEAD").strip()
    if not merge_base:
        print(
            f"경고: {pr_base} 와의 공통 조상을 찾지 못해 {what} 검사를 건너뛴다.\n"
            "      CI 라면 체크아웃의 fetch-depth 를 늘린다 (현재 100).",
            file=sys.stderr,
        )
        return None
    changed = [p for p in _git("diff", "--name-only", f"{merge_base}..HEAD").split("\n") if p]
    return changed, _git("log", "--format=%B", f"{merge_base}..HEAD")


def check_codemap_updated(pr_base: str) -> list[str]:
    """코드를 고치는 PR 은 docs/codemap.md 를 함께 고쳐야 한다.

    CLAUDE.md 에 "코드를 바꾸면 이 표도 같은 PR 에서 고친다"고 적혀 있지만 기계가 잡지 않아서
    낡을 수 있었다. 코드 지도가 낡으면 다음 세션이 위치를 찾으려고 파일을 넓게 읽게 되고,
    그게 바로 줄이려던 비용이다. STATE.md 검사와 같은 구조다.
    """
    pr = _pr_changes(pr_base, "codemap 갱신")
    if pr is None:
        return []
    changed, messages = pr

    skip = re.search(r"^Codemap-Skip:[ \t]*(\S.*)$", messages, re.MULTILINE)
    if skip:
        print(f"codemap 검사: 건너뛴다 — {skip.group(1).strip()}")
        return []
    if "docs/codemap.md" in changed:
        return []

    code = [p for p in changed if p.startswith(("src/", "include/"))]
    if not code:
        return []

    return [
        "docs/codemap.md 를 고치지 않았다 — 이 PR 은 코드 "
        f"{len(code)}개 파일을 바꾼다 (예: {', '.join(code[:3])})\n"
        "      무엇이 어디에 있는지 바뀌었으면 그 줄을 고친다. 지도가 낡으면 다음 작업이\n"
        "      위치를 찾으려고 파일을 넓게 읽게 된다.\n"
        "      지도에 영향이 없는 변경이라면 커밋 메시지에 `Codemap-Skip: <이유>` 줄을 넣는다."
    ]


def check_state_updated(pr_base: str) -> list[str]:
    """코드·문서를 고치는 PR 은 docs/STATE.md 를 함께 고쳐야 한다.

    낡은 상태 문서는 없는 것보다 해롭다. 규칙만 적어 두면 지켜지지 않으므로 검사로 만들었다.
    """
    pr = _pr_changes(pr_base, "STATE 갱신")
    if pr is None:
        return []
    changed, messages = pr

    # 탈출구는 git trailer 형식의 독립된 줄이어야 한다. 처음에는 본문 어디든 `[state-skip]` 이
    # 있으면 건너뛰게 했는데, **탈출구를 설명하는 커밋 메시지가 스스로 검사를 껐다.**
    # 줄 머리의 `State-Skip:` + 이유로 바꿔서 산문에 우연히 걸리지 않게 했고, 이유를 적게 만들었다.
    skip = re.search(r"^State-Skip:[ \t]*(\S.*)$", messages, re.MULTILINE)
    if skip:
        print(f"STATE 검사: 건너뛴다 — {skip.group(1).strip()}")
        return []
    if "docs/STATE.md" in changed:
        return []

    # 상태가 바뀌는 변경인지 — 코드와 살아 있는 문서만 본다.
    def matters(p: str) -> bool:
        if p.startswith("docs/archive/"):
            return False
        return (
            p.startswith(("src/", "include/", "docs/", "tools/", ".github/workflows/"))
            or p in {"CLAUDE.md", "README.md", "CMakeLists.txt"}
        )

    relevant = [p for p in changed if matters(p)]
    if not relevant:
        return []

    return [
        "docs/STATE.md 를 고치지 않았다 — 이 PR 은 "
        f"{len(relevant)}개 파일을 바꾼다 (예: {', '.join(relevant[:3])})\n"
        "      버전·CI·열린 PR·막혀 있는 것·순서 제약 중 바뀐 것을 반영하고 '최종 갱신' 날짜를 고친다.\n"
        "      상태가 정말 그대로라면 커밋 메시지에 `State-Skip: <이유>` 줄을 넣는다."
    ]


def check_state_fresh() -> list[str]:
    state = ROOT / "docs" / "STATE.md"
    if not state.exists():
        return ["docs/STATE.md 가 없다 — 세션 시작 때 읽는 문서다"]
    m = re.search(r"최종 갱신:\s*(\d{4})-(\d{2})-(\d{2})", state.read_text(encoding="utf-8"))
    if not m:
        return ["docs/STATE.md 에 `최종 갱신: YYYY-MM-DD` 줄이 없다"]
    updated = dt.date(*(int(g) for g in m.groups()))
    age = (dt.date.today() - updated).days
    if age > STATE_STALE_DAYS:
        return [
            f"docs/STATE.md 가 {age}일 묵었다 (기준 {STATE_STALE_DAYS}일) — "
            "낡은 상태 문서는 없는 것보다 해롭다. 지금 상태를 확인하고 날짜를 고친다"
        ]
    return []


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--pr-base",
        help="이 ref 와의 차이를 보고 docs/STATE.md 갱신 여부를 검사한다 (예: origin/main)",
    )
    args = ap.parse_args()

    files = md_files()
    problems = (
        check_links(files)
        + check_index(files)
        + check_archive_backflow(files)
        + check_state_fresh()
    )
    if args.pr_base:
        problems += check_state_updated(args.pr_base)
        problems += check_codemap_updated(args.pr_base)
    if problems:
        print(f"문서 검사 실패 — {len(problems)}건\n")
        for p in problems:
            print(f"  - {p}")
        print("\n고치는 방법: 문서를 옮겼으면 링크를 같은 PR 에서 고치고,")
        print("새 문서를 만들었으면 docs/INDEX.md 의 목록과 라우팅 표에 추가한다.")
        print("규칙은 CLAUDE.md 의 '문서를 쓸 때' 절에 있다.")
        return 1
    print(f"문서 검사 통과 — {len(files)}개 파일")
    return 0


if __name__ == "__main__":
    sys.exit(main())
