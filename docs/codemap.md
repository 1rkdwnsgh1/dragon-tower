# 코드 지도 — 무엇이 어디에 있는가

**작업을 시작하기 전에 이 문서부터 읽는다.** 파일을 뒤져서 위치를 찾지 말고 여기서 찾는다.
코드를 바꾸면 이 문서도 같은 PR에서 고친다.

## 전체 구조

```
include/core/, src/core/   게임 규칙. Windows에 의존하지 않아 어디서든 빌드·테스트된다.
src/win/                   Windows 전용. 창, 그리기, 입력. (진입점: src/win/main.cpp)
assets/dragons/<키>/       드래곤 데이터와 그림. 코드가 아니라 여기서 바꾼다.
tools/                     에셋 변환 스크립트
tests/                     코어 규칙 검증
```

핵심 원칙: **core는 win을 모른다.** 규칙 변경은 core에서, 보이는 것 변경은 win에서 한다.

## "그걸 고치려면 어디를 봐야 하나"

| 바꾸려는 것 | 파일 | 찾을 이름 |
|---|---|---|
| 드래곤 능력치·스킬 수치 | `assets/dragons/<키>/dragon.txt` | (코드 아님) |
| 내장 4종의 기본값 | `src/core/Species.cpp` | `builtinSpecies` |
| 전투 공통 수치(사거리, 회피, 회복 등) | `include/core/Config.h` | `cfg::k...` |
| 레벨 곡선, 자동 경험치 상한 | `src/core/Progression.cpp` | `expToNext`, `autoExpCap` |
| 종 추가·조회 방식 | `src/core/Species.cpp` | `registry`, `registerOrUpdateSpecies` |
| 적 종류·능력치·행동 | `assets/enemies/<키>/enemy.txt` | (코드 아님) |
| 내장 적 4종의 기본값 | `src/core/Enemy.cpp` | `makeSlime`, `makeGoblin`, `makeOgre`, `makeLich` |
| 적 추가·조회 방식 | `src/core/Enemy.cpp` | `registerOrUpdateEnemy`, `defaultMelee`, `defaultBoss` |
| `enemy.txt` 읽는 규칙 | `src/core/DragonData.cpp` | `parseEnemyText`, `loadEnemyFolders` |
| 바탕화면·탑에 나오는 적 구성 | `src/core/Game.cpp` | `randomMelee`, `spawnTowerFloor` |
| 디펜스 전투 띠(맨 아래 영역) | `src/core/Game.cpp` | `battleBand` |
| 왼쪽에서 들어오는 웨이브 | `src/core/Game.cpp` | `spawnWave`, `waveNumber_` |
| 띠 높이·등장 위치·웨이브 증가 | `include/core/Config.h` | `kBandHeight`, `kSpawnOffsetX`, `kWaveLevelStep` |
| 전투 토글 스위치·모드 버튼 | `src/win/Renderer.cpp` | `toggle`, `drawStatusCard` |
| 클릭 통과 범위 판정 | `src/win/App.cpp` | `updateClickThrough`, `FrameRegions` |
| 입력이 꺼져도 받는 영역 | `src/win/App.cpp` | `cursorOnAlways`, `UiButton::always` |
| 상시 입력 영역 표시 | `src/win/Renderer.cpp` | `g_always`, `AlwaysGuard` (상태창을 그리는 동안만 켠다) |
| 입력 받을 영역 수집 | `src/win/Renderer.cpp` | `markRegion`, `g_regions` |
| 트레이 아이콘·메뉴 | `src/win/App.cpp` | `createTray`, `showTrayMenu` |
| 트레이 풍선 알림 | `src/win/App.cpp` | `showBalloon`, `pumpAlerts` |
| 중요한 일 알림 모으기 | `src/core/Game.cpp` | `alert`, `takeAlert` |
| 탐색기 재시작 대응 | `src/win/App.cpp` | `taskbarCreatedMsg_` |
| 전역 단축키 | `src/win/App.cpp` | `RegisterHotKey`, `WM_HOTKEY` |
| 상태창·메뉴·드래곤 끌기 | `src/win/App.cpp` | `startDrag`, `WM_MOUSEMOVE` |
| 끄는 동안 자동 행동 멈춤 | `src/core/World.cpp` | `wander`의 `d.held`, `Game::setDragonHeld` |
| UI 스킨 읽기 | `src/win/Sprites.cpp` | `loadUiSkins`, `Sprites::ui` (스킨에 없으면 default) |
| UI 스킨 그리기(9조각) | `src/win/Renderer.cpp` | `drawNineSlice`, `Renderer::piece` |
| 드래곤별 스킨 선택 | `include/core/Game.h` | `Game::uiSkin`, `dragon.txt`의 `ui_skin` |
| 싸울 때 유지할 간격 | `src/core/World.cpp` | `desiredRange`, `Game::setKeepRange` |
| 쉴 때 엎드리기 | `src/core/World.cpp` | `updateDragonAnim`의 `restTime`, `cfg::kRestLieDelay` |
| 메뉴 창 위치·크기 | `src/win/Renderer.cpp` | `drawMenu`의 `menuScale`, `menuPos` |
| 캐릭터 보기 크기 | `src/win/Renderer.cpp` | `drawWorld`의 `cs`, `drawEnemy(charScale)` (그림만, 판정은 그대로) |
| 드래곤을 띠에 가두기 | `src/core/Game.cpp` | `setConfineToBand`, `placeDragon` |
| 조용할 때 프레임 줄이기 | `src/win/App.cpp` | `App::tick`의 `powerSave`·`busy`, `cfg::kIdleFrameInterval` |
| 얇은 영역에서 서는 자리 | `include/core/MathUtil.h`, `src/core/World.cpp` | `RectF::clampPoint`, `World::groundLine` |
| 자동 경험치 상한 켜고 끄기 | `include/core/Config.h` | `kAutoExpCapEnabled` |
| `dragon.txt` 읽는 규칙 | `src/core/DragonData.cpp` | `parseDragonText` |
| 폴더 스캔 | `src/core/DragonData.cpp` | `loadDragonFolders` |
| 전투 진행(피해, 회피, 스킬 발동) | `src/core/World.cpp` | `updateCombat`, `castSkill`, `damageDragon` |
| 적 행동과 공격 예고 | `src/core/World.cpp` | `updateEnemies`, `resolveZone` |
| 애니메이션 상태 전환 | `src/core/World.cpp` | `updateDragonAnim`, `setDragonAnim` |
| 바탕화면 자동 행동(쉬기·걷기·날기·착지) | `src/core/World.cpp` | `pickBehavior`, `wander`, `randomRoamTarget` |
| 상태별 그리기 크기 보정 | `src/win/Renderer.cpp` | `drawDragon`의 `render_scale` |
| 자동 행동 확률·시간·속도 | `include/core/Config.h` | `cfg::kIdle...` |
| 화면 흐름(선택→사냥→탑→사망) | `src/core/Game.cpp` | `Game::update`, `Screen` |
| 영구 사망 처리 | `src/core/Game.cpp` | `onDragonDied` |
| 저장 형식 | `src/core/SaveData.cpp` | `serializeSave`, `deserializeSave` |
| 드래곤·적 그리기 | `src/win/Renderer.cpp` | `drawDragon`, `drawEnemy` |
| 시트 한 칸 그리기 | `src/win/Renderer.cpp` | `drawSheetFrame` |
| 선택 화면, 도감, 경고창 | `src/win/Renderer.cpp` | `drawSelect`, `drawCodex`, `drawModal` |
| 메뉴 버튼과 상태 카드 | `src/win/Renderer.cpp` | `drawMenu`, `drawStatusCard` |
| 이미지·시트 읽기 | `src/win/Sprites.cpp` | `load`, `loadSheetsFor` |
| 창 만들기, 입력 전환, 메인 루프 | `src/win/App.cpp` | `App::init`, `setInputEnabled`, `tick` |
| 전체화면 감지, 저장 경로, DPI | `src/win/Platform.cpp` | `isOtherAppFullscreen`, `saveFilePath` |

## 파일 크기 (2026-11-13)

큰 파일은 통째로 읽지 말고 위 표의 이름으로 찾아 들어간다.

| 파일 | 줄 수 |
|---|---|
| `src/win/Renderer.cpp` | ~1060 |
| `src/core/World.cpp` | ~680 |
| `src/win/App.cpp` | ~480 |
| `src/core/Game.cpp` | ~420 |
| `src/core/DragonData.cpp` | ~290 |
| 나머지 | 200줄 이하 |

## 확인 방법

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

Linux에서도 코어 규칙은 전부 빌드·테스트된다(`tests/core_tests.cpp`, 147건).
Windows 실행 파일은 GitHub Actions의 `build`가 만든다. 실제 창 동작은 사람이 직접 확인한다.
