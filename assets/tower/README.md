# 탑 층 사용법 (v1.6)

탑의 층 구성을 `floors.txt` 하나로 정합니다. 코드를 고치지 않고 층을 늘리거나 적 구성을 바꿀 수
있습니다. 적([assets/enemies](../enemies/README.md))·드래곤([assets/dragons](../dragons/README.md))과 같은
`키=값` + `[구역]` 형식입니다.

## 파일

| 파일 | 내용 | 없으면 |
|---|---|---|
| `floors.txt` | 층 구성 | **내장 3층**을 씁니다(v1.5까지와 똑같이 동작합니다) |
| `floors.example.txt` | 베껴 쓸 예시. 게임은 읽지 않습니다 | — |

`floors.txt` 를 지우면 언제든 전으로 돌아갑니다.

## floors.txt 에 적는 것

```
[floor.1]
title=1층            ; 화면에 보일 이름. 비우면 "1층"으로 보인다
spacing=220          ; 옆 적과의 세로 간격(px)
boss=no              ; 보스 층인가
enemy=slime x2 lv4   ; 적 키 · x마리수 · lv레벨

[floor.2]
title=2층
spacing=160
enemy=goblin x1 lv6
enemy=ogre x1 lv6 inset=60   ; inset: 오른쪽 끝에서 이만큼 더 앞으로 당긴다
enemy=goblin x1 lv6

[floor.3]
title=3층: 수호자
boss=yes
enemy=- x1 lv8 inset=40      ; 키 자리의 '-' 는 "이 층의 기본 적"(보스 층이면 보스)
```

### 규칙

- **`[floor.1]` 부터 번호를 건너뛰지 않고** 차례로 적습니다. 건너뛰면 경고가 나고 그 층은 무시됩니다
  (`dragon.txt` 의 `[skill.N]` 과 같은 규칙입니다).
- `enemy=` 는 한 층에 여러 줄 적을 수 있습니다. 적힌 순서대로 위에서 아래로 섭니다.
- `x`(마리 수)·`lv`(레벨)·`inset`은 순서에 상관없고, 적지 않으면 `x1 lv1 inset=0` 입니다.
- 적 키는 `assets/enemies/<키>/` 의 폴더 이름입니다. **없는 키를 적으면** 경고가 나고 그 층의 기본
  적으로 대신 섭니다 — 조용히 넘어가지 않습니다.
- 보스 키를 아래 층에 적어도 됩니다(v1.5까지는 일반 적만 놓을 수 있었습니다).
- 적이 하나도 없는 층은 들어가는 순간 돌파되어 탑이 공짜가 되므로, 경고를 내고 **그 층을 뺍니다.**
  뒤 층의 번호가 하나씩 앞으로 옮겨집니다.
- 상한: 층 50개, 한 층에 `enemy=` 12줄, 한 줄에 8마리, 레벨 99
  ([`include/core/Config.h`](../../include/core/Config.h) 의 `kMaxTowerFloors` 등). 넘으면 맞추고 알립니다.

## 확인

게임을 켜지 않고 확인하려면:

```
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

게임을 켜면 읽은 층 수와 경고가 화면 알림으로 뜹니다.

## 주의

- **층 돌파 회복·경험치는 아직 전 층이 같습니다**(`cfg::kTowerFloorHeal`, `kTowerClearExp`).
  층마다 다르게 하는 것은 따로 할 일로 남겨 두었습니다.
- 탑에서는 자동 사냥을 쓸 수 없습니다(확정 규칙). 층을 늘려도 이것은 바뀌지 않습니다.
