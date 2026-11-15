# 퍼저

사람이 손으로 쓰거나 고치는 파일의 파서를 때린다. 설명과 한계는
[`docs/quality-tools.md`](../../docs/quality-tools.md) 3절.

| 퍼저 | 대상 |
|---|---|
| `fuzz_dragon_text` | `assets/dragons/<키>/dragon.txt` |
| `fuzz_enemy_text` | `assets/enemies/<키>/enemy.txt` |
| `fuzz_save_roundtrip` | 저장 파일 + **왕복 불변식** (읽기→쓰기→읽기가 같아야 한다) |
| `fuzz_tower_text` | `assets/tower/floors.txt` (v1.6) + `Config.h` 의 울타리 상수 확인 |

## 빌드

```bash
cmake -S . -B build-fuzz -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_COMPILER=clang++ -DDRAGON_BUILD_FUZZERS=ON
cmake --build build-fuzz -j
```

Clang 전용이다. `sudo apt-get install -y clang libclang-rt-18-dev` 가 필요하다.

## ⚠ 돌릴 때 — 반드시 작업 폴더를 따로 준다

**libFuzzer 는 인수로 준 폴더에 새로 찾은 입력을 계속 써 넣는다.**
`tests/fuzz/corpus/` 를 그대로 주고 퍼징하면 **저장소가 오염된다.**

```bash
# 이렇게 한다 — 작업 폴더를 따로 만들고 씨앗을 복사해서 돌린다
mkdir -p /tmp/fz/dragon && cp tests/fuzz/corpus/fuzz_dragon_text/* /tmp/fz/dragon/
./build-fuzz/fuzz_dragon_text -max_total_time=300 -max_len=4096 \
    -artifact_prefix=/tmp/fz/crash/ /tmp/fz/dragon
```

```bash
# 이렇게 하지 않는다 — 저장소 폴더에 수백 개 파일이 쌓인다
./build-fuzz/fuzz_dragon_text -max_total_time=300 tests/fuzz/corpus/fuzz_dragon_text
```

**실제로 당했다.** 2026-11-14에 `ctest` 가 `-runs=0` 없이 폴더만 주고 돌아서
(폴더만 주면 재생이 아니라 **무한 퍼징**이다) 16분 동안 입력을 쌓았고, 뒤이은
`git add -A` 가 **754개 파일 3.3MB** 를 그대로 커밋했다. PR #33 로 `main` 까지 들어갔다.
지운 것은 다음 PR 이고, **커밋 히스토리에는 남아 있다**(공유 저장소라 히스토리를 고쳐 쓰지 않았다).

## 회귀 검사 (`ctest`)

`ctest` 는 `-runs=0` 으로 **모아 둔 입력만 다시 돌리고 끝난다**(0.5초). 새 입력을 찾지 않으므로
저장소에 아무것도 쓰지 않는다. `CMakeLists.txt` 의 `-runs=0` 을 지우지 말 것.

## 코퍼스는 작게 유지한다

이 폴더에는 **씨앗과 회귀 입력만** 둔다(지금 17개, 96KB).

- `seed-*` — 실제 에셋에서 뜬 것. 퍼저가 처음부터 볼 모양을 준다
- `hand-*` — 손으로 쓴 극단값(음수 최대, `inf`/`nan`, 깨진 UTF-8, 터무니없는 개수)
- `regression-*` — **퍼저가 실제로 찾은 입력.** 고친 뒤 여기 넣어 다시는 안 나오게 한다

자란 코퍼스는 CI 가 Actions 캐시에 따로 쌓는다(`.github/workflows/quality.yml` 의 `fuzz` 잡).
저장소에 넣지 않는다.

## 크래시를 찾으면

1. 입력 파일을 `tests/fuzz/corpus/<퍼저>/regression-<무슨-문제>.txt` 로 넣는다
2. 고친다
3. **고친 것을 되돌리면 `ctest` 가 실패하는 것을 확인한다** — 확인하지 않으면 테스트가
   실제로 잡는지 알 수 없다(v1.6 부터의 절차)
