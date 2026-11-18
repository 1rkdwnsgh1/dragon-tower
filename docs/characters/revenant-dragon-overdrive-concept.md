# 망령룡 오버드라이브(2단 궁극기) 기획 시안 — 2026-11-18

**상태: 제안이고 승인이 아니다. 수치는 전부 임시값이다.**
이 문서는 [변신폼 기획 시안](revenant-dragon-transform-concepts.md)의 **위에 쌓는 2단계**를
설계한다. 변신폼 시안(A/B/C/D) 선택을 대신하지 않는다 — 1단계가 무엇으로 정해지든
그 위에 올라가는 상위 단계로 썼다.

사용자 요구(2026-11-18): **"최대한 화려하게, 극적으로 변화를 만들어라."** 새 본체 그림
추가 제작을 **허용**받았다(변신폼 A안의 "새 그림 0장" 제약이 여기서는 풀린다).

**선행 문서를 먼저 읽을 것.** 지금 게임이 할 수 있는 것과 없는 것(지속 FX 없음, 공격력 버프
없음, 시트 세트 교체 없음, 게이지 없음)은 [변신폼 문서 2절](revenant-dragon-transform-concepts.md)에
적었고 여기서 되풀이하지 않는다. 이 문서는 **그 위에 무엇을 더 쌓는가**만 다룬다.

---

## 1. 2단 구조 — 왜 1단계를 더 세게 만들지 않고 단계를 나누나

변신폼 하나를 "더 화려하게" 만드는 쪽은 **같은 연출을 매번 본다**는 문제가 있다. 궁극기가
15초짜리 하나면 플레이어는 그 15초를 평생 수백 번 본다. 반면 2단으로 쪼개면

- 1단계는 **자주 본다** → 가볍고 안전한 연출(겹침 영체, 가리기 위험 낮게)
- 2단계는 **드물게 본다** → 비싸고 과한 연출을 넣어도 피로하지 않다

"극적"은 절대적인 화려함이 아니라 **직전 상태와의 차이**다. 1단계 변신폼이 이미 떠 있는
화면에서 2단계로 올라가면 비교 대상이 눈앞에 있어 차이가 가장 크게 읽힌다. 아무 버프 없는
평상시에서 곧바로 최대 연출로 가면, 그 연출이 아무리 화려해도 **비교 기준이 없다.**

### 승격 조건 (임시안)

| 안 | 조건 | 평가 |
|---|---|---|
| **ㄱ (추천)** | 변신폼 **지속 중** 영혼 게이지를 다시 100 채운다 | 변신 중 적을 많이 잡아야 하므로 "밀어붙인 보상"으로 읽힌다. 자원 하나(`soulCharge`)만 쓴다 |
| ㄴ | 변신폼 지속 중 **5킬** | 게이지와 별개의 카운터가 하나 더 늘어난다 |
| ㄷ | 게이지 200 모아 **처음부터 2단계로** 발동 | 1단계를 건너뛰어 "차이"가 사라진다. 2절의 전제가 무너진다 — 비추천 |

ㄱ을 고르면 영혼 게이지는 **변신폼 중에도 계속 적립**되어야 한다. 1단계 발동에 게이지를
비우고, 변신 중 다시 채우면 즉시 승격한다. 코어에서 "변신 중 게이지 적립을 막지 않는다"는
한 줄이 설계의 전부다.

**실패 가능성 — 정직하게.** 변신폼 12~15초 안에 게이지 100을 다시 채울 만큼 적이
나오지 않으면 **오버드라이브를 아무도 보지 못한다.** 바탕화면 디펜스는 적이 왼쪽에서
들어오는 속도가 정해져 있고, 탑은 층당 적 수가 고정이다. 승격 비용은 반드시
`tests/balance_sim.cpp` 로 "변신 중 평균 처치 수"를 재서 정해야 한다 — 지금 적은 전부
임시값이다. 못 보는 궁극기는 안 만든 것과 같다.

---

## 2. 「왕의 귀환」 — 오버드라이브 연출

> 근거 그림(2026-11-16 콘셉트 60종 중): 7차 3번 `백골왕의 승천`(미평가) ·
> 7차 7번 `창백한 불사조`(미평가) · 7차 4번 `멸망한 왕국의 군기`(미평가) ·
> 1차 2번 `백골천궁`(**제작 참고 선택**) · 1차 8번 `백골 재구성`(미선택, 폐기 아님) ·
> 1차 5번 `천혼유성`(**제작 참고 선택**)
> → [콘셉트 보관 문서](revenant-dragon-skill-concepts.md)

**설정.** 1단계에서 거느리던 **선왕의 영체가 본체 안으로 빨려 들어가 합일한다.** 거느리는
것에서 **그 자신이 왕이 되는 것**으로 바뀐다. 그래서 1단계의 "뒤에 겹친 거대 영체"가
사라지는 것이 2단계의 시작 신호가 된다 — 화면에서 뭔가가 **없어지는 것**으로 변화를 알리는
쪽이, 뭔가를 더 얹는 것보다 눈에 더 잘 띈다.

### 연출 타임라인 (총 2.4초, 임시값)

| 구간 | 길이 | 무엇이 보이나 | 구현 수단 |
|---|---|---|---|
| **0 흡인** | 0.5초 | 뒤의 거대 영체가 본체 흉부 코어로 **빨려 들어간다.** 영체 스케일 2.2 → 1.0, 투명도 0.40 → 0, 동시에 코어 발광이 최대로 | 1단계의 겹침 레이어 파라미터를 **보간**하는 것뿐 — 새 그림 0장 |
| **1 정지** | 0.3초 | 본체가 멈추고 뼈가 **흰색으로 과열**된다. 주위 혼불이 안쪽으로 수축 | 본체 틴트(렌더러 색 곱하기 필요) + FX 1장 |
| **2 재구성** | 0.8초 | 골격이 흩어졌다가 **더 큰 골격으로 재조립**된다. 왕관 뿔이 활짝 전개, 손상된 영혼 날개가 좌우 대칭으로 **완전 재생** | 전용 변신 모션 시트 1장 (1차 8번 `백골 재구성` 그림을 그대로 쓸 수 있다) |
| **3 개안** | 0.3초 | 눈이 **백색으로 점화**되고 발이 지면에서 떨어진다(부양 시작). 머리 위로 백골 고리(백골천궁) 1개가 전개 | 오버드라이브 폼 시트 + 고리 FX 1장 |
| **4 군림** | 지속 전체 | 부양 유지 · 척추 광맥 흐름 · 뒤에 군기 실루엣 2~3개 · 발밑에 혼불 자취 · 날개 끝에서 불사조 깃 잔상 | 지속 오라 레이어 2개 |

**해제 연출(0.6초).** 역순이 아니다. 왕관이 접히고 날개가 **다시 찢어지며** 재가 되어
떨어진다. 몸이 1.0배로 돌아오며 지면에 **내려앉는 충격파** 1회. 끝날 때도 사건이 있어야
"꺼졌다"가 아니라 "끝났다"로 읽힌다.

### 지속 중 바뀌는 것 — 연출이 전투 행동에도 붙는다

겉모습만 바뀌면 "스킨"으로 읽힌다. 평소 행동 자체가 달라져야 "폼"으로 읽힌다.

| 행동 | 평상시 | 오버드라이브 중 |
|---|---|---|
| 기본 공격 | 영혼 탄 1발 | 3발 동시(부채꼴) + 명중 시 작은 혼백 폭발 |
| 이동 | 네 다리 보행 | **부양 활공** (`fly` 시트 사용, 발이 땅에 닿지 않는다) |
| 피격 | 피격 모션 | 피격 모션 생략, 대신 몸 주위 룬 고리가 1회 깨진다(경직 없음) |
| 스킬 발동 | 각 스킬 FX | 발동 시 머리 위 고리가 같이 회전 가속 |

**이 표가 제작량의 대부분이다.** 기본 공격 3발과 피격 경직 생략은 각각 코어 수정이고,
밸런스 영향이 수치 버프보다 크다. 전부 넣지 않고 **고를 수 있게** 적었다.

---

## 3. 임시 수치 — 전부 재측정 대상

변신폼 1단계(A안 기준 공격력 +70%)와 **곱이 아니라 치환**으로 둔다. 곱하면 +70% × +160%가
되어 4배가 넘고, 자동 사냥 난이도 계산이 무너진다.

| 항목 | 1단계 (변신폼 A) | 2단계 (오버드라이브) |
|---|---|---|
| 지속 | 12초 | 1단계 남은 시간 **+10초** |
| 공격력 | ×1.70 | **×2.60** (치환) |
| 받는 피해 | ×0.75 | ×0.60 |
| 스킬 쿨타임 | ×0.70 | ×0.40 + 발동 시 전체 쿨 초기화 1회 |
| 이동 속도 | ×1.15 | ×1.30 (부양) |
| 회피 | — | +20% (근접 한정) |
| 보기 크기 | ×1.0 | ×1.25 (**판정에는 곱하지 않는다** — 4절) |

**종료 페널티(임시안).** 끝난 뒤 5초간 공격력 ×0.70, 영혼 게이지 0으로 초기화. 페널티가
없으면 "강할 때만 싸우면 되는" 최적 전략이 생겨 자동 사냥이 대기 게임이 된다. 반대로
페널티가 너무 세면 궁극기를 **안 쓰는 쪽이 유리**해진다 — 이 수치가 가장 틀리기 쉽다.

---

## 4. 설계상 미리 막아 둔 것

선행 결정과 핵심 규칙에 걸리는 지점을 먼저 처리했다. 근거는 링크로만 둔다.

1. **보기 크기 ×1.25는 판정에 곱하지 않는다.** 크기 비례 판정(`scale_with_size`,
   2026-11-17 결정)과 곱해지면 1000% 설정에서 판정이 또 커진다. 변신폼 C안에서 같은
   이유로 "곱하지 않는다"를 권했다 — **두 문서가 같은 결론이어야 한다.**
2. **"예고 후 판정"을 가리지 않는다.** 2단계는 1단계의 거대 영체를 **없애는** 방향이라
   가리기 위험이 오히려 **줄어든다.** 다만 날개 완전 재생으로 가로 폭이 늘어나므로, 날개
   잔상과 군기는 `fx_front=no` + `fx_alpha ≤ 0.35` 로 둔다.
3. **화면 전체를 어둡게 하는 연출은 넣지 않았다.** 「망자의 일식」(1차 6번) 그림이 가장
   극적이지만, 이 게임은 **바탕화면 오버레이**다. 화면을 덮으면 사용자가 하던 작업을 가린다 —
   작업표시줄 띠가 바탕화면을 얼마나 먹어도 되는지가 아직 미결정인 상태에서
   **전체 화면 암전은 그보다 훨씬 큰 침범**이다. 일식은 **탑 전투 전용 연출**로 따로 올리는
   쪽을 권한다(이 문서 범위 밖).
4. **부양 중에도 "아래쪽에만" 설정을 따른다.** 띠 안에서 띄우려면 부양 높이는 띠 높이에
   비례해야 한다 — 고정 px 로 올리면 띠 밖으로 나간다.
5. **소리 금지.** 2.4초 변신 연출의 "타격감"을 소리로 벌 수 없으므로 전부 시각 수단
   (정지 0.3초, 수축, 점화 섬광)으로 설계했다. 위 타임라인의 **0.3초 정지 구간이 그 역할**이다.
6. **저장 중 오버드라이브 상태로 종료하면 저장하지 않는다.** 해제된 상태로 복귀한다.
   폼 상태를 저장하면 "끄고 다시 켜서 궁극기 유지"가 가능해진다.
7. **`dragon.txt` 는 `[skill.9]` 로 맨 뒤에 붙인다.** 현재 망령룡은 `[skill.1]`~`[skill.7]`,
   변신폼이 `[skill.8]` 이다. 중간에 끼우면 저장 파일의 장착 칸이 어긋난다
   ([decisions.md D](../decisions.md)).
8. **장착 칸을 더 먹지 않는다.** 오버드라이브는 **별도 스킬이 아니라 변신폼의 승격 상태**다.
   4칸 중 하나를 더 쓰면 평소 구성이 2칸으로 줄어 평상시 플레이가 망가진다.

---

## 5. 새로 그려야 하는 것 (그림 추가 허용 기준)

규격은 기존과 같다 — **칸 256×256, 발 기준점 (128, 240), 가로 한 줄**
([character-pipeline.md](../character-pipeline.md) 4장). 부양 상태라 "발 기준점"은
**부양하지 않은 발 위치**로 그리고 띄우는 것은 코어가 한다 — 시트에서 미리 띄우면 띠 안에서
위치가 어긋난다.

| 우선 | 파일 | 프레임 | 없으면 어떻게 되나 |
|---|---|---|---|
| **1** | `form_overdrive/idle.png` | 4~6 | 2단계가 아예 안 보인다. **필수** |
| **1** | `form_overdrive/attack.png` | 4~6 | 평상시 공격 시트로 대체(왕관·날개가 사라져 깜빡인다) |
| **2** | `form_overdrive/fly.png` | 6~8 | 부양 이동이 보행 시트로 보여 "떠 있는데 걷는다"가 된다 |
| **2** | `skill_overdrive_burst.png` (변신 2.4초 전용 모션) | 8~12 | 재구성 구간이 사라지고 **1프레임에 폼이 바뀐다** — 극적 변화가 가장 많이 깎이는 지점 |
| 3 | `fx_crown_halo.png` (머리 위 고리, 반복) | 8 | 고리 없음. 도형으로 임시 대체 가능 |
| 3 | `fx_phoenix_trail.png` (날개 잔상, 반복) | 8 | 생략 가능 |
| 4 | `form_overdrive/hit.png` · `death.png` | 2 / 4~6 | 평상시 시트로 대체. 피격·사망은 짧아 티가 적다 |
| 4 | `fx_fallen_banners.png` (군기, 반복) | 6 | 생략 가능 |

**최소 3장(1순위 2장 + 변신 모션 1장)이면 "극적 변화"가 성립한다.** 1·2순위 4장이 권장선,
8장 전부는 과하다. **그림이 1순위 2장도 없으면 이 기획은 착수하지 않는 쪽이 낫다** — 틴트와
고리만으로는 변신폼 B안(가장 안 읽히는 안)과 결과가 같아진다.

1인 1종 원칙상 본체 시트는 망령룡 담당자가 그린다. **그림 일정에 전부 묶이는 기획이다.**

---

## 6. 코드 변경 범위 — 변신폼 1단계 위에 얹는 분량만

변신폼 구현에서 이미 만드는 것(`SkillKind::Transform`, 폼 상태 필드, 공격력·쿨 배율 경로,
지속 FX 레이어, 영혼 게이지 + 저장)은 **여기서 다시 만들지 않는다.** 아래는 **추가분**이다.

| 무엇 | 어디 | 크기 | 비고 |
|---|---|---|---|
| 폼 **단계** 값 (1/2) | `DragonActor` | 작다 | `formTime` 옆에 `formTier` 하나 |
| 승격 판정 (변신 중 게이지 100) | `World.cpp` 게이지 적립 지점 | 작다 | |
| 본체 시트 **세트 전환** | `src/win/Sprites.cpp`·`Renderer.cpp` | **중간~큼** | 변신폼 A안을 고르면 **여기서 처음 생긴다**. C안을 고르면 이미 있다 |
| 본체 틴트(색 곱하기) | `src/win/Renderer.cpp` | 중간 | 지금 경로가 없다. 1 구간 "과열"에 필요 |
| 부양 오프셋 | `Renderer.cpp` + 띠 높이 연동 | 작다 | |
| 기본 공격 3발 | `World.cpp` 공격 생성 지점 | 중간 | **고를 수 있는 항목.** 밸런스 영향 큼 |
| 피격 경직 생략 | `World.cpp` | 작다 | **고를 수 있는 항목** |
| 2.4초 연출 상태 기계 | `World.cpp` | 중간 | 구간 5개. 기존 `fx_windup/hold/tail` 3단계로는 부족하다 |

**`src/win` 변경이 들어가므로 "소스는 하루씩 번갈아" 규칙에 걸린다.**
그리고 선행 문서와 같은 결론: **착수는 Windows 확인 59~78번 뒤가 맞다.** 이 환경에서는
`src/win` 을 실행할 수 없어 `fx_alpha`·`fx_scale`·오프셋이 맞는지 아직 아무도 못 봤다.
못 본 수치 위에 2단계 연출을 쌓으면 어느 층이 틀렸는지 분리되지 않는다.

### `dragon.txt` 초안 (변신폼 `[skill.8]` 에 키를 더하는 쪽)

오버드라이브는 별도 스킬이 아니므로 `[skill.9]` 를 새로 만들지 않고 변신폼 블록에
`od_` 키를 더하는 쪽이 저장 호환에 안전하다. 전부 **새 키**이고 전부 임시값이다.

```ini
; [skill.8] 변신폼 블록에 이어 적는다
od_enable=yes
od_charge_cost=100        ; 변신 지속 중 다시 채워야 하는 양
od_extend=10.0            ; 승격 시 지속 시간 연장(초)
od_atk_mult=2.60          ; 1단계 값을 치환한다(곱하지 않는다)
od_dmg_taken_mult=0.60
od_cooldown_mult=0.40
od_speed_mult=1.30
od_evade=0.20
od_render_scale=1.25      ; 판정에는 곱하지 않는다
od_hover_ratio=0.18       ; 띠 높이 대비 부양 높이
od_form_dir=form_overdrive
od_motion=overdrive_burst ; 2.4초 변신 모션
od_stage_pull=0.5         ; 0 흡인
od_stage_still=0.3        ; 1 정지
od_stage_rebuild=0.8      ; 2 재구성
od_stage_eyes=0.3         ; 3 개안
od_release=0.6            ; 해제 연출
od_exhaust_time=5.0       ; 종료 후 탈진
od_exhaust_atk_mult=0.70
od_fx_halo=crown_halo
od_fx_trail=phoenix_trail
od_fx_banners=fallen_banners
od_fx_alpha=0.35
od_fx_front=no
```

---

## 7. 이 기획이 실패하는 경우 — 먼저 적어 둔다

1. **아무도 2단계를 못 본다.** 승격 비용이 지속 시간 안에 못 채울 양이면 기능이 없는 것과
   같다. → 1절의 재측정이 **착수 전 조건**이다.
2. **102px 에서 안 보인다.** 기본 보기 크기에서 왕관·룬 고리·군기는 몇 픽셀이다. 변신폼
   B안이 같은 이유로 "안 읽힌다"고 평가됐다. → 실루엣 변화(날개 재생·부양·크기)에
   연출 비중을 몰아 둔 이유다. **보기 크기 100% 미리보기 확인이 필수**다.
3. **공격력 ×2.60이 자동 사냥을 무너뜨린다.** 지금까지 어떤 스킬보다 큰 변화다. 자동 사냥
   사망 위험의 허용 수준이 아직 미결정이라 **기준 자체가 없는 상태**다.
4. **그림이 안 나온다.** 1순위 2장이 없으면 착수하지 않는다(5절).
5. **연출 2.4초 동안 플레이어가 조작을 못 한다고 느낀다.** 0.3초 정지는 의도된 연출이지만,
   그 사이 적의 공격 예고가 진행되면 "보고만 있다가 맞았다"가 된다. → **정지 구간에는
   무적을 주거나**, 예고 중인 적이 있으면 정지를 0.1초로 줄이는 쪽 중 하나를 정해야 한다.
   이유를 알 수 없는 죽음은 수치 조정보다 먼저 고칠 항목이다.

---

## 8. 시안 이미지 생성 프롬프트 (4컷 1장)

기존 7장과 같은 규격(1컷 시트, 영문 라벨, 측면 뷰). 결과물은
`assets/references/revenant-dragon/work-YYYY-MM-DD-overdrive/` 에 `manifest.json` 과 함께
넣고, **`.gitattributes` 에 LFS 패턴을 먼저 추가한다**(2026-11-16에 빠뜨린 항목).

```text
Create ONE high-resolution landscape concept sheet, exactly TWO columns and TWO rows of FOUR
equal-sized gameplay screenshots with thin clean separators. Use case: stylized-concept,
professional 2D game-development SECOND-STAGE ULTIMATE (overdrive) transformation sequence.
Each cell is a wide fixed side-view 2D fantasy idle RPG battlefield, not a poster. Preserve the
approved character design in every cell: ivory ash-gray weathered skeleton, long segmented
neck, crown-like branching horns, elongated skull, turquoise eyes, exposed ribcage with
floating turquoise soul core, FOUR skeletal legs, long spear-tipped tail, TWO bone wings with
translucent cyan membranes, ritual chains. Dragon at LEFT facing RIGHT, small dark humanoid
enemies at RIGHT, muted stone ground at the same height in all cells, restrained dark ruined
battlefield. The four cells are CONSECUTIVE MOMENTS of one escalation, so the dragon's position
and the ground line must match across cells. Hand-painted 2D sprite-friendly fantasy art,
controlled turquoise/pale cyan/spectral white with a single white-hot accent, crisp
silhouettes, subdued background. The dragon's own silhouette must stay fully readable and must
NOT be buried by effects. Do NOT darken the whole frame. No 3D, photorealism, perspective
camera, HUD, title or marketing layout. Effects never cross separators. Only small legible
labels along the bottom of each panel, exact English text below.
Read panels left to right, top row then bottom:
1 label "ABSORPTION": A huge translucent ghost dragon behind the solid dragon is being SUCKED
INWARD toward the solid dragon's chest core, stretched and thinning as it is drawn in; the
chest core is blindingly bright. The solid body is unchanged in size.
2 label "WHITE-HOT STILL": No apparition left. The solid dragon is motionless and its bones
glow WHITE-HOT from within, light leaking from every seam; surrounding ghostflames are
contracting inward toward the body. Same body size as panel 1.
3 label "RECONSTRUCTION": The skeleton has come APART mid-air and is reassembling into a LARGER
frame: crown horns unfolding into a wide radiating crown, the torn soul wings regrowing into
full symmetrical wings, bone fragments orbiting as they lock into place.
4 label "THE KING RECLAIMED": The completed overdrive form, about one quarter larger, HOVERING
clearly above the ground with feet not touching it, eyes ignited WHITE, a single ring of bone
fragments rotating above the head, a spine of light along the vertebrae, faint phoenix-feather
afterimages at the wingtips, two or three tattered banners standing faintly in the background
haze.
Prioritize faithful reference anatomy, a clear readable escalation from panel 1 to panel 4, and
identical ground line and camera across all four panels. Render maximum useful resolution,
approximately 4K landscape.
```

---

## 9. 사람이 정할 것

[변신폼 문서 8절](revenant-dragon-transform-concepts.md)의 6개 결정이 **먼저** 정해져야 한다.
아래는 그 위에 더해지는 것만 적는다.

1. **2단 구조를 쓸지** — 쓰지 않고 1단계만 더 세게 만드는 쪽도 선택지다(1절에 근거).
2. **승격 조건** — ㄱ(변신 중 게이지 재충전, 추천) / ㄴ(변신 중 5킬) / ㄷ(200 모아 즉시 2단, 비추천).
3. **지속 중 행동 변화를 어디까지 넣을지** — 기본 공격 3발 / 부양 활공 / 피격 경직 생략
   (2절 표). 밸런스 영향이 수치 버프보다 크다.
4. **2.4초 연출 중 무적을 줄지** — 또는 예고 중인 적이 있으면 정지를 줄일지(7절 5번).
5. **그림 몇 장까지 그릴지** — 최소 3장 / 권장 4장 / 전체 8장 (5절).
6. **일식(화면 암전)을 탑 전용 연출로 따로 기획할지** (4절 3번. 이 문서 범위 밖).
