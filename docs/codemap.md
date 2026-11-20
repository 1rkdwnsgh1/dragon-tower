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
| 망령룡 기본 명중·저주 비례 폭발·숨결 레벨별 이동 제한 | `src/core/World.cpp` | `updateCombat`, `damageEnemy`, `advanceBreath`, `hitBreathTick`, `updateEnemies` |
| 죽음의 행렬 발동·화면 전체 추적·충돌·연출 | `src/core/World.cpp`, `src/win/Renderer.cpp` | `castSkill` 의 Procession 분기, `advanceProcession`, `chooseProcessionTarget`, `advanceProcessionWaves`, `drawProcession` |
| 궁극기 전용 칸(키 5·클릭·자동 AI) | `include/core/Species.h`, `src/core/DragonData.cpp`, `src/core/World.cpp`, `src/core/Game.cpp`, `src/win/App.cpp` | `SkillDef::ultimate`·`ultimateUsesSouls`(`ultimate=`·`ultimate_resource=`), `World::ultimateSkill`·`ultimateBlockedReason`·`castUltimate`·`requestUltimate`·`setSlots`(궁극기 뺌), `dragonAi`(자동 AI 먼저 사용), `Game::useUltimate`·`equipSkill`, `PlayerInput::ultimate`(키 5), `UiAction::UseUltimate` |
| 망령룡 궁극기 충전 | `src/core/World.cpp`, `src/win/Renderer.cpp` | `ultimateCharge`, `canUseUltimate`, `beginUltimatePayment`·`commitUltimatePayment`·`cancelUltimatePayment`, `drawStatusCard` |
| 상태창 궁극기 줄·접기 버튼·탑 도전 버튼 | `src/win/Renderer.cpp` | `drawStatusCard`(영혼 또는 쿨타임 게이지 + 오른쪽 궁극기 아이콘, 접기 버튼 맨 왼쪽, 스킬 칸 아래 탑 도전), `drawSkills`(궁극기는 "전용 칸") |
| 클릭 통과(상호작용 없는 그림) | `src/win/Renderer.cpp` | `markRegion` 은 드래곤·창·버튼만 부른다. 효과·장판·투사체·숨결은 부르지 않는다. `panel(..., hit=false)` |
| 망령룡 저주 중첩 표시 | `src/win/Renderer.cpp` | `drawEnemy`가 배치하고 `drawCurseBadge`가 실제 적 상태를 그린다 |
| 망령룡 저주 중첩과 장판 간격 | `include/core/World.h`, `src/core/World.cpp` | `Enemy::curseStacks`, `applyCurse`, `updateCombat`의 투사체·장판 분기 |
| 드래곤 능력치·스킬 수치 | `assets/dragons/<키>/dragon.txt` | (코드 아님) |
| 내장 4종의 기본값 | `src/core/Species.cpp` | `builtinSpecies` |
| 전투 공통 수치(사거리, 회피, 회복 등) | `include/core/Config.h` | `cfg::k...` |
| 레벨 곡선, 자동 경험치 상한 | `src/core/Progression.cpp` | `expToNext`, `autoExpCap` |
| 종 추가·조회 방식 | `src/core/Species.cpp` | `registry`, `registerOrUpdateSpecies` |
| 적 종류·능력치·행동 | `assets/enemies/<키>/enemy.txt` | (코드 아님) |
| 탑 100층용 대기 그림(게임이 읽지 않음) | `assets/enemies/_tower/<키>/` | T4 에서 `assets/enemies/<키>/` 로 옮긴다([안내](../assets/enemies/_tower/README.md)) |
| 내장 적 4종의 기본값 | `src/core/Enemy.cpp` | `makeSlime`, `makeGoblin`, `makeOgre`, `makeLich` |
| 적 추가·조회 방식 | `src/core/Enemy.cpp` | `registerOrUpdateEnemy`, `defaultMelee`, `defaultBoss` |
| `enemy.txt` 읽는 규칙 | `src/core/DragonData.cpp` | `parseEnemyText`, `loadEnemyFolders` |
| 바탕화면 사냥에 나오는 적 | `src/core/Game.cpp` | `randomMelee` |
| 탑 층 구성(몇 층, 층마다 무슨 적) | `assets/tower/floors.txt` | (코드 아님. [사용법](../assets/tower/README.md)) |
| 탑 층의 내장 기본값(파일이 없을 때) | `src/core/Tower.cpp` | `builtinTowerFloors` |
| `floors.txt` 읽는 규칙 | `src/core/DragonData.cpp` | `parseTowerText`, `loadTowerFloors` |
| 층의 적을 실제로 세우는 곳 | `src/core/Game.cpp` | `spawnTowerFloor`, `towerFloorDef` |
| 탑 선택창·휠·스크롤바·선택층 입장 | `src/win/Renderer.cpp`, `src/win/App.cpp`, `src/core/Game.cpp` | `drawTowerSelection`, `drawSkillBox`, `dragTowerScroll`, `openTowerSelection`, `selectTowerFloor`, `enterTower` |
| 탑 UI 에셋 검수 캡처 | `src/win/Preview.cpp` | `captureTowerSelection` — 층 번호 선택 가능 |
| 층 수·층 이름을 화면에 쓰기 | `src/core/Game.cpp` | `towerFloorCount`, `towerFloorLabel` |
| 적 변종이 그림 빌리기 | `assets/enemies/<키>/enemy.txt` 의 `art_from=` | 읽기: `parseEnemyText`, 확인: `loadEnemyFolders` 끝의 뒷검사, 쓰기: `src/win/Sprites.cpp` `load` 의 `artKey` |
| 바탕화면·탑에 나오는 적 구성 | `src/core/Game.cpp` | `randomMelee`, `spawnTowerFloor` |
| 디펜스 전투 띠(맨 아래 영역) | `src/core/Game.cpp` | `battleBand` |
| 왼쪽에서 들어오는 웨이브 | `src/core/Game.cpp` | `spawnWave`, `waveNumber_` |
| 띠 높이·등장 위치·웨이브 증가 | `include/core/Config.h` | `kBandHeight`, `kSpawnOffsetX`, `kWaveLevelStep` |
| 전투 토글 스위치·모드 버튼 | `src/win/Renderer.cpp` | `toggle`, `drawStatusCard` |
| 클릭 통과 범위 판정 | `src/win/App.cpp` | `updateClickThrough`, `FrameRegions` (마우스는 항상 받는다) |
| 키보드 받기(자동) | `src/win/App.cpp` | `syncKeyboard`, `Game::wantsKeyboard` (싸우는 중 + 자동 아님) |
| 메뉴 탭·슬라이더·크기조절 | `src/win/Renderer.cpp` | `drawMenu`, `slider`, `UiAction::DragMenuSize` |
| 메뉴 열고 닫기 | `src/core/Game.cpp` | `setMenuOpen`, 상태창의 `≡` 버튼 |
| 띠로 날아 내려가기 | `src/core/World.cpp` | `beginConfine`, `confining_` |
| 입력 받을 영역 수집 | `src/win/Renderer.cpp` | `markRegion`, `g_regions` |
| 장판 그림과 기본 도형 | `src/win/Renderer.cpp`, `src/core/World.cpp` | `drawDotZones`, `DotZone::spriteSkill`, `fx_zone_sprite_only` |
| 트레이 아이콘·메뉴 | `src/win/App.cpp` | `createTray`, `showTrayMenu` |
| 트레이 풍선 알림 | `src/win/App.cpp` | `showBalloon`, `pumpAlerts` |
| 중요한 일 알림 모으기 | `src/core/Game.cpp` | `alert`, `takeAlert` |
| 탐색기 재시작 대응 | `src/win/App.cpp` | `taskbarCreatedMsg_` |
| 전역 단축키 | `src/win/App.cpp` | `RegisterHotKey`, `WM_HOTKEY` (Ctrl+Shift+D 메뉴 / Ctrl+Alt+D 숨기기) |
| 전부 숨기기 | `src/win/App.cpp` | `setUserHidden`, `userHidden_` (전체화면 숨김과 구분) |
| 작업표시줄 꾸미기 | `src/win/App.cpp` | `updateTaskbarSkin` — 작업표시줄 **바깥쪽**에 띠를 그린다(#77), 스킨의 `taskbar.png` |
| 창 끌기 범위 자르기 | `src/win/App.cpp` | `clampToDesktop`, `Renderer::lastCardRect/lastMenuRect` |
| 상태창·메뉴·드래곤 끌기 | `src/win/App.cpp` | `startDrag`, `WM_MOUSEMOVE` |
| 끄는 동안 자동 행동 멈춤 | `src/core/World.cpp` | `wander`의 `d.held`, `Game::setDragonHeld` |
| UI 스킨 읽기 | `src/win/Sprites.cpp` | `loadUiSkins`, `Sprites::ui` (스킨에 없으면 default) |
| UI 스킨 그리기(맞춤) | `src/win/Renderer.cpp` | `drawPieceRaw`(nine/height/contain), `Renderer::piece` |
| 그림이 너무 클 때 포기 | `src/win/Renderer.cpp` | `kBorderShare`, `kMinArtScale` → 도형으로 그린다 |
| 드래곤별 스킨 선택 | `include/core/Game.h` | `Game::uiSkin`, `dragon.txt`의 `ui_skin` |
| 스킨 설정 물려받기 | `src/win/Sprites.cpp` | `loadUiSkins` 끝의 `hasCfg` 보정 (default 의 여백·fit 를 쓴다) |
| 에셋 데이터 검사 | `tools/asset_check.cpp` | `ctest` 에 포함. dragon.txt·enemy.txt 오타를 잡는다 |
| 프레임 시간 측정(계산) | `include/core/FrameStats.h` | 단계별 평균·최대. 플랫폼 무관 |
| 프레임 시간 측정(시계·토글) | `src/win/FrameProfiler.{h,cpp}` | 트레이 메뉴로 켠다. 기본 꺼짐 |
| 프레임 측정 표시 | `Renderer::drawFrameStats` | 왼쪽 위 표. 켜 둔 동안만 |
| UI 조각 캐시 | `Renderer::drawPieceCached` | 장치 픽셀로 미리 그려 1:1 로 붙인다. `cfg::kUiPieceCache` |
| 그림 캐시(스프라이트·효과·장판·투사체·숨결 공통) | `src/win/FrameCache.{h,cpp}`, `Renderer::drawFrameCached` | 칸 자르기(`FrameCache::cell`), 뒤 스레드로 화면 크기 그림 만들기(`find`·`prefetch`·`beginFrame`), 대신 그리기·예전 그리기(`drawFrameDirect`), 장치 크기 계산(`frameDeviceSize`). `drawSprite`·`drawSheetFrame` 이 여기로 온다. 설정 `cfg::kFrameCache*` |
| 스킬 파일 유무 표 | `tools/asset_check.cpp` | 스킬마다 아이콘·본체 모션·효과 시트. 표 정렬은 `displayWidth`·`pad` (한글 두 칸) |
| Windows 코드를 리눅스에서 컴파일 | `tools/check_win_build.sh` | mingw-w64. 푸시 전에 9초. [설명](quality-tools.md) |
| 정적 분석(기준선과 비교) | `tools/check_static.py` | cppcheck + clang-tidy. 기준선은 `tools/static-baseline.txt` |
| 스프라이트 시트 격자·크기 검사 | `tools/check_sheets.py` | `--out` 으로 대조표 PNG. 격자 나누기와 그려지는 크기 |
| FX 합성 미리보기(본체+효과, 배율 3종) | `tools/fx_preview.py` | `--dragon` 또는 `--body`+`--fx`. [설명](quality-tools.md) |
| 파서 퍼저 | `tests/fuzz/` | `-DDRAGON_BUILD_FUZZERS=ON` (Clang). dragon.txt·enemy.txt·저장 파일 |
| 품질 CI | `.github/workflows/quality.yml` | 새니타이저·정적 분석·시트·커버리지. 퍼징은 주 1회 |
| 수치 시뮬레이션 | `tests/balance_sim.cpp` | `main` 에서 에셋을 먼저 읽는다. 탑 층별은 `floorsMode`·`floorsRun`·`climb`, 자동 사냥 생존은 `autoMode`·`autoRun`·`autoPrint` |
| 드래곤 보유·교체 | `src/core/Game.cpp` | `switchDragon`, `beginAddDragon`, `canSwitchDragon` |
| 보관 중 체력 회복 | `src/core/Game.cpp` | `Game::update` 앞부분, `cfg::kStableRegenRatio` |
| 보유 목록 저장 | `src/core/SaveData.cpp` | `stable.*` 키와 옛 저장 파일 변환 |
| 바탕화면 사냥 적 구성(T3) | `src/core/Game.cpp` | `huntFloor`, `huntPool`(직전 일반 층의 일반 적), `spawnWave`, 탑 돌파 기록 `recordFloorCleared` |
| 탑 100층 저장(T2) | `src/core/SaveData.cpp`, `include/core/Tower.h` | `stable.N.floor`·`cleared`, `credits`, `tower.firstClear`·`tower.firstClearFloors`·`fieldBossCleared`·`clearedFloors`, 슬롯 수 `stableCapacityFor`/`SaveData::stableCapacity` |
| 관리창 그리기 | `src/win/Renderer.cpp` | `drawStable` |
| 싸울 때 유지할 간격 | `src/core/World.cpp` | `desiredRange`, `Game::setKeepRange` |
| 쉴 때 엎드리기 | `src/core/World.cpp` | `updateDragonAnim`의 `restTime`, `cfg::kRestLieDelay` |
| 대쉬할 때 나는 모션 | `src/core/World.cpp` | `updateDragonAnim`의 `dashTime` |
| 모션 고르기(우클릭)와 대기 행동 | `src/core/Game.cpp`, `src/core/World.cpp`, `src/win/Renderer.cpp` | `setForcedMotion`, `updateDragonAnim`, `pickBehavior`, `drawMotionMenu` |
| 적 좌우 뒤집기 | `src/win/Renderer.cpp` | `drawEnemy`의 `artFacesRight` (`enemy.txt`의 `art_facing`, 읽는 곳은 `parseEnemyText`) |
| 스킬 아이콘 | `src/win/Sprites.cpp` | `skillicon_<종>_<번호>` ← `assets/dragons/<키>/icon_N.png` |
| 메뉴 창 위치·크기 | `src/win/Renderer.cpp` | `drawMenu`의 `menuScale`, `menuPos` |
| 캐릭터 보기 크기 | `src/win/Renderer.cpp` | `drawWorld`의 `cs`, `drawEnemy(charScale)` (그림만, 판정은 그대로) |
| 드래곤을 띠에 가두기 | `src/core/Game.cpp` | `setConfineToBand`, `placeDragon` |
| 조용할 때 프레임 줄이기 | `src/win/App.cpp` | `App::tick`의 `powerSave`·`busy`, `cfg::kIdleFrameInterval` |
| 얇은 영역에서 서는 자리 | `include/core/MathUtil.h`, `src/core/World.cpp` | `RectF::clampPoint`, `World::groundLine` |
| 자동 경험치 상한 켜고 끄기 | `include/core/Config.h` | `kAutoExpCapEnabled` |
| `dragon.txt` 읽는 규칙 | `src/core/DragonData.cpp` | `parseDragonText` |
| 폴더 스캔 | `src/core/DragonData.cpp` | `loadDragonFolders` |
| 전투 진행(피해, 회피, 스킬 발동) | `src/core/World.cpp` | `updateCombat`, `castSkill`, `damageDragon` |
| 스킬 해제 레벨 배정(레벨당 하나) | `src/core/Species.cpp` | `assignUnlockLevels` (`registerOrUpdateSpecies`가 항상 부른다) |
| 액티브·패시브 구분 | `include/core/Species.h`, `src/core/Species.cpp` | `SkillKind`, `skillIsPassive` |
| 패시브 능력치 보정 | `src/core/Species.cpp` | `statsFor` 뒤쪽의 `PassiveStat` |
| 처치 패시브(회복·영혼) | `src/core/World.cpp` | `damageEnemy`에서 확정한 처치를 `onEnemyKilled`가 처리 |
| 망령룡 영혼 보유량·초기화·표시 | `include/core/World.h`, `src/core/Game.cpp`, `src/win/Renderer.cpp` | `DragonActor::souls`, 전투 세션 전환, `drawStatusCard` |
| 장착 4칸(저장되는 값) | `include/core/Progression.h` | `DragonRecord::slots`, `fillEmptySkillSlots` |
| 장착 바꾸기·관리창 열기 | `src/core/Game.cpp` | `equipSkill`, `clearSlot`, `syncSkillSlots`, `openSkills` |
| 키 1~4와 자동 전투의 스킬 선택 | `src/core/World.cpp` | `slotSkill`, `slotReady`, `dragonAi`의 `castSkills` |
| 스킬 관리창 그리기 | `src/win/Renderer.cpp` | `drawSkills`, `drawSkillBox` |
| 스킬별 본체 모션 | `src/win/Sprites.cpp`, `src/win/Renderer.cpp` | `loadSkillSheets`/`skillSheet`, `drawDragon`의 `skillMotion` |
| 크기 비례 판정 | `src/core/World.cpp` | `scaledBody`, `World::skillRadius`, `setSizeScale`. `Game::setCharScale` 가 넣어 준다 |
| 모션·스킬 미리보기 도구 | `src/win/Preview.cpp` | `PreviewWindow`, `Renderer::drawAnimationPreview`, `Sprites::loadPreviewDragon` |
| 스킬 효과(FX) 설정 | `assets/dragons/<키>/dragon.txt` | `fx=`, `fx_front=`, `fx_scale=`, `fx_offset_x=`, `fx_offset_y=` ([사용법](../assets/dragons/README.md)) |
| 망령룡 효과 시트 | `assets/dragons/revenant/fx_*.png` (`pierce`·`claw`·`breath`·`harvest`) | 8~18프레임 256칸. 출처는 같은 폴더 `CREDITS.md` |
| 망령룡 효과 시트 | `assets/dragons/revenant/fx_*.png` (`pierce`·`claw`·`breath`·`bolt`·`bolt_hit`) | 8~9프레임 256칸. 출처는 같은 폴더 `CREDITS.md` |
| 망령룡 효과 시트 | `assets/dragons/revenant/fx_pierce.png`·`fx_claw.png`·`fx_breath.png`·`fx_fog.png` | 8·8·8·18프레임 256칸. 출처는 같은 폴더 `CREDITS.md` |
| 그림자룡 효과 시트 | `assets/dragons/shadow/fx_claw.png`·`fx_breath.png` | 9·11프레임 256칸. 출처는 같은 폴더 `CREDITS.md` |
| 스킬 효과(FX) 설정 | `assets/dragons/<키>/dragon.txt` | `fx=`, `fx_front=`, `fx_scale=`, `fx_offset_x=`, `fx_offset_y=`, `fx_alpha=` ([사용법](../assets/dragons/README.md)) |
| 망령룡 효과 시트 | `assets/dragons/revenant/fx_pierce.png`·`fx_claw.png`·`fx_breath.png` | 8프레임 256칸. 출처는 같은 폴더 `CREDITS.md` |
| 그림자룡 효과 시트 | `assets/dragons/shadow/fx_claw.png`·`fx_breath.png`·`fx_barrier.png` | 9·11·24프레임 256칸. 출처는 같은 폴더 `CREDITS.md` |
| 효과의 세 단계(발동·지속·잔류) | `src/core/World.cpp` | `Fx::phase`, `Fx::phaseProgress`, `addSkillFx` |
| 효과 단계 기본 길이·개수 상한 | `include/core/Config.h` | `kFxWindup`, `kFxHold`, `kFxTail`, `kMaxFx` |
| 효과를 도형으로 그리기 | `src/win/Renderer.cpp` | `drawFx` (시트가 없는 효과) |
| 레이어형 숨결 로드·렌더링 | `src/win/Sprites.cpp`, `src/win/Renderer.cpp` | `layered_breath` 에 지정한 효과 팩을 읽고 시작·지속·종료 레이어를 그린다 |
| 레이어형 숨결 합성 | `src/win/Sprites.cpp` | 읽을 때 층을 16장으로 합친다: `composeBreathFrame`(`BreathLayers` → `BreathSheets::frames`). 그리기는 그림 캐시를 탄다. 장착하면 미리 만들기: `Renderer::warmBreathCache`(`cfg::kBreathCacheSettleFrames`) |
| 레이어형 숨결 판정 | `src/core/DragonData.cpp`, `src/core/World.cpp` | `breath_defer_hit` 와 `breath_input_lock` 을 읽어 피해와 입력 잠금 시점을 처리한다 |
| 타격 프레임 스킬(`hit_frame=`) | `src/core/World.cpp` | `castSkill` 의 `usesTimedHit` 분기, `advanceSkillCast`(효과·피해 시점), `cancelSkillCast`, `spawnAnchoredFx`(`fx_anchor`→화면 좌표). 상태는 `World.h` 의 `SkillCast`, 키 읽기는 `DragonData.cpp`, 상수는 `cfg::kSkillCastRecover`·`kSkillCastCancelCooldown` |
| 발사체 효과 그리기(따라다님) | `src/win/Renderer.cpp` | 투사체 루프에서 `Projectile::skill` 로 스킬을 찾아 현재 위치에 시트를 그린다(반복 재생) |
| 효과를 스프라이트로 그리기 | `src/win/Renderer.cpp` | `drawFxSheet`(좌우 뒤집기·`fx_offset_x`·보정값×`charScale`), `fxPhaseLook`, `findSkillByFx` |
| 효과 시트 읽기 (`fx_<이름>.png`) | `src/win/Sprites.cpp` | `loadFxSheets`/`fxSheet` |
| 효과의 앞·뒤 층 나누기 | `src/win/Renderer.cpp` | `drawWorld` 가 `drawFx` 를 두 번 부른다(뒤→앞) |
| 스킬 칸 수·모션 유지 시간 | `include/core/Species.h`, `include/core/Config.h` | `kSkillSlots`, `cfg::kSkillMotionHold` |
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
| 탑 100층 구조(층 종류·자동사냥 기준 층·필드 보스 레벨 제한) | `src/core/Tower.cpp` | `floorType`, `huntFloorFor`, `fieldBossLevelCap`, `partyMeetsLevelCap` (수치는 `cfg::kTowerTopFloor` 등) |
| 전투를 시작할 때 싸울 자리(아래쪽에만 켬=띠, 끔=화면 전체) | `src/core/Game.cpp` | `enterBattleArea`, `setConfineToBand`, `spawnWave` (#47) |
| 캐릭터 그림을 발 기준으로 세우기 | `src/win/Renderer.cpp` | `drawWorld`의 `dcy`, `drawEnemy`의 `artY` (#49) |

## 파일 크기 (2026-11-14, v1.4)

큰 파일은 통째로 읽지 말고 위 표의 이름으로 찾아 들어간다.

| 파일 | 줄 수 |
|---|---|
| `src/win/Renderer.cpp` | ~1300 |
| `src/core/World.cpp` | ~720 |
| `src/win/App.cpp` | ~480 |
| `src/core/Game.cpp` | ~540 |
| `src/core/DragonData.cpp` | ~290 |
| 나머지 | 200줄 이하 |

## 확인 방법

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

Linux에서도 코어 규칙은 전부 빌드·테스트된다(`tests/core_tests.cpp`).
Windows 실행 파일은 GitHub Actions의 `build`가 만든다. 실제 창 동작은 사람이 직접 확인한다.
