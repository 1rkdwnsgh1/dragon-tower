# 망령룡 자료 출처

| 파일 | 원본 | 변환 |
|---|---|---|
| `idle.png` | `references/revenant-dragon/work-2026-11-13/REVENANT_IDLE_FINAL_V1.zip` 의 `FINAL_FRAMES` 4장 | 아래 한 번의 명령 |
| `move.png` | 같은 폴더 `REVENANT_WALK_FINAL_V1.zip` 의 `FINAL_FRAMES` 8장 | 아래 한 번의 명령 |
| `fly.png` | 같은 폴더 `REVENANT_FLY_FINAL_V1.zip` 의 `FINAL_FRAMES` 8장 | 아래 한 번의 명령 |

세 시트는 **한 번의 명령으로 함께 변환**했다(상태 간 크기를 맞추기 위해 필수):

```
python tools/make_sheet.py --mirror -o assets/dragons/revenant \
  --set idle=<IDLE>/FINAL_FRAMES --set move=<WALK>/FINAL_FRAMES --set fly=<FLY>/FINAL_FRAMES
```

생성형 AI로 만든 자료이며, 디자인 기준은 [망령룡 캐릭터 기획](../../../docs/characters/revenant-dragon.md)을 따른다.

## 주의: 검수 전 자료다

원본 패키지는 [제작 진행사항](../../../docs/characters/revenant-dragon-progress.md) 기준으로 모두
**NEEDS_REVIEW** 상태다. 파일명의 `FINAL`은 검수 완료나 게임 적용 승인을 뜻하지 않는다.
여기 넣은 것은 **게임에서 움직임을 확인하기 위한 시험용**이며, 승인된 최종 에셋이 아니다.
검수에서 수정이 나오면 원본을 고친 뒤 같은 명령으로 다시 변환한다.

## 2026-11-14 추가 (v1.4) — 공격·스킬 본체 모션

| 파일 | 원본 (`references/revenant-dragon/work-2026-11-14/`) |
|---|---|
| `attack.png` | `basic_attack/ranged/ranged_01..08.png` (망령탄) |
| `skill_melee.png` | `basic_attack/melee/melee_01..08.png` (망자의 발톱) |
| `skill_pierce.png` | `spirit_pierce_atlas.png` (혼령 관통) |
| `skill_erosion.png` | `spectral_erosion_atlas.png` (망령의 침식) |
| `skill_harvest.png` | `soul_harvest_atlas.png` (영혼 수확 — 패시브라 아직 쓰이지 않는다) |

다섯 장을 **한 번의 명령으로 함께 변환**했다. 명령과 주의사항은 `dragon.txt` 주석과
참고 폴더의 `README.txt`에 있다. 기존 대기와 몸 크기를 맞추려고 `render_scale=1.04`를 쓴다.

이 패키지도 검수 전 자료다. 새 패키지의 날개막이 기존 대기보다 덜 손상돼 보이는 점은 검수에서 확인해야 한다.

## 아직 없는 것

- `hit.png` `death.png` — 제작 전. 지금은 `idle.png`로 대신 그려진다.
- 스킬 효과(FX) — 효과 전용 그리기 코드가 없어 원본만 보관 중이다.
- 영혼 안개(`SOUL_MIST`) — 별도 레이어 합성이 필요해 현재 구조로는 쓸 수 없다.
