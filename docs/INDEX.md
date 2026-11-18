# 문서 색인과 읽기 경로

**이 저장소의 문서 목록은 여기 하나다.** `README.md` · `CLAUDE.md` 에는 목록을 두지 않는다 —
세 곳에 나눠 두었더니 목록이 서로 달라져서 문서를 추가할 때마다 한 곳씩 빠졌다.

## 읽는 방법

1. [STATE.md](STATE.md) — 지금 무슨 상태인가, 무엇이 막혀 있나
2. 이 문서의 라우팅 표에서 **지금 할 일에 해당하는 줄만** 읽는다
3. `archive/` 는 **읽지 않는다** (아래 참고)

전부 읽지 않는다. 한 작업에 필요한 문서는 보통 2~3개다.

## 작업별 라우팅

| 할 일 | 읽을 것 |
|---|---|
| **코어 규칙 수정** (성장·전투·탑·저장) | `CLAUDE.md` 핵심 규칙 → [codemap.md](codemap.md) 해당 줄 → [game-design.md](game-design.md) 관련 장 |
| **Windows 쪽 수정** (그리기·입력·오버레이) | [codemap.md](codemap.md) → [quality-tools.md](quality-tools.md) 1장(푸시 전 컴파일) |
| **드래곤 추가·수치 변경** | [../assets/dragons/README.md](../assets/dragons/README.md) → [character-pipeline.md](character-pipeline.md) 3·4장 |
| **적 추가·변종** | [../assets/enemies/README.md](../assets/enemies/README.md) |
| **스프라이트 시트·스킬 이펙트** | [character-pipeline.md](character-pipeline.md) 4장 → [quality-tools.md](quality-tools.md) 5장(`check_sheets.py`) |
| **탑 층 구성** | [../assets/tower/README.md](../assets/tower/README.md) → [tower-100-design.md](tower-100-design.md) |
| **탑 보스 기획** | [tower-100-bosses.md](tower-100-bosses.md) |
| **UI 스킨** | [../assets/ui/README.md](../assets/ui/README.md) |
| **밸런스 수치 조정** | [implementation-notes.md](implementation-notes.md) 임시값·시뮬레이션 참고값 |
| **테스트·정적 분석·퍼저** | [quality-tools.md](quality-tools.md) |
| **릴리스·버전 올리기** | [STATE.md](STATE.md) → [changelog.md](changelog.md) |
| **결정이 이미 있는지 확인** | [decisions.md](decisions.md) ← **작업 시작 전에 본다** |
| **버그 고치기** | [known-issues.md](known-issues.md) → [codemap.md](codemap.md) 해당 줄 |
| **망령룡 작업** | [characters/revenant-dragon.md](characters/revenant-dragon.md) → [characters/revenant-dragon-progress.md](characters/revenant-dragon-progress.md) |
| **망령룡 스킬 그림 고르기** | [characters/revenant-dragon-skill-concepts.md](characters/revenant-dragon-skill-concepts.md) |
| **변신폼·궁극기 기획 결정** | [characters/revenant-dragon-transform-concepts.md](characters/revenant-dragon-transform-concepts.md) |
| **토큰·왕복 줄이기** | [token-budget.md](token-budget.md) |
| **팀원에게 AI 지침 전달** | [team-ai-prompt.md](team-ai-prompt.md) |

## 전체 문서

### 매번 읽는 것 (2개)
- [STATE.md](STATE.md) — 현재 상태, 막힌 것, 순서 제약
- INDEX.md — 이 문서

### 기준 문서 (해당 작업에서만)
- [game-design.md](game-design.md) — 통합 기획서. **규칙의 유일한 기준**
- [decisions.md](decisions.md) — 사람이 정한 것의 색인. 설계 방향 결정은 여기가 원본
- [codemap.md](codemap.md) — 무엇이 어디에 있는지 적은 표
- [roadmap.md](roadmap.md) — 개발 단계와 체크포인트
- [character-pipeline.md](character-pipeline.md) — 캐릭터 이미지·애니메이션·스킬 적용 규격
- [tower-100-design.md](tower-100-design.md) · [tower-100-bosses.md](tower-100-bosses.md) — 탑 100층 설계·보스
- [quality-tools.md](quality-tools.md) — 빌드 확인·새니타이저·퍼저·정적 분석·시트 검사
- [implementation-notes.md](implementation-notes.md) — 임시값과 시뮬레이션 참고값
- [windows-checklist.md](windows-checklist.md) — 사람이 Windows 에서 확인할 것
- [known-issues.md](known-issues.md) — 돌려 보고 나온 문제와 아직 안 고친 이유
- [changelog.md](changelog.md) — 버전별 바뀐 점 (최근분만)
- [token-budget.md](token-budget.md) — 왕복·토큰 줄이는 방법 중 효과가 확인된 것
- [ai-workflow-notes.md](ai-workflow-notes.md) — 이 환경에서 안 되는 것, 비용 기록
- [team-ai-prompt.md](team-ai-prompt.md) — 팀원 AI 에 붙여넣는 요약
- 드래곤별 기획: [characters/revenant-dragon.md](characters/revenant-dragon.md) (망령룡 외형·제작 방향) ·
  [characters/revenant-dragon-skills-v1.md](characters/revenant-dragon-skills-v1.md) (스킬 V1, 승인 대기) ·
  [characters/revenant-dragon-progress.md](characters/revenant-dragon-progress.md) (제작 진행) ·
  [characters/revenant-dragon-skill-concepts.md](characters/revenant-dragon-skill-concepts.md)
  (추가 스킬 콘셉트 60종 목록과 시각 선호 — **제작 승인·스킬 확정 아님**) ·
  [characters/revenant-dragon-transform-concepts.md](characters/revenant-dragon-transform-concepts.md)
  (변신폼·궁극기 시안 4종과 구현 방식 비교 — **제안, 승인 아님**)
- 에셋 사용법: [dragons](../assets/dragons/README.md) · [enemies](../assets/enemies/README.md) · [tower](../assets/tower/README.md) · [ui](../assets/ui/README.md) · [전체 규격](../assets/README.md)

### archive/ — 읽지 않는다
끝난 작업 큐, 결정이 끝난 심의 문서, 옛 변경 기록이 들어 있다. **사람이 명시적으로 지시할 때만**
읽는다. 결정 내용은 전부 [decisions.md](decisions.md) 로 옮겼으므로 평시에 여기를 볼 일은 없다.
`grep` 할 때도 `--exclude-dir=archive` 를 붙인다.

## 문서를 새로 만들 때

1. **이 문서의 "전체 문서"와 라우팅 표에 추가한다.** 다른 곳에는 목록을 만들지 않는다.
2. 같은 결정·규칙을 두 곳에 적지 않는다. 필요하면 한 줄 요약 + 링크.
3. 결정이 끝난 심의 문서는 결정만 [decisions.md](decisions.md) 로 옮기고 본문은 `archive/` 로 내린다.
