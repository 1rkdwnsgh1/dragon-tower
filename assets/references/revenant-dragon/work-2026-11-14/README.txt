망령룡 스킬 그림 (2026-11-14)

스킬 기획은 docs/characters/revenant-dragon-skills-v1.md 를 본다.
모든 프레임은 444x444, 8프레임, 투명 RGBA, **왼쪽 지향**이다 (변환할 때 --mirror 로 뒤집는다).

본체 모션 — 게임에 들어갔다
  basic_attack/ranged/ranged_01..08.png   망령탄(원거리 기본 공격) -> attack.png
  basic_attack/melee/melee_01..08.png     망자의 발톱(근접)        -> skill_melee.png
  spirit_pierce_atlas.png                 혼령 관통                -> skill_pierce.png
  spectral_erosion_atlas.png              망령의 침식              -> skill_erosion.png
  soul_harvest_atlas.png                  영혼 수확                -> skill_harvest.png
    (영혼 수확은 패시브라 발동 모션이 없다. 변환만 해 두었고 아직 쓰이지 않는다)

manifest 파일에 원본의 권장 타이밍(duration_ms)과 단계(release_frame 등)가 적혀 있다.

효과(FX) 패키지는 아직 넣지 않았다 — 효과 전용 스프라이트를 그리는 코드가 없다.
원본(revenant_dragon_*_fx_v1.zip)에는 투사체 비행·명중, 발톱 궤적, 충전·관통·균열,
영혼 구슬·이동·흡수·강화가 각각 8프레임씩 들어 있다.

변환할 때 주의
- 기본 공격에 동봉된 ranged_sheet.png / melee_sheet.png 는 **1774x887** 이라 444 칸으로
  정확히 나뉘지 않는다. 격자로 자르면 1~2px 씩 밀린다. 낱장 프레임 폴더를 쓸 것.
- 나머지 셋의 *_atlas.png 는 1776x888 이라 @2x4 격자로 잘라도 낱장과 같은 결과가 나온다(확인함).
- 다섯 상태를 **한 명령에 넣어** 공통 배율을 쓴다. 명령은 assets/dragons/revenant/dragon.txt 주석에 있다.
- 기존 대기(idle)와 몸 크기를 맞추려고 render_scale=1.04 를 쓴다. 흉부 영혼 코어 높이를 재서
  정했다(대기 136px : 새 패키지 130px). 눈으로도 확인했다.

검수해야 할 것
- 새 패키지의 날개막이 기존 대기보다 덜 손상돼 보인다(구멍이 적다). 같은 개체로 보이는지 확인 필요.
