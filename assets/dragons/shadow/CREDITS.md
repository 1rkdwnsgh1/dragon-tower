# 그림자룡 자료 출처

원본은 `assets/references/shadow-dragon/work-2026-11-14/` 에 있다.
생성형 AI로 만든 자료이며 기준 일러스트는 `illustration_shadow_dragon.png` 다.

| 파일 | 원본 | 비고 |
|---|---|---|
| `idle.png` | `dragon_attack_12_v2.png` 의 앞쪽 4장 | 전용 대기 시트가 없어 1-2-3-4-3-2 로 왕복시켰다 |
| `attack.png` | `dragon_attack_12_v2.png` | 앞발 공격 12장 |
| `move.png` | `dragon_walk_art_12.png` | 크기 기준(`--ref move`) |
| `fly.png` | `dragon_flight_12_reference_v3.png` | |
| `rest.png` | `dragon_rest_relaxed_12_v3.png` | |
| `skill_claw.png` | **2026-11-18 교체** — 아래 "가르기 강화 모션" | 원래는 `shadow_01_body_claw.png`(v1.4, 12장) |
| `skill_breath.png` | `shadow_01_body_breath.png` | v1.4 — 어둠의 숨결 |
| `skill_barrier.png` | `shadow_01_body_barrier.png` | v1.4 — 어스름 장막 |

**여덟 장은 한 번의 명령으로 함께 변환했다.** 공통 배율(0.7278)을 써야 상태가 바뀔 때 몸 크기가
튀지 않는다. 명령은 `dragon.txt` 주석에 있다. v1.4에서 스킬 셋을 더해 다시 변환했을 때 기존
다섯 장이 바이트 단위로 같은 결과가 나오는 것을 확인했다.

## 검수 상태

시트는 동작 시안이다. 배경 처리와 프레임 간 정렬·연결에 수정이 필요할 수 있다(참고 폴더 README).

## 아직 없는 것

- `hit.png` `death.png` — 제작 전. 지금은 `idle.png`로 대신 그려진다.
- 스킬 효과(FX) — **발톱 궤적·숨결·보호막은 2026-11-17 에 넣었다**(아래 참고).
  보호막의 **흡수**(`shadow_04_barrier_absorb`)는 "피해를 막은 순간"에 띄울 자리가 코드에 없어
  넣지 않았다. 낙인·궁극기 충전 그림은 게임에 그 기능 자체가 없어 넣을 수 없다.
- 궁극기(영야의 날개) 본체·바닥 그림자·폭발 — 제작 전(프롬프트만 있다).

## 2026-11-17 추가 — 스킬 효과 시트 2장

| 파일 | 원본 | 변환 |
|---|---|---|
| `fx_claw.png` | `shadow_02_claw_fx.png` (4열x3행 12칸) 중 **그림이 있는 9칸** | 아래 명령 |
| `fx_breath.png` | `shadow_02_breath_fx.png` (같은 격자) 중 **그림이 있는 11칸** | 아래 명령 |

원본은 **오른쪽 지향**이라 망령룡과 달리 `--mirror` 를 쓰지 않았다.

```
python tools/make_sheet.py --set fx_claw=<빈 칸 뺀 낱장 폴더> --cell 256 --ratio 0.95 -o assets/dragons/shadow
python tools/make_sheet.py --set fx_breath=<빈 칸 뺀 낱장 폴더> --cell 256 --ratio 0.95 -o assets/dragons/shadow
```

**격자 지정(`@3x4:*,1-4`)으로 한 번에 돌리면 실패한다** — 빈 칸이 섞여 있어
`make_sheet.py` 가 "완전히 투명합니다" 로 멈춘다. 칸마다 알파를 재서 **보이는 픽셀이
칸의 0.3% 미만인 칸을 빼고** 낱장으로 내보낸 뒤 폴더로 넘겨야 한다. 뺀 칸은
발톱 3칸(1,1 / 3,3 / 3,4), 숨결 1칸(3,4) 이다.

수치는 `tools/fx_preview.py --dragon shadow --char-scale 0.5,1.0,10.0` 으로 보며 맞췄다.
`--fx-frame` 기본값(0번)은 두 효과 모두 거의 비어 있어 `--fx-frame 5` 로 봤다.

## 2026-11-17 추가 — 보호막 효과 시트

| 파일 | 원본 | 프레임 |
|---|---|---|
| `fx_barrier.png` | `shadow_02_barrier_activate.png` 12칸 + `shadow_03_barrier_loop.png` 12칸 | **24장** |

생성과 유지를 **한 시트로 이었다.** 효과는 1회 재생이고 지속 단계에서 마지막 칸에 멈추므로
(`DragonData.cpp` 의 `fxAnim.loop = false`), 유지 그림까지 이어 붙여야 3.5초 동안 움직인다.

```
python tools/make_sheet.py --set fx_barrier=<두 원본의 칸을 순서대로 모은 폴더> \
    --cell 256 --ratio 0.95 -o assets/dragons/shadow
```

**흡수(`shadow_04_barrier_absorb`, 11칸)는 넣지 않았다** — "피해를 막은 순간"에 효과를 띄우는
자리가 코드에 없다. 넣으려면 `World` 에서 보호막이 피해를 깎는 지점에 `addSkillFx` 를
부르는 변경이 필요하다.

## 어둠의 숨결 V2

`fx_umbral_vortex.png`는 사용자가 제공한 `UMBRAL_VORTEX_3LAYER_V2.zip`의
코어·나선·그림자 기류를 합성한 게임용 시트다. 시작 4장, 지속 8장, 종료 4장을
976×256 칸에 배치했다. 종료에는 원본의 나선과 그림자 기류 종료 낱장을 사용했다.

## 2026-11-18 — 가르기 강화 모션(교체)

| 파일 | 원본 | 변환 |
|---|---|---|
| `skill_claw.png` | `assets/references/shadow-dragon/skill-showcase-2026-11-18/claw/body.png` (16칸, 칸 311) | 칸마다 알파 16 이하를 지우고 256칸으로 줄인 뒤(배율 0.823) **프레임마다 맨 아래를 y=240 에 맞춤**. 가로 위치는 그대로 |
| `fx_claw.png` | 같은 폴더 `effect.png` (8칸, 칸 443) | 알파 16 이하만 지움. 크기 그대로(`fx_frame_w=443`) |

- 제작: OpenAI built-in image_gen (사용자가 Codex 로 생성, 2026-11-18). 생성형 AI 그림이다. 프롬프트는 같은 폴더 `generation.json`.
- 몸은 기존 그림자룡을 참고로 **다시 그린 것**이라 대기·이동 시트와 세부 비율이 조금 다를 수 있다(제작 쪽 README).
- 예전 `skill_claw.png`(12장)·`fx_claw.png`(9장)는 교체했다. git 이력에 남아 있다.
- 효과 위치(`fx_anchor`·`fx_origin`·`fx_scale`)는 제작 쪽 미리보기 `claw_player.py` 의 배치를 그대로 옮겼다.
