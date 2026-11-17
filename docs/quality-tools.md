# 품질·안정성 도구

"돌아가는가"는 `build.yml` 이 본다. 이 문서는 **조용히 틀린 데를 찾는 도구들**을 적는다.
전부 무료이고 이 환경과 CI 에서 돈다.

작업 전에 알아야 할 한 줄: **`src/win` 은 이제 이 환경에서 컴파일된다.** 아래 1번을 먼저 본다.

---

## 1. `tools/check_win_build.sh` — Windows 코드를 푸시 전에 컴파일한다

```bash
sudo apt-get update && sudo apt-get install -y mingw-w64   # 한 번만

tools/check_win_build.sh          # 문법 검사 (9초쯤)
tools/check_win_build.sh --link   # 링크까지. dist/win-local/dragon_tower.exe
```

**이게 가장 크게 바뀐 점이다.** `docs/ai-workflow-notes.md` 에 "이 환경에서는 Windows 코드를
컴파일할 수 없고 mingw 크로스 컴파일러도 설치되지 않는다"고 적혀 있었는데 **사실이 아니었다.**
`apt-get update` 를 먼저 하지 않아 패키지를 못 찾은 것이었다. GDI+ 까지 포함해 링크가 되고
20초쯤에 실행 파일이 나온다.

v0.6 의 CI 실패 셋(헤더 누락 / `RectF` 모호성 / 정의 순서)을 **전부 9초에 잡는다.**
(실제로 `App.h` 의 `#include <shellapi.h>` 를 지워 확인했다 — 같은 오류가 그대로 나온다.)

**한계**: GCC 지 MSVC 가 아니다. GCC 가 더 엄격한 곳(이름 해석, 정의 순서)은 여기서 잡히지만
MSVC 전용 pragma 같은 것은 못 잡는다. **CI 를 대신하지 않고 왕복 횟수를 줄인다.**
여기서 나온 `.exe` 도 mingw 산물이라 배포본이 아니다 — 사람이 볼 때는 CI 의 `DragonTower-windows` 를 쓴다.

---

## 2. 새니타이저 — 테스트가 "통과"해도 메모리를 잘못 건드리는 것을 잡는다

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
cmake --build build-asan -j
cd build-asan && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ctest --output-on-failure
```

필요하면 `sudo apt-get install -y libclang-rt-18-dev` (없으면 clang 이 asan 런타임을 못 찾는다).

이걸 켜서 **보스 2페이즈 소환의 use-after-free** 를 찾았다. 1417건의 테스트가 전부 통과하는
상태였는데도 그랬다 — 잘못된 메모리를 읽고 "그럴듯한" 값이 나오면 테스트는 아무것도 모른다.

테스트만으로는 좁으니 `balance_sim` 도 같이 돌린다(전투·탑 경로를 훑는다):

```bash
./balance_sim floors --trials=20 --seed=1
```

---

## 3. 퍼저 — 사람이 손으로 쓰는 파일의 파서를 때린다

대상은 셋. 전부 사람이 손으로 쓰거나 손으로 고치는 파일이다.

| 퍼저 | 무엇을 | 왜 |
|---|---|---|
| `fuzz_dragon_text` | `dragon.txt` 파서 | 오타·반쪽 줄·깨진 UTF-8 이 실제로 들어온다 |
| `fuzz_enemy_text` | `enemy.txt` 파서 | `summon_count` 처럼 나중에 루프 횟수가 되는 값이 있다 |
| `fuzz_save_roundtrip` | 저장 파일 + **왕복 불변식** | 깨지면 영구 사망 게임에서 기른 개체가 사라진다 |
| `fuzz_tower_text` | `floors.txt` 파서 (v1.6) | 층 수·마리 수·레벨이 나중에 루프 횟수가 된다. `Config.h` 의 울타리 네 개가 **모든 경로에서** 걸리는지 본다 |

```bash
cmake -S . -B build-fuzz -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_COMPILER=clang++ -DDRAGON_BUILD_FUZZERS=ON
cmake --build build-fuzz -j
./build-fuzz/fuzz_save_roundtrip -max_total_time=300 tests/fuzz/corpus/fuzz_save_roundtrip
```

`fuzz_save_roundtrip` 은 죽는 것만 보지 않는다. **한 번 읽어 쓴 글을 또 읽어 쓰면 같아야 한다**는
것까지 본다. 이 검사가 `dragon.level` 의 상한 누락을 찾았다 — `stable.N.level` 은 30으로
자르는데 `dragon.level` 만 자르지 않아서, 손으로 고친 저장 파일이 레벨 34로 읽히고 그대로
저장됐다가 다음에 켤 때 30으로 잘렸다.

**⚠ 퍼징은 반드시 작업 폴더를 따로 주고 돌린다.** libFuzzer 는 인수로 준 폴더에 새로 찾은
입력을 계속 써 넣으므로 `tests/fuzz/corpus/` 를 그대로 주면 **저장소가 오염된다.**
2026-11-14에 실제로 754개 파일 3.3MB 가 이렇게 들어갔다. 자세한 것은
[`tests/fuzz/README.md`](../tests/fuzz/README.md).

**찾은 입력은 `tests/fuzz/corpus/<퍼저>/` 에 넣는다.** `ctest` 가 그 폴더를 매번 다시 돌린다
(새 입력을 찾지 않고 끝나므로 빠르다). 코퍼스는 작게 유지한다 — 큰 파일을 저장소에 넣지 않고,
CI 가 Actions 캐시에 따로 쌓는다.

---

## 4. `tools/check_static.py` — cppcheck + clang-tidy, 기준선과 비교

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
python3 tools/check_static.py --build-dir build            # 검사
python3 tools/check_static.py --build-dir build --update   # 기준선 갱신
```

**왜 기준선인가**: 처음 돌리면 지적이 수십 개다. 전부 고치지 않으면 CI 가 영원히 빨간색이고,
빨간 CI 는 아무도 보지 않는다. 그래서 지금 것을 `tools/static-baseline.txt` 에 적어 두고
**새로 생긴 것만** 막는다. 그 목록은 줄어들기만 해야 한다.

지적을 `파일:도구/검사이름` 으로만 적는다 — 줄 번호를 빼서 코드를 조금 옮겨도 기준선이 안 깨진다.

**cppcheck 의 `style` 검사는 일부러 켜지 않았다.** 버전이 조금만 달라도 결과가 달라져서
기준선이 깨진다(이 환경의 2.13 과 CI 러너의 버전이 달라 4건이 어긋나 CI 가 한 번 실패했다).
게다가 `style` 지적은 전부 "const 를 붙여라" 류라 버그가 아니다.
**기준선 방식의 약점이 여기다** — 도구 버전이 올라가면 기준선이 흔들릴 수 있다.
그때는 `--update` 로 갱신하고, 늘어난 것이 진짜 지적인지 한 번 보면 된다.

이 검사가 **항상 통과하던 테스트**를 찾았다(`core_tests.cpp` 의
`slots[0] != before || before == slots[0]` — 무엇을 넣어도 참이다).

지금 기준선에 남은 것은 전부 "버그는 아니지만 더 좋게 쓸 수 있는 것"이다. 고치면 `--update` 한다.

---

## 5. `tools/check_sheets.py` — 스프라이트 시트를 숫자로 재고 그림으로 본다

```bash
python3 tools/check_sheets.py                        # 표
python3 tools/check_sheets.py --out /tmp/sheets.png  # 대조표 PNG
```

이 저장소에서 가장 많이 되돌아간 작업이 시트 변환이다(v0.3 배율, v0.4 격자, v1.4 칸 나누기).
기록이 매번 같은 말로 끝난다: **성공 메시지만으로는 모른다. 숫자로 재고 눈으로 확인해야 한다.**

- `너비 % 칸너비 == 0` 을 확인한다. 게임도 이 조건을 거부하므로 객관적 기준이다.
- 상태별 내용 높이와 `render_scale` 을 곱한 **그려지는 크기**를 표로 낸다.
- `--out` 으로 **대조표 PNG** 를 만든다. 칸 경계와 바닥선을 그려 넣어 어긋난 것이 눈에 보인다.
  PR 본문에 붙이면 사람이 Windows 를 켜지 않고도 확인할 수 있다.

**자동으로 판정하지 않는 것**: 누운 자세(`rest`·`death`)·날개 펼침(`fly`)·`hit`·`attack` 의
크기 차이. `docs/character-pipeline.md` 와 v0.3 기록이 "윤곽 전체로 재면 날개 펼침에
휘둘린다 — 같은 신체 부위로 재야 한다"로 끝나 있다. 이 도구는 알파 윤곽만 보므로
그 둘을 비교하면 **그 교훈을 그대로 다시 밟는다.** 서 있는 자세(`idle`·`move`)끼리만
자동 비교하고, 나머지는 숫자와 대조표로 사람에게 넘긴다.

**쓰이지 않는 스킬 시트·아이콘은 이 도구가 보지 않는다** — `tools/asset_check.cpp` 가
v1.6(PR #29)부터 표로 적는다. 같은 것을 두 도구가 말하면 한쪽만 고치고 지나가게 된다.

---

## 6. 커버리지

```bash
pip install gcovr
cmake -S . -B build-cov -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="--coverage -O0" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build build-cov -j && (cd build-cov && ctest; ./balance_sim)
gcovr --root . --filter 'src/core/' --txt --sort uncovered-number build-cov
```

2026-11-14 기준 코어 **89%**(2421줄 중 2176줄). 막는 기준으로 쓰지 않고 숫자만 남긴다 —
커버리지를 목표로 삼으면 의미 없는 테스트가 늘어난다. 어디가 비었는지 볼 때만 쓴다.

---

## 7. `tools/fx_preview.py` — 본체 위에 효과를 합성해 본다

`check_sheets.py` 는 시트 **한 장**이 격자에 맞는지만 본다. 이 도구는 **합성 결과**를 본다 —
`fx_scale`·`fx_offset_x`·`fx_offset_y`·`fx_alpha` 를 적었을 때 효과가 본체 어디에 어떤 크기·
진하기로 붙는지다.

이 환경은 `src/win` 을 **컴파일만 하고 실행하지 못한다.** 그래서 수치를 적어도 맞는지 볼
방법이 없었다. 이 도구가 `Renderer::drawFxSheet` 와 **같은 계산**을 파이썬으로 다시 해서
한 장의 PNG 로 낸다.

```bash
# dragon.txt 에서 fx= 가 적힌 스킬을 전부 본다
python3 tools/fx_preview.py --dragon revenant --char-scale 0.5,1.0,10.0 --out /tmp/fx.png

# 시트를 직접 지정해서 수치만 바꿔 가며 본다
python3 tools/fx_preview.py --body assets/dragons/revenant/skill_pierce.png \
    --fx assets/dragons/revenant/fx_pierce.png \
    --fx-scale 1.8 --fx-offset-x 130 --fx-alpha 0.7 --body-render-scale 1.04 --out /tmp/fx.png
```

- 가로는 **캐릭터 배율 세 가지**(기본 0.5 / 1.0 / 10.0), 세로는 **효과 x 좌우 두 방향**이다.
- 보정값은 "100% 기준 px" 이고 그리는 쪽에서 `charScale` 을 곱하므로, **세 칸이 서로 똑같이
  보여야 맞다.** 한 칸이라도 위치가 다르면 곱셈이 빠진 것이다(크기 1000%에서 머리 위 보정이
  몸통에 파묻히는 그 버그다).
- `--phase windup|hold|tail` 로 세 단계의 크기·투명도를 바꿔 본다(`fxPhaseLook` 과 같은 값).
- 그림 안 글자는 **ASCII 로만** 쓴다. 이 환경에 한글 TTF 가 없어 한글은 네모로 나온다.
  설명은 PR 본문과 이 문서에 쓴다.

**주의**: 2026-11-16 현재 `Renderer::drawFxSheet` 는 보정값에 `charScale` 을 곱하지 않고
좌우 뒤집기도 하지 않는다. 이 도구는 **고쳐질 쪽**(자동 큐 2번 항목) 기준으로 만들었으므로,
그 작업이 들어가기 전에는 이 그림과 실제 화면이 다르다.

## CI (`.github/workflows/quality.yml`)

**이 저장소는 비공개라 Actions 분이 유한하다.** 그래서:

- 잡을 쪼개지 않고 `quality` 하나로 묶었다(잡마다 체크아웃·설치가 다시 돈다).
- 전부 `ubuntu` 다 — `windows` 러너는 분이 **2배**로 계산된다.
- **`src/win` 교차 컴파일을 맨 앞에 둔다.** 10초만에 실패하면 뒤를 볼 필요가 없고,
  `build.yml` 의 느리고 비싼 windows 잡이 같은 이유로 실패하는 것을 미리 막는다.
- **퍼징은 매 푸시가 아니라** 주 1회(일요일 04:17 KST)와 수동(`workflow_dispatch`)으로만 돈다.
  찾은 입력은 Actions 캐시에 쌓여 다음 회차가 이어 간다.

분이 모자라면 `on: push` 를 `on: pull_request` 로 줄이는 것이 첫 번째 조정이다.

## 7. `tools/check_docs.py` — 문서 링크와 색인

문서 규칙("같은 것은 한 곳에만", "새 문서는 색인에 추가")은 사람 손으로 지켜지지 않았다.
목록이 README·`CLAUDE.md`·`AGENTS.md` 세 곳에 갈라져 서로 달라졌고, 문서를 `archive/` 로
옮겼을 때 안쪽 링크가 깨진 채 남았다. 그래서 기계가 잡을 수 있는 둘만 검사로 만들었다.

```bash
python3 tools/check_docs.py
```

1. **깨진 상대 링크** — 문서를 옮기고 링크를 안 고친 것
2. **`docs/INDEX.md` 미등재** — 색인에 없어서 아무도 찾지 못하는 `docs/` 문서

`assets/references/**/recovered-*/` 는 복구한 원본 보존 문서라 검사에서 뺀다(고치지 않는 것이 목적).
`docs/archive/` 는 링크만 검사하고 색인 등재는 요구하지 않는다.

CI 에서 가장 먼저 돈다 — 1초이고 설치가 필요 없다.

### 7.1 archive 역류 검사

`docs/archive/` 를 "읽지 않는다"로 정했는데, 살아 있는 문서가 링크로 끌어오면 그 규칙이
무의미해진다. 그래서 **평시에 읽는 문서가 archive 를 가리키면 실패**시킨다.

가리켜도 되는 문서는 7개뿐이다(`ARCHIVE_LINK_OK`) — 보관 이유와 원본 위치를 적는 것이 그들의 역할이다:
`INDEX.md` · `decisions.md` · `token-budget.md` · `archive/README.md` · `ai-workflow-notes.md` ·
`implementation-notes.md` · `changelog.md`.

검사와 별개로 **저장소 루트의 `.ignore`** 가 `rg`·`fd` 에서 `docs/archive/` 를 기본으로 뺀다.
git 은 이 파일을 보지 않으므로 커밋·diff·CI 에는 영향이 없다. 일부러 보려면 `rg -u`.

### 7.2 `docs/STATE.md` 갱신 검사

`STATE.md` 는 세션 시작 때 읽는 유일한 상태 문서다. **낡으면 없는 것보다 해롭다.**
"작업 끝낼 때 같은 PR 에서 고친다"를 `CLAUDE.md` 에 적었지만 사람 손으로는 지켜지지 않으므로
검사로 만들었다.

```bash
python3 tools/check_docs.py --pr-base origin/main
```

- `src/` · `include/` · `docs/`(archive 제외) · `tools/` · 워크플로 · `CLAUDE.md` · `README.md` ·
  `CMakeLists.txt` 를 고친 PR 이 `docs/STATE.md` 를 안 고치면 **실패**.
- 상태가 정말 그대로인 PR(에셋만, 오타만)은 **커밋 메시지에 `State-Skip: <이유>` 줄**을 넣는다.
- 별도로 `최종 갱신` 날짜가 **14일**(`STATE_STALE_DAYS`) 넘게 묵으면 푸시·PR 어디서든 실패한다.
- 얕은 체크아웃에서 merge-base 를 못 구하면 **막지 않고 경고만** 한다(환경 문제로 PR 을 막으면
  검사 자체를 지우게 된다). CI 체크아웃은 그래서 `fetch-depth: 100` 이다. 방치는 날짜 검사가 잡는다.

### 7.3 `docs/codemap.md` 갱신 검사

`CLAUDE.md` 에 "코드를 바꾸면 이 표도 같은 PR 에서 고친다"고 적혀 있지만 기계가 잡지 않았다.
**코드 지도가 낡으면 다음 세션이 위치를 찾으려고 파일을 넓게 읽게 되고, 그게 줄이려던 비용이다.**

- `src/` · `include/` 를 고친 PR 이 `docs/codemap.md` 를 안 고치면 **실패**
- 지도에 영향이 없는 변경(주석, 내부 리팩터링)은 커밋 메시지에 **`Codemap-Skip: <이유>`** 줄
- `docs/` 만 고치는 PR 에는 걸리지 않는다

7.2 의 STATE 검사와 같은 구조이고 같은 이유로 trailer 형식이다 — 산문에 우연히 걸리지 않는다.
세 경우를 확인했다: 미갱신→실패, 문장 안에 `Codemap-Skip:` 언급→여전히 실패, 줄 머리 trailer→건너뜀.

탈출구를 둔 이유: 탈출구가 없는 검사는 급할 때 검사 자체를 지우게 된다. `State-Skip:` 은
커밋 메시지에 남아서 **누가 언제 왜 건너뛰었는지 보인다.**

형식이 git trailer 인 이유: 처음에는 본문 어디든 `[state-skip]` 이 있으면 건너뛰게 했는데,
**탈출구를 설명하는 커밋 메시지가 스스로 검사를 껐다.** 줄 머리의 `State-Skip:` 는 산문에
우연히 걸리지 않고 이유를 적게 만든다.
