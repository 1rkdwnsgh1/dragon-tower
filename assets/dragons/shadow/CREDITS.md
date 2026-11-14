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
| `skill_claw.png` | `shadow_01_body_claw.png` | v1.4 — 그림자 가르기 |
| `skill_breath.png` | `shadow_01_body_breath.png` | v1.4 — 어둠의 숨결 |
| `skill_barrier.png` | `shadow_01_body_barrier.png` | v1.4 — 어스름 장막 |

**여덟 장은 한 번의 명령으로 함께 변환했다.** 공통 배율(0.7278)을 써야 상태가 바뀔 때 몸 크기가
튀지 않는다. 명령은 `dragon.txt` 주석에 있다. v1.4에서 스킬 셋을 더해 다시 변환했을 때 기존
다섯 장이 바이트 단위로 같은 결과가 나오는 것을 확인했다.

## 검수 상태

시트는 동작 시안이다. 배경 처리와 프레임 간 정렬·연결에 수정이 필요할 수 있다(참고 폴더 README).

## 아직 없는 것

- `hit.png` `death.png` — 제작 전. 지금은 `idle.png`로 대신 그려진다.
- 스킬 효과(FX) — 발톱 궤적·숨결·보호막·낙인·궁극기 충전 그림은 참고 폴더에 있지만,
  효과 전용 그리기 코드가 없어 아직 넣지 않았다.
- 궁극기(영야의 날개) 본체·바닥 그림자·폭발 — 제작 전(프롬프트만 있다).
