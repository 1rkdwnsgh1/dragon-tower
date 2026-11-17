# 탑 100층용 대기 그림

**게임은 이 폴더를 읽지 않는다**(`_` 로 시작하는 폴더는 건너뛴다 — `loadEnemyFolders`).
바탕화면 사냥에 아직 나오면 안 되는 몬스터와 보스 그림을 T4(탑 100층) 전까지 여기 둔다(사용자 결정, 2026-11-16).

## 쓰는 법

T4 에서 층에 배치할 때 폴더를 `assets/enemies/<키>/` 로 옮기고 `enemy.txt` 와 `CREDITS.md`(이 폴더의 것을 나눠서)를 넣는다.
**폴더 이름이 곧 키이고 도감에 기록되므로 옮길 때 이름을 바꾸지 않는다.**

시트는 이미 게임 규격이다: 가로 한 줄 12프레임, 칸 256×256, 발 (128, 240), **오른쪽 지향**(`art_facing=right` 필요).

## 들어 있는 것

| 구분 | 키 |
|---|---|
| 일반 몬스터 17종 | `wolf` `treant` `yeti` `frost-spirit` `ice-golem` `skeleton` `orc` `lizardman` `hydra` `water-dragon` `fire-elemental` `basilisk` `lava-giant` `wraith` `death-knight` `chimera` `abyss-lord` |
| 중간보스 9종 | `goblin-chief` `alpha-wolf` `giant-yeti` `orc-warlord` `swamp-witch` `volcanic-smith` `black-knight-captain` `basilisk-queen` `abyss-gatekeeper` (35층은 기존 `lich`) |
| 필드보스 10종 | `slime-king` `ancient-world-tree` `frost-queen` `bone-dragon` `great-hydra-body` `leviathan` `volcano-lord` `king-of-dead` `chimera-lord` (100층은 `abyss-lord` 와 따로 그린 큰 그림 — 아래) |
| 보스 부품 3종 | `world-tree-fruit`(세계수 열매) `hydra-head`(히드라 머리) `leviathan-tentacle`(리바이어던 촉수) |

지역·보스 배치는 [보스 구성안](../../../docs/tower-100-bosses.md).

## 알아 둘 것

- **`abyss-lord` 가 두 개다.** 일반 몬스터 묶음의 심연 군주와 보스 묶음의 심연 군주(100층)가 같은 이름이다. 지금 폴더의 `abyss-lord` 는 **보스 묶음 것**이고, 일반 몬스터 묶음 것은 `abyss-lord` 대신 쓰지 않는다(구성안에서 심연 군주는 보스로 올렸다).
- **해상도**: 요청은 칸 512px(6144×2560)였지만 받은 원본은 일반 몬스터와 같은 약 1942×809(칸 약 162px)다. 256 칸에 맞추며 1.0~1.5배 확대했다. 보스를 화면에서 크게 그리면 흐릴 수 있다.
- **부품**은 따로 움직이는 개체로 쓰는 초안이다. 본체와 연결 위치·크기·판정은 아직 없다(T4 전에 정한다). 히드라 본체는 머리 없이 그려져 있다.
- `black-knight-captain`·`abyss-lord`·`leviathan-tentacle` 은 무기·효과가 길어 칸 안에서 작게 들어갔다. 옮길 때 `render_scale` 로 키운다.
- 공격 줄 앞 1~4프레임이 예고 자세로 생성됐지만 실제 판정 프레임(`hit_frame`)은 재생해 보고 정한다.
