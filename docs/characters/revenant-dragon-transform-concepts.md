# 망령룡 변신폼(궁극기) 기획 시안 — 2026-11-18

**상태: 제안이고 승인이 아니다.** 수치는 전부 임시값이다. 이 문서는
[STATE.md](../STATE.md)의 "낙인(중첩)·궁극기(충전) 기능을 만들지" 행을 사람이 결정할 수
있게 만들기 위한 선택지 정리다. 코드·`dragon.txt`·밸런스는 아직 건드리지 않았다.

변신폼은 **새 전투 메커니즘**이다. 지금까지의 스킬 작업(데이터 + FX 시트)과 규모가 다르므로,
어느 시안을 고르느냐가 곧 "코어를 얼마나 고치는가"를 정한다. 그 대응을 2절과 6절에 적었다.

---

## 1. 요구사항 (사용자, 2026-11-18)

- 망령룡의 **변신폼**을 만든다.
- **궁극기 느낌의 강화계 스킬**로 쓰인다.
- **무조건 더 화려하고 더 강해 보여야 한다.**

"강해 보인다"를 이 문서에서는 셋으로 나눠 다룬다. 셋이 전부 같은 작업량이 아니기 때문이다.

| 읽히는 방식 | 수단 | 드는 것 |
|---|---|---|
| 실루엣이 바뀐다 | 본체 시트 교체·확대 | 새 그림 3~4장 + 코어 |
| 몸에서 빛이 난다 | 지속 오라·틴트 | 렌더러 변경 + 그림 1~2장 |
| 숫자가 오른다 | 능력치 버프 | 코어 소규모 |

**지금 게임은 셋 중 아무것도 "지속되는 형태"로는 못 한다.** 다음 절이 근거다.

---

## 2. 지금 게임이 할 수 있는 것과 없는 것 (코드 확인, 2026-11-18)

확인한 파일: `include/core/Species.h`, `include/core/World.h`, `src/core/World.cpp`.

### 있는 것

- `SkillKind` 10종 중 강화계는 **둘뿐**이다 — `Haste`(이동 속도 `power`, 회피 `extra`),
  `Shield`(받는 피해 감소 `power`). 둘 다 `DragonActor`에 전용 필드로 지속 시간을 센다
  (`hasteTime`·`shieldTime`).
- 스킬 전용 본체 모션 교체는 **된다** — `motion=` 으로 `skill_<이름>.png` 를 재생한다
  (`DragonActor::skillMotion`). 단 **발동 중 한 번 재생**이고 상태가 끝나면 돌아온다.
- 여러 시트를 겹치는 레이어 효과의 **선례가 있다** — `layered_breath=` 의 5시트 묶음
  (`SkillDef::layeredBreath`, 망계의 숨결·어둠의 숨결).
- 세 단계 FX(`fx_windup`/`hold`/`tail`), 투명도(`fx_alpha`), 앞뒤(`fx_front`), 크기 비례
  판정(`scale_with_size`)은 전부 데이터로 조절된다.

### 없는 것 — 변신폼이 걸리는 지점

1. **공격력·쿨타임을 올리는 버프가 없다.** `Haste`는 속도와 회피만 만진다. "강해 보인다"의
   핵심인 공격력 증가가 **지금 데이터로는 표현 불가**다.
2. **지속되는 FX가 없다.** `Shield`도 발동 순간 0.5초짜리 FX 하나만 띄운다
   (`World.cpp:443`). 10초 동안 몸에 붙어 있는 오라를 그리는 경로가 없다.
3. **본체 시트 세트를 바꾸는 경로가 없다.** `AnimState` 7종(idle/move/attack/hit/death/fly/rest)은
   종(種)당 한 세트뿐이다. 변신 중 걷기·날기까지 바뀌려면 **세트 전환**이 필요하다.
4. **충전·중첩 자원이 없다.** 게임에 게이지 개념이 한 글자도 없다(기획서·`SkillKind` 모두).
   그래서 "궁극기"는 지금 **긴 쿨타임 스킬**로밖에 표현되지 않는다.
5. **장착 칸은 4개 고정**(`kSkillSlots = 4`)이다. 궁극기가 한 칸을 먹으면 평소 구성이 3칸으로
   줄어든다. 전용 칸을 따로 두려면 입력·장착창·저장을 같이 고쳐야 한다.

### 따라서 순서상 이 문서가 맞는 자리인가

맞다. [STATE.md](../STATE.md)의 "막혀 있는 것"에 **"낙인·궁극기 기능을 만들지 — 사람"** 이
이미 올라와 있고, FX 로드맵 6단계도 "기획 신설, 3명 합의"로 적혀 있다. 기획 없이는 그림도
못 붙이는 상태였으므로, **기획안 작성이 막힌 칸을 푸는 작업**이다.

**다만 구현 착수는 아직 이르다.** 앞에 Windows 확인 59~78번이 남아 있어
(`fx_alpha`·`fx_scale`·오프셋이 실제로 맞는지 아무도 못 봤다) 지금 새 FX를 더하면 **못 본
수치 위에 못 본 수치를 쌓는다.** 변신폼 구현은 그 확인 뒤가 맞다.

---

## 3. 구현 방식 세 갈래 — 어느 시안을 고르든 하나를 택해야 한다

| | (가) 덧붙임 | (나) 폼 전환 | (다) 데이터 흉내 |
|---|---|---|---|
| 무엇을 하나 | 본체 시트 그대로, 위·뒤에 **지속 오라/영체**를 겹친다 | 변신 중 **본체 시트 세트를 교체**한다 | 기존 `Haste` + 큰 일회성 FX |
| 코어 변경 | 중간 — 지속 FX + 복합 버프 | 큼 — 시트 세트 전환 + 지속 FX + 버프 | 없음 |
| 새 그림 | 1~2장 (오라·영체) | **3~4장** (idle/move/attack, 가능하면 fly) | 1장 |
| "변신"으로 읽히나 | 읽힌다 (실루엣이 커진다) | **가장 확실히 읽힌다** | 거의 안 읽힌다 |
| 되돌리기 | 쉽다 (키 지우면 끝) | 어렵다 (저장·애니 상태가 섞인다) | 쉽다 |
| 밸런스 위험 | 중간 | 중간 | 낮음 |

**추천: (가)로 시작해 (나)로 승격할 수 있게 짠다.** 근거는 셋이다.

1. 지금 환경은 `src/win`을 실행하지 못한다. 눈으로 못 보는 상태에서 시트 4장짜리 폼 전환을
   먼저 만들면, 11/17에 세 번 반복한 실수(계산값이 틀려 미리보기로 고친 일)를 네 번째로
   반복한다.
2. (가)의 지속 오라 렌더는 (나)에도 **그대로 쓰인다.** 버리는 작업이 없다.
3. 1인 1종 원칙상 본체 시트는 망령룡 담당자가 그려야 한다. (나)는 **그림 일정에 묶인다.**

(다)는 요구사항("무조건 더 화려하게")을 만족하지 못한다. 기록만 남긴다.

### 충전을 넣을지 — 궁극기 느낌의 핵심

| | 넣는 것 | 비용 | 평가 |
|---|---|---|---|
| ㄱ | 안 넣는다. 아주 긴 쿨타임(90초)만 | 없음 | 가장 싸다. "모았다 쓴다"는 감각은 없다 |
| ㄴ | **영혼 게이지** — 적을 처치할 때마다 적립, 가득 차면 쓸 수 있다 | 코어: 자원 1개 + 저장 필드 1개 + 표시 | **추천.** 망령룡의 `영혼 수확`(Lv7 패시브)과 설정이 이어진다 |
| ㄷ | 피해를 받을 때도 적립(복수 게이지) | ㄴ + 균형 조정 | 자동 사냥에서 일부러 맞는 행동을 유도한다. 보류 |

ㄴ을 고르면 **처치 수가 곧 게이지**라 바탕화면 디펜스 전투와 궁합이 맞고, 탑에서는 층당
적 수가 정해져 있어 쓸 수 있는 횟수가 자연히 제한된다. 저장 필드 하나(`soulCharge`)가 늘지만
기존 `SaveData` 추가와 같은 형태다.

---

## 4. 시안 — 네 개

네 시안 모두 2026-11-16 콘셉트 시트 60종 중 **이미 그려 둔 그림을 근거로 삼는다**
([콘셉트 보관 문서](revenant-dragon-skill-concepts.md)). 새로 그려야 하는 양을 줄이려는
의도이고, 선택은 사람이 한다.

### A안 — 「선왕의 현신」 (겹침형) ★추천

> 근거 그림: 1차 1번 `ANCESTRAL DRAGON MANIFESTATION` (**제작 참고 선택** 상태)

**무엇이 보이나.** 본체 뒤·위에 **거대한 반투명 선왕 영체**가 겹쳐 솟는다. 영체는 본체와 같은
골격이지만 2.2배 크고, 본체의 동작을 **반 박자 늦게 따라 한다.** 공격할 때 영체도 같이
휘두르고, 숨결을 쓰면 영체의 턱에서도 격류가 겹쳐 나간다. 흉부 코어에서 영체의 가슴으로
청록 실 다섯 줄이 이어져 "조종당하는 것이 아니라 거느리는 것"으로 읽히게 한다.

**왜 화려해 보이나.** 화면에서 차지하는 면적이 즉시 2배가 된다. 실루엣이 바뀌는데 **본체
그림은 한 장도 새로 안 그린다.**

**구현.** (가) 덧붙임. 지속 오라 레이어 1개 + 본체 포즈를 따라가는 지연 복제 1개.
지연 복제는 이미 있는 본체 시트를 **같은 프레임 인덱스에서 N프레임 뒤로** 그리면 된다 —
새 그림이 필요 없다.

**임시 수치.** 지속 12초 / 공격력 +70% / 받는 피해 −25% / 스킬 쿨타임 −30% / 이동 속도 +15%.

**정직하게 남는 문제.** 영체가 **적과 예고 범위를 가린다.** "예고 후 판정"은 깨지면 안 되는
핵심 규칙이라, 영체는 `fx_front=no`(본체·적 뒤)로 두고 `fx_alpha`를 0.45 이하로 둬야 한다.
그래도 바탕화면 오버레이에서는 차지 면적이 커 다른 창을 많이 덮는다 — 작업표시줄 띠 때와
같은 종류의 문제다.

---

### B안 — 「영혼핵 과부하」 (발광형)

> 근거 그림: 1차 3번 `SOUL CORE OVERLOAD` (**제작 참고 선택** 상태)

**무엇이 보이나.** 몸집은 그대로다. 흉부 코어가 부풀어 갈비뼈 사이로 빛이 새고, 뼈의 균열마다
청록 빛이 흐르며 **전신 윤곽이 백열**한다. 몸 주위를 동심원 룬 고리 세 개가 돌고, 발밑에
타지 않는 혼불 자취가 남는다. 지속 중에는 기본 공격이 탄이 아니라 **짧은 광선**으로 바뀐다.

**왜 화려해 보이나.** 색과 발광만으로 밀어붙인다. 그림 제작량이 가장 적다.

**구현.** (가) 덧붙임의 가장 가벼운 형태. 본체 틴트(발광 가산) + 고리 오라 1장.
**본체 틴트는 렌더러에 색 곱하기 경로가 필요하다** — 지금은 없다(확인 필요 항목).

**임시 수치.** 지속 8초 / 공격력 +100% / **지속 중 초당 최대 체력 2% 감소** /
종료 시 3초간 이동 속도 −30%(탈진).

**정직하게 남는 문제.** 네 시안 중 **"변신"으로 가장 안 읽힌다.** 실루엣이 안 바뀌어서
작은 표시 크기(약 102px)에서는 "색이 좀 밝아졌다"로만 보일 수 있다. 사용자의 "무조건 더
화려하게"에 가장 약하게 답하는 안이다. 반대로 자기 피해가 붙어 **고위험 고수익**이라는
성격은 가장 뚜렷하다.

---

### C안 — 「백골왕의 승천」 (진짜 폼 전환)

> 근거 그림: 7차 3번 `OSSUARY ASCENSION` (미평가) + 4차 1번 `사령왕의 왕관` (미선택, 폐기 아님)

**무엇이 보이나.** 왕관형 뿔이 **활짝 전개**되고, 손상된 영혼 날개가 **완전히 재생**되어
좌우 대칭으로 펼쳐진다. 몸이 1.3배 커지고 지면에서 떠오른다(발이 땅에 닿지 않는다).
척추를 따라 빛의 척수가 흐르고, 뒤로 멸망한 왕국의 군기 실루엣이 희미하게 선다.
변신 연출은 2초: 뼈가 흩어졌다가 더 큰 골격으로 재조립된다(1차 8번 `SKELETAL
RECONSTRUCTION`의 그림을 변신 순간에 그대로 쓸 수 있다).

**왜 화려해 보이나.** 실루엣·크기·자세가 다 바뀐다. **요구사항에 가장 정확히 답한다.**

**구현.** (나) 폼 전환. `assets/dragons/revenant/form_ascension/` 에 별도 시트 세트를 두고
변신 중 그 세트를 읽는다. 최소 `idle`·`move`·`attack` 3장, 부양 상태라 `fly`를 쓰면 4장.
코어에 "지금 어느 폼인가"가 생기고 애니·판정·`render_scale`이 폼을 따라간다.

**임시 수치.** 지속 15초 / 공격력 +80% / 받는 피해 −30% / 모든 스킬 즉시 쿨타임 초기화 /
부양 중 적의 근접 공격 회피 +20%.

**정직하게 남는 문제.** 제작량이 가장 크고, **그림 일정에 전부 묶인다.** 몸이 1.3배가 되면
11/17에 정한 크기 비례 판정(`scale_with_size`)과 곱해져 1000% 설정에서 판정이 또 커진다 —
폼 배율은 **판정에 곱하지 않기로** 미리 정해 두는 편이 안전하다. 저장 중 변신 상태로 게임을
끄면 어떻게 할지도 정해야 한다(권장: 저장하지 않고 해제된 상태로 복귀).

---

### D안 — 「멸망한 왕국의 각성」 (군단형) — 비추천, 기록용

> 근거 그림: 7차 8번 `THE KINGDOM'S FINAL AWAKENING`, 4차 8번 `사령왕의 옥좌`, 2차 6번 `망령 군단`

**무엇이 보이나.** 본체가 솟아오른 백골 옥좌에 앉고, 지면이 갈라져 **망령 병사 다섯이
올라와 대신 싸운다.** 본체는 손짓만 한다.

**왜 기록만 하나.** 게임에 **소환물 개념이 없다.** 적 외의 전투 유닛을 추가하는 일이라
`World`의 판정·수·AI가 전부 늘고, "바탕화면 전투는 디펜스"와 "자동 사냥" 규칙과 어떻게
맞물릴지부터 다시 정해야 한다. 변신폼이 아니라 **별개의 대형 기능**이다. 지금 집으면
A·B·C 전부보다 오래 걸린다.

---

## 5. 네 시안 비교

| | A 선왕의 현신 | B 영혼핵 과부하 | C 백골왕의 승천 | D 각성 |
|---|---|---|---|---|
| 화려함 (요구사항) | 높음 | **낮음~중간** | **가장 높음** | 가장 높음 |
| 새 본체 그림 | **0장** | 0장 | 3~4장 | 5종 + 소환물 |
| 새 FX 그림 | 1~2장 | 1~2장 | 2~3장 | 6장+ |
| 코어 변경 | 중간 | 중간(+틴트) | 큼 | **가장 큼** |
| 쓸 수 있는 기존 콘셉트 | **있음(선택됨)** | **있음(선택됨)** | 있음(미평가) | 있음(미평가) |
| 가리기 위험 | **높음** | 낮음 | 중간 | 높음 |
| 지금 착수 가능 | 예(Windows 확인 뒤) | 예 | 그림 일정 뒤 | 아니오 |

**추천 조합: A안을 1차로 만들고, 그림이 준비되면 같은 버프·게이지 위에 C안을 2차로 올린다.**
A의 지속 오라·복합 버프·충전 게이지는 C에서 그대로 쓰이므로 버려지는 작업이 없다.
B안은 A·C와 **경쟁하지 않는다** — 자기 피해형이라 나중에 별개 스킬로 둘 수 있다.

---

## 6. 데이터·코드 변경 범위 (A안 기준 초안)

### `dragon.txt` — 맨 뒤에 이어 적는다

**반드시 `[skill.8]`로 뒤에 붙인다.** 중간에 끼우면 스킬 인덱스가 밀려 저장 파일의 장착 칸이
어긋난다 — 2026-11-15에 저주의 안개를 지우지 않은 것과 같은 이유다
([decisions.md D](../decisions.md)).

```ini
[skill.8]
name=선왕의 현신
desc=선대 망령룡 왕의 영체를 겹쳐 불러 한동안 함께 싸운다.
kind=Transform            ; 새 SkillKind
level=8
charge=soul               ; 영혼 게이지로 발동 (ㄴ안을 고를 때만)
charge_cost=100
cooldown=30               ; 게이지와 별도의 안전장치
duration=12
atk_mult=1.70             ; 새 키
dmg_taken_mult=0.75       ; 새 키
cooldown_mult=0.70        ; 새 키
speed_mult=1.15
; 겹침 영체 — 본체 시트를 N프레임 늦게 2.2배로 다시 그린다(새 그림 없음)
echo_scale=2.20
echo_delay_frames=2
echo_alpha=0.40
echo_front=no             ; 적과 공격 예고를 가리면 안 된다
; 변신·해제 순간 FX
fx=ancestral_burst
fx_alpha=0.55
fx_scale=2.6
fx_fps=12
fx_frame_w=256
fx_windup=0.30
fx_hold=1.20
fx_tail=0.50
```

### 코어

| 무엇 | 어디 | 크기 |
|---|---|---|
| `SkillKind::Transform` 추가 | `include/core/Species.h` | 작다 (`skillIsPassive`·이름 표만 고친다) |
| 폼 상태 필드 (`formTime`, `formSkill`) | `DragonActor` | 작다 (`shieldTime`과 같은 형태) |
| 공격력·쿨타임 배율 적용 | `World.cpp` 피해·쿨 계산 지점 | **중간 — 기존 수치 경로를 건드린다** |
| 지속 오라/겹침 그리기 | `src/win/Renderer.cpp` | 중간 |
| 영혼 게이지 + 저장 | `World`, `SaveData`, 표시 | 중간 |

**`src/win` 변경이 들어가므로 "소스는 하루씩 번갈아" 규칙에 걸린다** — 담당 날짜를 맞춰야 한다.

---

## 7. 시안 이미지 생성 프롬프트 (4컷 1장)

기존 7장과 같은 규격(1컷 시트, 영문 라벨, 측면 뷰)으로 맞췄다. 결과물은
`assets/references/revenant-dragon/work-YYYY-MM-DD-transform/` 에 `manifest.json`과 함께
넣는다(LFS 패턴을 `.gitattributes`에 추가할 것 — 11/16에 빠뜨린 항목이다).

```text
Create ONE high-resolution landscape concept sheet, exactly TWO columns and TWO rows of FOUR
equal-sized gameplay screenshots with thin clean separators. Use case: stylized-concept,
professional 2D game-development ULTIMATE TRANSFORMATION visualization. Each cell is a wide
fixed side-view 2D fantasy idle RPG battlefield, not a poster. Preserve the approved character
design in every cell: ivory ash-gray weathered skeleton, long segmented neck, crown-like
branching horns, elongated skull, turquoise eyes, exposed ribcage with floating turquoise soul
core, FOUR skeletal legs, long spear-tipped tail, TWO bone wings with translucent cyan
membranes and asymmetric tears, ritual chains. Dragon at LEFT facing RIGHT, small dark
humanoid enemies at RIGHT, muted stone ground at the same height in all cells, restrained dark
ruined battlefield. Each cell must read as a SUSTAINED EMPOWERED STATE, not a single attack.
Hand-painted 2D sprite-friendly fantasy art, controlled turquoise/pale cyan/spectral white,
crisp silhouettes, subdued background. The original dragon body must stay fully readable and
must NOT be hidden by effects. No 3D, photorealism, perspective camera, HUD, title or
marketing layout. Effects never cross separators. Only small legible labels along the bottom
of each panel, exact English text below.
Read panels left to right, top row then bottom:
1 label "ANCESTRAL MANIFESTATION": A huge TRANSLUCENT ghost dragon of the same skeletal design,
about twice the size, rises BEHIND and ABOVE the solid dragon in the same pose, half a beat
delayed; five thin cyan strands connect the solid chest core to the apparition's chest. The
solid dragon remains clearly in front and fully visible.
2 label "SOUL CORE OVERLOAD": Same body size, no apparition. The chest core is swollen and
white-hot, light leaks between every rib and along fine cracks in the bones, the whole
silhouette has a white-hot rim, three concentric rune rings orbit the torso, faint ghostflame
trails under the feet. Bones stay readable, not washed out.
3 label "OSSUARY ASCENSION": A TRANSFORMED body — crown horns fully unfolded into a wide
radiating crown, both soul wings fully regenerated and symmetrical, spread wide; body about
one-third larger and HOVERING clearly above the ground with feet not touching it, a spine of
light running along the vertebrae, faint tattered banners standing in the background haze.
4 label "KINGDOM'S AWAKENING": The dragon sits on a risen bone throne at the LEFT, wings
folded, one claw raised in command; the ground in the middle splits and FIVE translucent
spectral skeletal soldiers with cyan eyes climb out and advance toward the right-side enemies.
The dragon itself is not attacking.
Prioritize faithful reference anatomy, identical scale between panels 1, 2 and 4, four
unmistakably different sustained empowered states. Render maximum useful resolution,
approximately 4K landscape.
```

프롬프트를 따로 쓸 때의 주의: 2026-11-16 작업에서 **외부 묶음의 "원본" 표기를 믿었다가
재인코딩 사본을 받은 일**이 있었다. 생성 결과를 받으면 PC 원본과 화소 해시까지 대조한다.

---

## 8. 사람이 정할 것

1. **시안 선택** — A / B / C / (A→C 승격) 중 하나. D는 지금 집지 않는 쪽을 권한다.
2. **충전 방식** — ㄱ(긴 쿨타임만) / **ㄴ(영혼 게이지)** / ㄷ(복수 게이지).
3. **장착 칸** — 궁극기가 4칸 중 하나를 먹을지, 전용 칸을 새로 둘지(후자는 입력·장착창·저장
   변경).
4. **폼 배율을 판정에 곱할지** (C안을 고를 때만). 권장: 곱하지 않는다.
5. **착수 시점** — Windows 확인 59~78번 뒤로 미룰지. 권장: 미룬다.
6. 수치 전부 임시값이다 — `tests/balance_sim.cpp` 로 재측정해야 한다. 공격력 +70~100%는
   **지금까지 어떤 스킬보다 큰 변화**라 자동 사냥 난이도를 함께 본다.
