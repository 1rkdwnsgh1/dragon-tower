# UI 스킨 사용법

창틀·버튼·토글·체력 막대 그림을 폴더 하나로 묶은 것이 **스킨**이다.
`assets/ui/<스킨키>/` 안에 PNG와 `ui.txt`를 둔다.

```
assets/ui/
  default/      ← 기본 스킨. 어떤 조각이 없으면 여기서 찾는다
    ui.txt
    panel.png  button.png  button_hover.png  ...
  revenant/     ← 드래곤 전용 스킨(원하는 조각만 넣으면 된다)
```

## 드래곤마다 다른 스킨 쓰기

`assets/dragons/<키>/dragon.txt` 의 `[dragon]` 구역에 한 줄 적는다.

```
ui_skin=revenant
```

**없는 조각은 자동으로 `default` 에서 가져온다.** 그래서 바꾸고 싶은 조각만 넣으면 된다.
예를 들어 체력 막대만 바꾸려면 `assets/ui/revenant/bar_fill_hp.png` 하나만 두면 된다.
`ui_skin` 을 적지 않으면 `default` 를 쓴다.

## 조각 이름

| 이름 | 쓰이는 곳 |
|---|---|
| `panel` | 창 배경(상태창·메뉴·모달) |
| `button` / `button_hover` / `button_off` | 보통 버튼의 세 가지 상태 |
| `pill` / `pill_hover` / `pill_off` | 좁은 자리의 작은 버튼 |
| `toggle_on` / `toggle_off` / `toggle_disabled` | 토글 스위치 |
| `bar_frame` | 막대 틀(빈 막대) |
| `bar_fill_hp` | 체력(초록) |
| `bar_fill_low` | 체력이 적을 때(빨강) |
| `bar_fill_exp` | 경험치(하늘) |
| `bar_fill_auto` | 자동 경험치(금색) |
| `square` / `square_off` | 아이콘용 작은 네모 버튼 (아직 코드에서 쓰지 않는다) |
| `check_on` | 체크 표시가 켜진 네모 (아직 코드에서 쓰지 않는다) |
| `icon_*` | 아이콘 (아직 코드에서 쓰지 않는다) |

**조각이 하나도 없어도 게임은 돈다.** 그림이 없으면 예전처럼 도형으로 그린다.

## ui.txt — 9조각 여백

버튼은 글자 길이에 따라 크기가 달라진다. 그림을 통째로 늘리면 모서리 장식이 찌그러지므로,
네 모서리는 그대로 두고 가장자리와 가운데만 늘린다(9조각). `ui.txt` 에 "늘리지 않을 테두리
두께"를 픽셀로 적는다.

```
[button]
left=70
top=40
right=70
bottom=40
```

적지 않은 조각은 늘리지 않고 가운데에 그대로 그린다.

## 그림 바꾸기

원본 시트에서 조각을 자를 때는 [tools/slice_ui.py](../../tools/slice_ui.py)를 쓴다.
자른 좌표가 스크립트 안에 표로 남아 있어서 같은 원본에서 다시 자를 수 있다.

```
python tools/slice_ui.py --out assets/ui/default --panels 버튼시트.png --bars 막대시트.png
```

## 원본 보관

자른 원본 시트는 [assets/references/ui/](../references/ui/)에 날짜 폴더로 둔다.
조각을 다시 자르거나 여백을 고칠 때 필요하다.
