그림자용 이미지 모음

illustration_shadow_dragon.png : 기준 일러스트
walk_horse_v1.png, walk_forward_v2.png, walk_order_v3.png : 이전 걷기 시트
dragon_walk_art_12.png : 마지막으로 만든 걷기 시트
dragon_flight_12_reference_v3.png : 참고 영상에 맞춰 수정한 비행 시트
dragon_attack_12_v2.png : 수정한 앞발 공격 시트
dragon_rest_relaxed_12_v3.png : 몸을 느슨하게 펴고 쉬는 시트
나머지 flight, attack, rest PNG : 이전 버전

preview.html 파일을 열면 같은 폴더의 PNG를 순서대로 재생합니다. GIF는 이전 걷기 시트의 재생본입니다.
시트는 동작 시안이며 배경 및 프레임 간 정렬과 연결에 수정이 필요할 수 있습니다.

--- 2026-11-14 추가: 스킬 그림 (v1.4) ---

본체 모션 (게임에 들어갔다. skill_*.png 로 변환)
  shadow_01_body_claw.png    발톱 휘두르기 -> skill_claw.png
  shadow_01_body_breath.png  숨결 -> skill_breath.png
  shadow_01_body_barrier.png 보호막 -> skill_barrier.png

효과(FX) 레이어 (아직 게임에 안 들어갔다 — 효과 전용 그리기 코드가 없다)
  shadow_02_claw_fx.png        발톱 궤적
  shadow_02_breath_fx.png      숨결
  shadow_02_barrier_activate.png / shadow_03_barrier_loop.png / shadow_04_barrier_absorb.png
                               보호막 생성 -> 유지 -> 흡수
  shadow_01_mark_1_loop.png / shadow_02_mark_2_loop.png / shadow_03_mark_burst.png
                               낙인 1중첩 -> 2중첩 -> 터짐
  shadow_05_charge_loop.png    궁극기 충전

모두 1448x1086, 4열x3행, 362px 칸 12프레임, 투명 배경, 오른쪽 지향이다.
궁극기 본체·바닥 그림자·폭발은 아직 없다(프롬프트만 있다).

변환 명령은 assets/dragons/shadow/dragon.txt 주석에 적어 두었다.
**스킬 시트만 따로 변환하지 말 것** — 공통 배율이 달라져 스킬을 쓸 때 몸 크기가 튄다.
기존 다섯 상태와 반드시 한 명령에 넣는다.
