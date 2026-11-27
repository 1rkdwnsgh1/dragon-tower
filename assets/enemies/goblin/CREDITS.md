# 고블린 자료 출처

| 파일 | 원본 | 변환 |
|---|---|---|
| `idle.png` `move.png` `attack.png` `hit.png` `death.png` | 사용자가 준 몬스터 생성본(2026-11-16) `monsters-12frames` 의 `고블린` 아틀라스(12열 × 5행, 1942×809) | 알파 덩어리를 칸마다 나눠 가로 한 줄 256×256 시트로, 발을 (128, 240)에 맞춤. 60칸 공통 배율 |

- 제작: OpenAI built-in imagegen(사용자가 Codex로 생성). 생성형 AI 그림이다.
- 프롬프트: 원본 묶음의 `manifest.json`.
- 원본이 오른쪽을 보므로 뒤집지 않았다. `enemy.txt` 에 `art_facing=right`.
- 원본 칸이 작아(약 162px) 칸에 맞추며 확대했다. 화면에서 크게 그리면 조금 흐릴 수 있다.
- 예전 그림(이전 프로젝트 `kospher123-sys/dragon_` 에서 옮긴 것)은 이 PR 에서 교체했다. 기록은 git 이력에 있다.

## 투사체 `projectile.png` (2026-11-25, T8)

- 제작: 사용자가 Codex(OpenAI built-in imagegen)로 생성. 생성형 AI 그림이다. 칸 128×128, 8프레임, 오른쪽으로 날아가는 그림.
- 원본·프롬프트·미리보기: `assets/references/t8-first-2026-11-25/`(README.txt, manifest.json).
- 고블린 투사체는 고블린 족장용 단검(`goblin-chief/fx_dagger.png`, 칸 512, 12프레임)을 칸 128 로 잘라 줄였다(Claude, Pillow).
