## 무엇을

<!-- 한두 줄. 왜 필요한지도 적는다. -->

## 확인

- [ ] `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
- [ ] `src/win` 을 고쳤으면 `tools/check_win_build.sh --link`
- [ ] 문서를 옮기거나 추가했으면 `python3 tools/check_docs.py`
- [ ] **`docs/STATE.md` 갱신** — 버전·CI·열린 PR·막혀 있는 것·순서 제약 중 바뀐 것과 '최종 갱신' 날짜
      <br>상태가 정말 그대로라면 커밋 메시지에 `State-Skip: <이유>` 줄을 넣는다 (CI 가 검사한다)
- [ ] 결정이 생겼으면 `docs/decisions.md` 에 한 줄, 게임 규칙이면 `docs/game-design.md` 10장

## 작업 중 생긴 문제

<!-- 다음에 같은 작업을 할 때 반복하지 않도록 적는다. 없으면 "없음". -->
