# 현재 상태

**세션을 시작할 때 이 문서와 [INDEX.md](INDEX.md) 만 읽는다.** 나머지는 INDEX 의 라우팅 표를 보고
그 작업에 필요한 것만 읽는다.

작업을 끝낼 때 이 문서를 같은 PR 에서 고친다. **낡은 상태 문서는 없는 것보다 해롭다.**

최종 갱신: 2026-11-19 (망령룡 WALK24 에셋 적용 PR)

---

## 한눈에

| 항목 | 상태 |
|---|---|
| 버전 | **v1.9** (플레이 가능) |
| 저장소 | **공개(public)** — 2026-11-17 전환 |
| CI | **정상** (2026-11-17 16:10 `main` build·quality 통과) |
| 자동 작업 큐 | **없음** — 두 큐 모두 종료 (11-15 20:00, 11-17 08:00) |
| 열린 PR | **1건** — 그림자 가르기 타격 프레임·새 모션(`feat/shadow-claw-hit-frame`). Windows 확인 대기 |
| 알려진 문제 | **1건** — [known-issues.md](known-issues.md) 4번 `make_sheet.py` 가 칸을 꽉 채우는 그림에 안 맞는다(우회 있음). 1~3번은 고쳤다 ([#77](https://github.com/1rkdwnsgh1/dragon-tower/issues/77) → #82, [#78](https://github.com/1rkdwnsgh1/dragon-tower/issues/78) → #80, [#79](https://github.com/1rkdwnsgh1/dragon-tower/issues/79) → #83). **Windows 확인 59~78번과 그림자 가르기 타격 프레임 4개가 남아 있다** |

- 제작 중인 캐릭터 모션과 스킬은 [미리보기 도구](character-pipeline.md#모션과-스킬-미리보기)에서 확인할 수 있다.
- 망령룡 WALK24는 `assets/revenant-walk24` 브랜치에서 이동 시트에 적용했다(main 반영은 PR 검토 대기). 원본·이전 8프레임 보존, 검사·빌드 결과와 Windows 미검증 항목은 [제작 진행사항](characters/revenant-dragon-progress.md#walk24-2026-11-19)에 있다. 기존 작업 순서와 스킬 효과 수치 조정 대기는 유지한다.

## 운영 방식

- **main 에 직접 커밋하지 않는다.** 브랜치 → PR.
- 소스코드 수정은 **두 사람이 하루씩 번갈아** 맡는다. 소스를 맡지 않는 날에는 각자 자기 드래곤
  에셋을 만든다. 같은 날 둘이 코드를 고치면 기능이 충돌하고, 각자 AI 를 돌려 결론이 어긋난다.
- 드래곤은 **1인 1종**. 추가는 `assets/dragons/<키>/` 폴더를 만드는 것으로 끝난다(코드 수정 없음).

## 막혀 있는 것 — 사람이 정해야 진행된다

내용은 원본에만 적는다. 여기는 **무엇이 누구를 기다리는지**만 적는다.

| 기다리는 것 | 누가 | 원본 |
|---|---|---|
| 게임 중 캐릭터 교체를 "가(미리보기)"로 할지 "나(기능)"로 할지 | 사람 | [game-design.md 10장](game-design.md#10-확정-사항과-남은-미결정-사항) |
| 원소 체계 (탑 100층에서 뺐고 새로 설계하기로 함) | 사람 | [tower-100-design.md 3장](tower-100-design.md#3-미결정--정해지기-전에는-중립값으로-둔다) |
| 적의 정체·세계관 → 정해지면 탑 변주 이름을 붙인다 | 사람 | [game-design.md 10장](game-design.md#10-확정-사항과-남은-미결정-사항) |
| 망령룡 스킬 수치·판정 프레임·기술 설계 승인 | 사람 | [characters/revenant-dragon-skills-v1.md 8장](characters/revenant-dragon-skills-v1.md) |
| 자동 사냥 사망 위험의 허용 수준 | 사람 | [game-design.md 10장](game-design.md#10-확정-사항과-남은-미결정-사항) |
| 스킬 자동 채움을 끌 수 있게 할지 | 사람 (보류 — 칸을 비워 두고 싶은 사람이 실제로 있는지 먼저 본다) | [decisions.md D](decisions.md#d-망령룡-장판-스킬-중복-2026-11-15) |
| 작업표시줄 띠가 바탕화면을 얼마나 먹어도 되는지 (지금은 작업표시줄과 같은 높이) | 사람 | [known-issues.md 1번](known-issues.md) |
| **낙인(중첩)·궁극기(충전) 기능을 만들지** — 그림 4장은 있는데 **게임에 그 개념 자체가 없다**(`SkillKind` 10종에 없고 기획서에 한 글자도 없다). 새 전투 메커니즘 설계다 | 사람 | [characters/revenant-dragon-skill-concepts.md](characters/revenant-dragon-skill-concepts.md) |
| **죽음의 행렬을 자동으로 할지 수동으로 할지, 영혼을 소모할지** — 정해져야 그림 6장을 붙인다 | 사람 | [characters/revenant-dragon-skills-v1.md](characters/revenant-dragon-skills-v1.md) |
| **영혼 수확 연출을 체력이 가득 찼을 때도 띄울지** — 지금은 회복량이 0이면 연출도 없다(기존 회복 연출 조건 그대로) | 사람 | [windows-checklist.md 78번](windows-checklist.md) |
| 드래곤 첫 제작 목표 종 수 (2명 기준으로 다시 정해야 한다) | 사람 | [character-pipeline.md 1장](character-pipeline.md) |

## 순서가 걸려 있는 것 — 앞 작업이 끝나야 집는다

| 할 일 | 앞에 끝나야 하는 것 | 근거 |
|---|---|---|
| 깊이감 축 C (사냥터 고르기) | **탑 10층** | [decisions.md C](decisions.md#c-깊이감-축-c--자동-사냥-2026-11-15) |
| 깊이감 축 B (탑 진입 변주) | **탑 10층 + 층별 보상** | [decisions.md B](decisions.md#b-깊이감-축-b--탑-진입-변주-2026-11-15) |
| 탑 100층 T2~T8 | T1 완료(2026-11-15) | [tower-100-design.md 6장](tower-100-design.md#6-단계-계획-브랜치-하나--pr-하나) |
| **스킬 효과 수치 조정**(`fx_alpha`·`fx_scale`·`fx_offset_*`·단계 길이·띠 높이) | **Windows 확인 59~78번** | 이 환경에서는 `src/win` 을 실행할 수 없어 수치가 맞는지 볼 수 없다 |
| **낙인·궁극기·죽음의 행렬 그림 붙이기** | **위 기획 결정 세 건** | 그림만으로는 못 붙인다 |

## 사람이 Windows 에서 확인할 것

이 환경에서는 `src/win` 을 실행할 수 없다. 쌓인 확인 항목은 [windows-checklist.md](windows-checklist.md).
