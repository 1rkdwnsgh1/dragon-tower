# Dragon Tower (가제)

현재 버전: **v1.8** ([바뀐 점](docs/changelog.md))

Windows 바탕화면에서 드래곤을 육성하고 탑에 도전하는 게임입니다. 주 언어는 C++이며 게임 엔진은 사용하지 않습니다. 소리 없이 진행합니다.

- [통합 기획서](docs/game-design.md)
- [망령룡 캐릭터 기획: 확정 외형과 2D 제작 방향](docs/characters/revenant-dragon.md)
- [망령룡 스킬 기획 V1 — 콘셉트 제안·기술 설계 대기](docs/characters/revenant-dragon-skills-v1.md)
- [망령룡 제작 진행사항과 작업 자료 (2026-11-13)](docs/characters/revenant-dragon-progress.md)
- [개발 로드맵과 체크포인트](docs/roadmap.md)
- [구현 메모: 임시로 정한 사항과 확인할 것](docs/implementation-notes.md)
- [이미지 자료 규격과 생성 요청 예시](assets/README.md)
- [캐릭터 이미지·애니메이션·스킬 적용 기획 (드래곤 추가 구조)](docs/character-pipeline.md)
- [**드래곤 추가·그림 넣기 사용법**](assets/dragons/README.md)
- [**적 추가·그림 넣기 사용법**](assets/enemies/README.md)
- [버전별 바뀐 점](docs/changelog.md)
- [**코드 지도: 무엇이 어디에 있나**](docs/codemap.md)
- [AI 작업 비용 기록과 개선](docs/ai-workflow-notes.md)

## 지금 할 수 있는 것(첫 수직 단면)

- 드래곤 4종 중 하나를 골라 시작합니다. 이름과 수치는 임시안입니다.
- **Ctrl+Shift+D**로 게임 입력을 켜고 끕니다. 트레이 아이콘에서도 됩니다.
- 입력을 켜도 드래곤·패널 위에서만 클릭을 받고, 나머지는 바탕화면으로 통과합니다.
- 상태창은 제목 줄을 끌어 옮기고 오른쪽 버튼으로 접습니다. 드래곤도 끌어 옮길 수 있습니다.
- 드래곤이 죽거나 탑을 정복하면 트레이 알림으로 알려 줍니다. 다른 앱이 전체화면이라 화면을 못 봐도 전달됩니다.
- **드래곤 추가**: `assets/dragons/`에 폴더를 만들면 코드 수정 없이 새 드래곤이 생깁니다. 그림(스프라이트 시트)도 그 폴더에 넣습니다. → [사용법](assets/dragons/README.md)
- 드래곤과 몬스터는 다른 프로그램 창보다 항상 위에, 바탕화면 위에 떠서 움직입니다.
- 오른쪽 아래 **설정 버튼**으로 게임 입력을 켜고 끕니다. 꺼져 있으면 마우스·키보드가 다른 프로그램으로 그대로 갑니다.
- 다른 앱이 전체화면이 되면 드래곤, 몬스터, 설정 버튼을 숨기고 끝나면 다시 표시합니다. 자동 사냥은 숨겨진 동안에도 계속됩니다.
- 사냥 모드 3가지: 직접 조작(WASD 이동, Space 회피, 1~3 스킬), 스킬 수동(1~3 스킬, 이동과 회피 판정은 자동), 자동 사냥.
- 자동 사냥은 시작 전에 경고하고, 개체별 누적 경험치가 Lv.5 도달에 필요한 총 경험치에 닿으면 멈춥니다.
- 레벨업으로 능력치가 오르고 Lv.3, Lv.6에 새 스킬을 배웁니다.
- 탑 3층(일반 적 → 일반 적 → 보스). 탑에서는 자동 사냥을 쓸 수 없습니다.
- 영구 사망: 쓰러진 드래곤은 저장 즉시 사용할 수 없게 되고 새 드래곤으로 다시 시작합니다. 탑 최고 기록과 도감은 남습니다.
- 저장 위치: `%APPDATA%\DragonTower\save.txt`

## 실행

GitHub의 **Actions** 탭 → 최근 `build` 실행 → 아래쪽 **Artifacts**의 `DragonTower-windows`를 받아 압축을 풀고 `dragon_tower.exe`를 실행합니다. `assets` 폴더는 실행 파일 옆에 두어야 합니다.

## 직접 빌드

Windows, C++17, CMake 3.20 이상, Visual Studio 2019 이상(데스크톱 C++ 워크로드)이 필요합니다. 그래픽은 Windows에 기본으로 들어 있는 Win32 API와 GDI+만 사용하므로 별도 라이브러리를 설치하지 않습니다.

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

실행 파일은 `build/Release/dragon_tower.exe`에 생기고 `assets` 폴더가 옆에 복사됩니다.

## 폴더 구조

- `include/core`, `src/core`: 게임 규칙(성장, 전투, 탑, 저장). Windows에 의존하지 않아 다른 환경에서도 테스트할 수 있습니다.
- `src/win`: 바탕화면 투명 창, 설정 버튼, 전체화면 감지, 그리기와 입력.
- `assets/dragons/<키>/`: 드래곤별 데이터 파일(`dragon.txt`)과 스프라이트 시트. 게임이 켜질 때 읽습니다.
- `assets/`: 그 밖의 이미지 자료. 없으면 임시 도형으로 표시합니다.
- `tools/make_sheet.py`: 낱장 프레임이나 다른 규격의 그림을 시트 규격으로 변환합니다.
- `tests/core_tests.cpp`: 핵심 규칙 검증(자동 경험치 상한, 영구 사망, 저장, 모드 규칙 등).
- `tests/balance_sim.cpp`: 임시 수치 점검용 시뮬레이션.

## 수치 바꾸기

드래곤별 능력치와 스킬은 **`assets/dragons/<키>/dragon.txt`에서 코드 수정 없이** 바꿀 수 있습니다(기본값은 `src/core/Species.cpp`). 전투 공통 수치는 `include/core/Config.h`, 레벨 곡선은 `src/core/Progression.cpp`에 있습니다.
