# 적 폴더 사용법 (v0.4)

적 하나 = 폴더 하나입니다. 게임은 켤 때 이 폴더 안의 하위 폴더를 모두 읽어 적 목록을 만듭니다.
드래곤([assets/dragons](../dragons/README.md))과 같은 방식이고, 파일 이름만 `enemy.txt`로 다릅니다.

## 폴더 안에 넣는 것

| 파일 | 내용 | 없으면 |
|---|---|---|
| `enemy.txt` | 이름·역할·능력치·행동 | 내장 4종은 기본값 사용. 새 적은 등록되지 않음 |
| `idle.png` | 대기 시트 | 임시 도형 |
| `move.png` | 이동 시트 | `idle.png` |
| `attack.png` | 공격 예고 중에 쓰는 시트 | `idle.png` |
| `hit.png` | 피격 시트 | `idle.png` + 깜빡임 |
| `death.png` | 사망 시트 | `idle.png` + 사라지는 효과 |
| `CREDITS.md` | 제작자·출처·라이선스 | (직접 적어 주세요) |

시트 규격은 드래곤과 같습니다: 가로 한 줄, 칸 256×256, 발이 (128, 240), 오른쪽 지향.
자세한 내용은 [캐릭터 적용 기획](../../docs/character-pipeline.md)을 보세요.

## enemy.txt 에 적는 것

```
[enemy]
name=고블린
role=melee          ; melee(일반) 또는 boss
desc=빠르게 파고들어 찌른다.
color=FF9FBF5A

hp=40               ; 1레벨 기준값
atk=7
def=1
speed=95
radius=24           ; 충돌·그리기 크기
exp=3               ; 처치 보상

hp_growth=15        ; 레벨당 증가량
atk_growth=2.5
def_growth=0.5
speed_growth=3.5
exp_growth=2

attack_range=72     ; 이 거리 안이면 공격한다
keep_ratio=0.8      ; attack_range * 이 값보다 멀면 다가간다
windup=0.8          ; 공격 예고 시간(초). 0이면 기본값
attack_cooldown=2.2

; 보스만 쓰는 값
phase2=yes          ; 체력 절반에서 2페이즈
summon=goblin       ; 그때 부를 적의 키
summon_count=2
summon_level=5
```

애니메이션은 드래곤과 같습니다(`[anim.idle]` 의 `fps` `loop` `frame_w` `render_scale`).

## 그림을 빌려 변종 만들기 (v1.6)

행동과 수치만 다른 변종은 **그림을 복사하지 말고 `art_from=` 으로 원본 폴더를 가리킵니다.**

```
[enemy]
name=정예 슬라임
art_from=slime      ; assets/enemies/slime 의 시트 다섯 장을 그대로 읽는다
speed=132           ; 그림만 빌리고 수치·행동은 여기서 따로 정한다
```

- **시트 파일만 빌립니다.** `[anim.*]` 의 `frame_w`·`render_scale`·`fps` 는 **빌리지 않으므로
  변종의 `enemy.txt` 에 다시 적어야 합니다.** 원본을 고칠 때 변종이 따라 흔들리지 않게 한 것입니다.
- 폴더 전체를 가리킵니다. 다섯 장 중 일부만 빌릴 수는 없습니다.
- **한 다리만 건넙니다.** 가리킨 쪽이 또 `art_from` 을 쓰면 따라가지 않고 경고합니다.
- 가리킨 폴더가 없으면 경고하고 도형으로 그립니다(조용히 넘어가지 않습니다).
- 변종만의 그림이 생기면 `art_from` 줄을 지우고 다섯 장을 그 폴더에 넣습니다.

보기: [`elite-slime`](elite-slime/) · [`elite-goblin`](elite-goblin/) · [`elite-lich`](elite-lich/).

## 그림이 어느 쪽을 보는가

`art_facing=left|right` 로 적습니다. 기본은 `left` 입니다(지금 적 그림은 모두 왼쪽을 봅니다).

## 새 적 추가하기

1. 폴더를 만듭니다(영문 소문자·숫자·`-`·`_`). `_`로 시작하면 읽지 않습니다.
2. `enemy.txt`를 쓰고 `idle.png`를 넣습니다.
3. 게임을 켜서 크기와 움직임을 확인합니다.
4. 나머지 시트와 `CREDITS.md`를 채웁니다.

**폴더 이름(키)은 저장 파일의 도감에 기록됩니다. 한 번 정하면 바꾸지 마세요.**

`role=melee`인 적은 바탕화면 사냥에 무작위로 섞여 나오고, 탑 층 구성은 `src/core/Game.cpp`의
`spawnTowerFloor`가 키로 찾아 씁니다(`slime` 1층, `goblin`·`ogre` 2층, 보스 3층).
**폴더를 더하면 그만큼 바탕화면 사냥에 나오는 적이 늘어납니다**(`role=boss`는 섞이지 않습니다).

## 잘못 적으면 어떻게 되나요

게임을 켤 때 화면 알림으로 어느 파일 몇 행이 잘못됐는지 알려 줍니다. 허용 범위를 벗어난 값도
범위로 맞춘 뒤 알려 줍니다. 잘못된 값이 있어도 게임은 계속 실행됩니다.
