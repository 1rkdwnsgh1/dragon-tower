# 정예 리치 자료 출처

**그림을 새로 만들지 않았다. `lich`(리치) 의 시트를 그대로 재활용한다.**

| 파일 | 어디서 오나 |
|---|---|
| `idle.png` `move.png` `attack.png` `hit.png` `death.png` | 없다. `enemy.txt` 의 `art_from=lich` 으로 [`assets/enemies/lich`](../lich/) 의 시트를 읽는다 |

그림 출처·라이선스는 원본 폴더의 [`CREDITS.md`](../lich/CREDITS.md) 와 같다.
**시트를 이 폴더로 복사하지 않는다** — 복사하면 원본을 고칠 때 두 곳이 갈라진다.

## 주의

이 폴더에 `idle.png` 같은 파일을 넣으면 그 파일이 쓰이지 않는다(`art_from` 이 폴더 전체를
가리키기 때문이다). 변종만의 그림이 생기면 `art_from` 줄을 지우고 다섯 장을 모두 넣는다.

## 투사체 `projectile.png` (2026-11-25, T8)

- 제작: 사용자가 Codex(OpenAI built-in imagegen)로 생성. 생성형 AI 그림이다. 칸 128×128, 8프레임, 오른쪽으로 날아가는 그림.
- 원본·프롬프트·미리보기: `assets/references/t8-first-2026-11-25/`(README.txt, manifest.json).
- 정예 리치 전용 뼈 화살(리치보다 길고 톱니가 있다). 이 폴더에 있으면 `art_from` 보다 먼저 쓴다.
