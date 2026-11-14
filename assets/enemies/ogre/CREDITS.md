# 오우거 자료 출처

| 파일 | 원본 | 변환 |
|---|---|---|
| `idle.png` `move.png` `hit.png` `death.png` | 이전 프로젝트 `kospher123-sys/dragon_` 의 `public/img/monsters-desk.png` (ogre 행) | `tools/make_sheet.py --mirror` 한 번 |
| `attack.png` | 같은 저장소 `public/img/monsters.png` (ogre 행 2~3열) | 같은 도구, 따로 변환 후 `render_scale`로 크기 보정 |

같은 팀이 이전에 만든 자료를 옮겨 왔다. 생성형 AI로 만든 그림이다.
원본은 왼쪽 지향이라 `--mirror`로 뒤집었다.

## 주의

`attack.png`는 다른 시트에서 왔고 원본 칸 크기가 달라, `enemy.txt`의 `render_scale`로 크기를 맞췄다.
값을 바꾸려면 대기 동작과 나란히 놓고 몸 크기를 비교해 정한다.
