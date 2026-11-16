# 자동 작업 큐 — 망령룡 스킬 이펙트 적용 (2026-11-16 밤)

예약된 AI 세션이 **1시간마다** 돌면서 이 문서의 항목을 위에서부터 하나씩 집어 간다.
**매 회차는 이전 기억이 없는 새 세션이다.** 진행 상태는 전부 이 문서에만 있다.

**시작 2026-11-16 22:00 / 종료 2026-11-17 08:00 (KST).** 종료 시각 이후에 깨어난 회차는
아무것도 하지 말고 끝낸다.

사람이 2026-11-16 21:00에 결정한 내용으로 만들었다. 결정 과정은 이 문서 맨 아래 "정해진 것"에 있다.

---

## 회차 절차

1. **최신 받기.** `git fetch origin main && git checkout -B work origin/main`.
   팀원 3명이 같은 저장소에서 작업한다 — 매번 받는다.
2. **읽기.** 이 문서 전체. 그리고 [`docs/codemap.md`](codemap.md)에서 고칠 곳의 위치만 찾는다.
   `Renderer.cpp`·`World.cpp`를 통째로 읽지 않는다.
3. **직전 회차 재검증이 먼저다.** 아래 "검증" 5단계를 **직전 회차가 머지한 것에** 돌린다.
   깨진 게 있으면 **새 항목을 집지 않고 그것을 먼저 고친다.**
4. **집기.** `대기` 항목 중 의존이 풀린 **가장 위** 하나. 집는 즉시 상태를
   `진행 중 (HH:MM)`으로 바꿔 **먼저 커밋·푸시한다**(상태 줄만 main에 직접 — 다음 회차와
   겹치지 않기 위한 이 큐의 기존 방식이다. 코드·데이터는 반드시 브랜치+PR).
   - `진행 중`인데 시작 시각이 **70분**(1시간 주기이므로 짧게) 넘게 지났으면 중단된 것으로 보고 다시 집는다.
   - 대기 항목이 없으면 "큐가 비었다"고만 보고하고 끝낸다. **새 항목을 스스로 만들지 않는다.**
5. **하기.** 항목의 "할 일"만 한다. 범위를 넓히지 않는다.
6. **검증.** 아래 "검증" 5단계 전부 통과해야 한다.
7. **올리기.** 브랜치 + PR. **브랜치는 이 큐 전체에서 3개까지만 만든다**(아래 표의 세 개).
   CI(Windows·Linux)가 통과하면 **머지한다** — `win` 분류도 이번 작업에 한해 사람이 허용했다.
   PR 본문에 `자동 세션이 머지함 — 사후 검토 필요`를 적는다.
8. **기록.** 같은 PR에서 [`docs/changelog.md`](changelog.md),
   [`docs/codemap.md`](codemap.md)(코드를 바꿨으면),
   [`docs/implementation-notes.md`](implementation-notes.md)(임시값·뜻이 바뀐 값·Windows에서
   확인할 것이 생겼으면)를 고치고, 이 문서의 상태를 `완료 (PR #n)`으로 바꾼다.
9. **보고.** 3줄 이내. 한 항목, PR 번호, 막힌 것.

### GitHub Actions 가 멈춰 있다 (2026-11-16 21:30~)

**CI 가 작업을 시작하지 못한다.** `build`·`quality` 두 워크플로의 job 이 **step 0개로 실패**하고
로그는 `BlobNotFound` 404 로 나오지 않는다. **코드 문제가 아니다** — 같은 커밋에서 로컬
`cmake && build && ctest` 는 통과한다(21:35 확인). 21:05(#63)·21:10(#65) 머지 커밋은 셋 다
통과했으므로 그 사이에 Actions 쪽이 바뀌었다. 원인은 **Actions 사용량·저장 공간 한도로 추정**하지만
(이 저장소는 #58·#60·#61 에서 이미 저장 공간 초과를 겪었고 #63 이 약 102MB 를 더했다)
과금 정보를 자동 세션이 조회할 수 없어 **확정이 아니다.** 사람이 아침에 고친다.

그동안 회차는 이렇게 한다:

- **CI 결과를 받을 수 없으면 머지하지 않는다.** PR 은 열어 두고 이 문서의 상태를
  `완료 — CI 불가, 머지 대기 (PR #n)` 로 적는다. 다음 회차는 그 항목을 다시 집지 않고 다음으로 간다.
- **판별법**: `gh api repos/{owner}/{repo}/commits/{sha}/check-runs` 가 `failure` 인데
  `gh api .../actions/runs/{id}/jobs` 의 `steps` 가 **0개**면 코드 실패가 아니다.
- **검증은 로컬에서 한다.** 아래 "검증" 절의 5단계를 그대로 돌린다. CI 를 대신하는 것은 이것뿐이다.
- **CI 나 `.github/workflows/` 를 고치려 들지 말 것.** 한도·과금 문제일 수 있고, 자동 세션이
  워크플로를 손대면 사람이 원인을 못 찾는다.
- Actions 가 정상으로 돌아왔으면(step 이 실제로 실행되고 통과) 원래 규칙대로 머지한다.
  밀린 PR 은 **표의 순서대로**(1 → 2 → 3) 머지해야 한다 — 뒤 항목이 앞 항목에 의존한다.

- **2026-11-17 01:10 회차 확인**: Actions 는 여전히 멈춰 있다(`build`·`quality` 모두 step 0개 실패,
  run 43893911426/43893911244). 머지하지 않았다. PR #69·#70·#71 은 그대로 열려 있다.
  `assets/revenant-skill-fx`(#69·#70 포함) 기준으로 **검증 5단계 전부 로컬 통과**:
  ctest 2/2, `check_win_build.sh` 문법 통과(mingw-w64 를 이 회차에서 설치해야 했다 — 세션마다 없다),
  `check_sheets.py` 시트 7개 문제 없음, `asset_check` 경고 없음,
  `fx_preview.py` 세 배율(0.5/1.0/10.0) 열 3개가 동일 — `charScale` 곱이 들어가 있고 좌우 뒤집기도 맞다.
  **대기 항목이 없다. 큐는 비었다.**

- **2026-11-17 02:10 회차 확인**: Actions 여전히 멈춤(run 43903541821 `build`: core-linux·windows 둘 다
  step 0개 실패 / 43903541114 `quality`: quality step 0개 실패). 머지하지 않았다. PR #69·#70·#71 그대로 열림.
  `assets/revenant-skill-fx` 기준 **검증 5단계 전부 로컬 통과**: ctest 2/2, `check_win_build.sh` 문법 통과
  (mingw-w64 는 이 회차에서도 다시 설치해야 했다 — `apt-get update` 먼저), `check_sheets.py` 시트 7개 문제 없음,
  `asset_check` 경고 없음, `fx_preview.py` 출력에서 claw·pierce·breath 3개 x 좌우 2방향 모두 0.5/1.0/10.0
  세 열이 동일 — `charScale` 곱과 좌우 뒤집기 정상. **대기 항목 없음, 큐는 비었다.**

- **2026-11-17 03:05 회차 확인**: Actions 여전히 멈춤(run 43914503268 `quality`: quality step 0개 실패 /
  43914503199 `build`: core-linux·windows 둘 다 step 0개 실패). 머지하지 않았다. PR #69·#70·#71 그대로 열림.
  `assets/revenant-skill-fx` 기준 **검증 5단계 전부 로컬 통과**: ctest 2/2, `check_win_build.sh` 문법 통과
  (mingw-w64 는 이번에도 세션에 없어 다시 설치했다 — 세 회차 연속이다), `check_sheets.py` 시트 7개 문제 없음,
  `asset_check` 경고 없음, `fx_preview.py` 출력(1266x2108)을 직접 열어 확인 — claw·pierce·breath 3개 x 좌우 2방향
  6행 모두 0.5/1.0/10.0 세 열의 그림이 동일하고, 왼쪽 지향 행에서 FX 가 본체 반대편으로 뒤집혀 있다.
  `charScale` 곱과 좌우 뒤집기 정상. **대기 항목 없음, 큐는 비었다.**

**이 문서의 이 절과 PR #66 자체는 CI 없이 main 에 들어갔다**(사람이 2026-11-16 21:38 허용).
문서 변경이라 로컬 검증으로 갈음했다. 코드·데이터 항목에는 이 예외를 적용하지 않는다.

### 중단 조건

**같은 항목이 2회차 연속 실패하면 멈춘다.** 상태를 `막힘 — 사람 확인 필요`로 바꾸고 무엇이
어떻게 실패했는지 적는다. 그 항목을 다시 집지 않고, **다음 항목으로 넘어가지도 않는다**(순서에
의존이 있다). 남은 회차는 "막혀 있다"고만 보고하고 끝낸다.

---

## 작업 항목

| # | 상태 | 분류 | 브랜치 | 할 일 | 의존 |
|---|---|---|---|---|---|
| 0 | **완료** (PR #63, 2026-11-16 21:05) | 에셋 | — | 원본 이미지 main 병합 | — |
| 1 | **완료 — CI 불가, 머지 대기** (PR #69) | 도구 | `tools/fx-preview` | FX 합성 미리보기 도구 | 0 |
| 2 | **완료 — CI 불가, 머지 대기** (PR #70) | win | `feat/fx-facing-and-offset` | FX 좌우 뒤집기 + `fx_offset_x` + **보정값에 `charScale` 곱하기** | 1 |
| 3 | **완료 — CI 불가, 머지 대기** (PR #71) | 데이터 | `assets/revenant-skill-fx` | 11/15 결정 적용 + FX 시트 3장 + `fx=` 배선 | 2 |

### 1번 — FX 합성 미리보기 도구 (`tools/fx_preview.py`)

이 환경에서는 `src/win`을 **실행할 수 없다**(컴파일만 된다). 그래서 이펙트의 크기·위치·방향이
맞는지 볼 유일한 수단이 이 도구다. **2번·3번의 검증이 전부 여기에 매여 있으니 먼저 만든다.**

할 일:
- 입력: 본체 모션 시트(`assets/dragons/<키>/skill_*.png` 또는 `attack.png`), FX 시트,
  `fx_scale`·`fx_offset_x`·`fx_offset_y`·`fx_render_scale`, 그리고 `--char-scale`.
- `Renderer::drawFxSheet`와 **같은 계산**을 쓴다. 베끼지 말고 그 함수를 읽고 맞춘다:
  - 기준 높이 `base = cfg::kDragonRadius * 3 * charScale` (`kDragonRadius`는 `include/core/Config.h`)
  - FX 높이 `h = base * fx_scale * fx_render_scale`
  - 중심 `cx = a.x + fx_offset_x * charScale * dirSign`, `cy = a.y + fx_offset_y * charScale`
    — **2번이 끝나기 전에는 `charScale` 곱이 코드에 없다.** 도구는 **고쳐진 쪽 기준**으로 만들고,
    1번 회차에서는 "지금 코드는 이렇게 안 한다"를 도구 주석과 PR 본문에 적는다.
- 출력: 한 장의 PNG. **`--char-scale 0.5,1.0,10.0` 세 배율을 가로로 나란히** 붙이고 배율을 글자로 적는다.
  세 배율 전부에서 위치가 맞는지 보는 것이 이 도구의 목적이다.
- 좌우 양쪽을 본다: 오른쪽 지향 1장 + 뒤집은 1장.
- `tools/preview_ui.py`의 기존 방식(인자·출력 경로·한국어 메시지)을 따라간다.
- `docs/quality-tools.md`에 쓰는 법을 한 절 더한다.

### 2번 — FX 좌우 뒤집기 + 앞으로 밀기 (`src/win/Renderer.cpp`)

고칠 곳: `Renderer::drawFxSheet` (`src/win/Renderer.cpp`, 962행 근처).

```cpp
// 보정값은 "100% 기준 px"다 — 그려지는 본체 크기에 맞추려면 charScale 을 곱한다.
// (안 곱하면 캐릭터 크기 10배에서 머리 위 보정이 몸통에 파묻힌다. kCharScaleMax = 10.0)
float dirSign = (f.b.x < f.a.x) ? -1.f : 1.f;
float cx = f.a.x + f.offsetX * charScale * dirSign;
float cy = f.a.y + f.offsetY * charScale;
...
drawSheetFrame(g, sh, animFrame(...), cx, cy, h, /*flip=*/dirSign < 0.f, 0.f, alpha);
```

- 뒤집기는 **드래곤의 `facing`을 쓰지 않는다.** `Fx`에 발동점 `a`와 끝점 `b`가 이미 있어
  방향을 거기서 뽑는다(core를 건드리지 않는다).
- `drawSheetFrame`에 **이미 flip 인자가 있다** — 지금 `false`로 고정되어 있을 뿐이다.
- `Burst`는 `a == b`라 `dirSign`이 +1로 고정된다. 포효는 방향 없는 원형이라 맞다.
- `Fx`에 `offsetX`를 더한다(`include/core/World.h`), `SkillDef`에 `fxOffsetX`,
  파서(`src/core/DragonData.cpp` `parseDragonText`)에 `fx_offset_x=`, `World::addSkillFx`에 전달.
- `markRegion`은 `cx`·`cy`를 쓰므로 자동으로 따라 맞는다. 별도 작업이 아니다.

**함께 고칠 문서** — `fx_offset_y`의 **뜻이 바뀐다**(화면 px → 100% 기준 px):
- `assets/dragons/README.md`의 `fx_offset_y=-24` 설명과 "크기 기준" 절
- `docs/implementation-notes.md`에 "뜻이 바뀐 값"으로 기록. 지금 `fx_offset_y`를 쓰는
  데이터는 **하나도 없어서 깨지는 것은 없다**(`fx=`를 적은 스킬이 아직 없다).
- `docs/codemap.md`의 "효과를 스프라이트로 그리기" 줄에 `fx_offset_x`를 더한다.

### 3번 — 데이터 적용 (`assets/dragons/revenant/`)

**(가) 2026-11-15 결정 적용** — `docs/characters/revenant-dragon-zone-skills.md` 7절:
- `[skill.2]` 저주의 안개(Lv3)를 **남기고**, `[skill.6]` 침식의 수치로 **올려 하나로** 만든다.
- 비는 `[skill.6]` 자리에 **망계의 숨결**을 `kind=Line`으로 넣는다.
- **안개를 지우지 않는다.** 지우면 스킬 인덱스가 밀려 저장 파일의 장착 칸이 어긋난다.
- 수치는 전부 임시값이다. `implementation-notes.md`에 적고 `tests/balance_sim.cpp`로 재측정한다.

**(나) FX 시트 3장 만들기.** 원본은 낱장 8프레임이 가장 안전하다(모두 444×444 확인,
atlas는 1776×888 = 2행×4열이라 격자로도 맞지만 낱장을 쓴다). `make_sheet.py`는 `--set 이름=경로`의
이름으로 `<이름>.png`를 뽑으므로 `fx_` 접두사를 그대로 쓴다. 원본은 **왼쪽 지향이라 `--mirror`**.

```bash
R=assets/references/revenant-dragon/recovered-2026-11-16/original
python tools/make_sheet.py \
  --set fx_pierce=$R/04_spirit_pierce_effects/piercing_attack \
  --set fx_claw=$R/02_basic_attack_effects/claw_slash \
  --set fx_breath=$R/10_underworld_breath_effects/sustained_stream \
  --mirror --cell 256 -o assets/dragons/revenant
```

본체 시트와 달리 **`--ref`·`--ratio`로 몸 크기에 맞추지 않는다** — FX는 본체 높이 기준
`fx_scale`로 맞추므로 칸을 꽉 쓰는 쪽이 낫다. 돌린 뒤 실제 인자와 결과 칸 수를
`assets/dragons/revenant/dragon.txt` 주석에 남긴다(기존 본체 시트 주석과 같은 방식).

**(다) `dragon.txt` 배선.** 초기 수치는 **전부 임시값**이고 `fx_offset_*`는 **100% 기준 px**다.
1번 도구의 미리보기를 보고 이 회차 안에서 조정한다. `fx_fps`는 8프레임을 3단계 합에 맞춰 역산한다.

| 스킬 | kind | fx | fx_scale | fx_offset_x | 발동/지속/잔류 | 합 | fx_fps |
|---|---|---|---|---|---|---|---|
| 혼령 관통 (`[skill.5]`, range 360) | Line | `pierce` | 1.8 | +130 | 0.15 / 0.35 / 0.20 | 0.70 | 11 |
| 망자의 발톱 (`[skill.4]`, range 140) | Line | `claw` | 1.0 | +45 | 0.10 / 0.22 / 0.15 | 0.47 | 17 |
| 망계의 숨결 (`[skill.6]`, 신규) | Line | `breath` | 2.2 | +170 | 0.18 / 0.45 / 0.25 | 0.88 | 9 |

`fx_front`는 세 개 모두 기본값(`yes`, 본체 위)으로 둔다.

**(라) `CREDITS.md`** — `assets/dragons/revenant/CREDITS.md`에 FX 시트 3장의 출처를
`recovered-2026-11-16` 원본 경로로 적는다.

---

## 주고받는 횟수를 줄이는 규칙 (회차마다 지킬 것)

11회차가 같은 일을 반복하므로 효과가 11배다. 2026-11-16 저녁 세션의 실측에서
**토큰의 66%가 "매 턴 다시 읽히는 지시문"**이었다(요청 43회, `CLAUDE.md` + 시스템 프롬프트 +
도구 목록). 읽은 양이 아니라 **주고받은 횟수**가 비용을 정했다. `CLAUDE.md`의 "적게 주고받기"가
숫자로 확인된 것이니, 회차 세션은 다음을 지킨다.

1. **명령은 `&&` 로 묶는다. 한 회차의 `Bash` 호출을 10회 이하로 목표한다.**
   같은 세션에서 `git` 확인 → `ls` → `grep` 을 세 번에 나누지 않는다. 빌드·테스트·검증도 한 줄이다.
   (11-16 세션은 28회를 썼다. 묶을 수 있었던 것이 절반이었다.)
2. **도구 출력은 반드시 걸러 받는다.** `gh api` 는 `-q` 로, 목록은 `awk`·`sort -u` 로 줄인다.
   파일 목록·JSON 을 날것으로 받지 않는다. (11-16 세션은 PR 파일명 100줄을 그대로 받아
   세션 끝까지 지고 갔다.)
3. **"어디가 어떻게 되어 있나" 조사는 `Explore` 서브에이전트에 넘기고 결론만 받는다.**
   서브에이전트는 별도 context 에서 읽고 요약만 돌려준다. 단 서브에이전트도 `CLAUDE.md` 와
   도구 목록을 자기 몫으로 다시 로드하므로, **한두 번 읽으면 끝나는 것에는 쓰지 않는다** —
   넓게 훑어야 할 때만 쓴다.
4. **큰 파일은 통째로 읽지 않는다.** `docs/codemap.md` 에서 이름을 찾아 `grep` 으로
   그 함수 근처만 읽는다. `Renderer.cpp`(~1300줄)·`World.cpp`(~720줄)이 특히 그렇다.
5. **중간 보고를 하지 않는다.** 단계마다 설명하지 말고 회차 끝에 3줄로 정리한다.
6. **쓰지 않는 MCP 연결은 끈다.** 이 큐의 작업에는 Notion·Google Drive·Docs 가 필요하지 않다.
   연결된 서버는 도구 목록으로 매 턴 비용을 낸다.

## 검증 — 회차마다 5단계 전부

```bash
# 1 코어
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
# 2 Windows 코드 (src/win 을 고쳤으면 푸시 전에 필수, 9초)
tools/check_win_build.sh
# 3 시트 격자·크기
python tools/check_sheets.py --only revenant --out /tmp/sheets.png
# 4 에셋 데이터
./build/asset_check assets
# 5 FX 합성 미리보기 (1번 항목이 만든 도구) — 0.5 / 1.0 / 10.0 세 배율
python tools/fx_preview.py --dragon revenant --char-scale 0.5,1.0,10.0 --out /tmp/fx.png
```

3·5번의 출력 PNG는 **봐야 의미가 있다.** 회차 세션은 생성 후 직접 열어 보고, 무엇이 보였는지
PR 본문에 글로 적는다("관통 FX가 1.0배에서 몸 앞 130px, 10배에서도 같은 비율로 앞에 있다" 식).

**실행은 사람 몫으로 남는다.** 이 환경은 `src/win`을 컴파일만 한다.
`docs/implementation-notes.md`의 "Windows에서 확인할 것"에 다음을 더한다:
- 캐릭터 크기 50% / 100% / 1000%에서 세 스킬의 이펙트 위치
- 드래곤이 왼쪽을 볼 때 이펙트가 같이 뒤집히는지
- 이펙트가 본체를 가리지 않는지(`fx_front=yes`)

---

## 이번에 하지 않는 것 — 코드·결정 대기

| 원본 폴더 | 스킬 | 왜 미루는가 |
|---|---|---|
| 06 영혼 수확 효과 | 영혼 수확 (`PassiveOnKill`) | 패시브는 발동 경로가 없다. `World::onEnemyKilled`에서 `addSkillFx`를 부르는 **core 변경**이 필요하고 브랜치 3개를 넘긴다 |
| 08 망령의 침식 효과 | 망령의 침식 (`Zone`) | 11/15 결정으로 **안개와 합쳐져 없어진다.** 또 `Zone`은 아직 `fx=`를 쓰지 않는다 |
| 12 죽음의 행렬 효과 | 죽음의 행렬 | **미결정.** `revenant-dragon-skills-v1.md`가 "기존 시스템 검토 후 자동/수동 결정"으로 남겨 뒀다 |
| 02 projectile_travel / impact | 영혼 탄 (`Projectile`) | `Projectile`은 아직 `fx=`를 쓰지 않는다 |

`Zone`·`Projectile`에 `fx=`를 허용하는 건은 `docs/implementation-notes.md`에
"지속 긴 장판에 3단계를 억지로 맞추지 않기로 했다"는 기존 결정이 있어, **설계부터 다시 정해야 한다.**
회차 세션이 혼자 뒤집지 않는다.

---

## 정해진 것 (2026-11-16 21:00, 사람)

- PR #63을 먼저 머지하고 그 위에서 작업한다. → 완료
- 오늘은 **코드 지원이 되는 것만** 한다: 혼령 관통·망자의 발톱, 그리고 11/15 결정으로 새로 들어오는 망계의 숨결.
- 11/15의 "안개+침식 합치고 Lv5에 숨결" 결정을 **오늘 함께 적용한다**(FX 작업의 전제).
- 브랜치는 **3개까지**. `win` 분류도 **이번 작업에 한해** 자동 세션이 머지해도 된다
  — 단 PR 본문에 사후 검토 필요를 남긴다. main에 직접 push 하는 것은 이 문서의 **상태 줄만**.
- 뒤집기는 `Fx`의 `a`→`b` 방향으로 판정한다(드래곤 `facing`을 끌어오지 않는다).
- **`fx_offset_*`에 `charScale`을 곱한다.** 사람이 21:03에 지적한 건이고, 기존 `fx_offset_y`도
  같은 버그였다(`fx=`를 쓰는 스킬이 없어 드러나지 않았다).
- 회차마다 직전 회차를 재검증하고, 깨졌으면 새 작업보다 수정이 먼저다.
- 같은 항목 2회차 연속 실패 시 멈춘다.
