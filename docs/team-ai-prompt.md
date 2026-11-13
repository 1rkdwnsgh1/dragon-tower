# 팀원용 AI 지침 프롬프트

팀원이 쓰는 AI 도구(ChatGPT 프로젝트, Claude 프로젝트, Copilot 등)의 **프로젝트 지침 칸에 아래 블록을 붙여넣는다.**

`AGENTS.md`와 `CLAUDE.md`는 저장소를 직접 읽는 도구에만 전달된다. 웹 채팅에 코드를 복사해 붙여 쓰는 방식에는 닿지 않아서 따로 둔 것이다.

**짧게 유지한다.** 지침은 요청마다 다시 읽히므로 길어지면 그 자체가 비용이다. "모르고 작업하면 되돌리는 비용이 큰 것"만 남겼다. 나머지 규칙은 `AGENTS.md`·`CLAUDE.md`에 있고, **어긋나면 그쪽이 기준이다.** 이 문서를 고치면 팀원에게 다시 붙여넣으라고 알린다.

---

## 붙여넣을 내용 (여기서부터)

드래곤 타워 — 바탕화면 오버레이 드래곤 육성 게임. C++17 + CMake, 게임 엔진 없이 Win32 API + GDI+. 저장소 `1rkdwnsgh1/dragon-tower`, 3명 협업. 코드 식별자는 영어, 주석·문서는 한국어.

### 1. 코드를 찾기 전에 코드 지도부터 본다

**`docs/codemap.md`** 에 무엇이 어디에 있는지 표로 적혀 있다. 위치를 찾으려고 파일을 넓게 읽지 말고, 지도에서 함수 이름을 찾아 `grep` 한 뒤 그 근처만 읽는다.

`src/win/Renderer.cpp`(약 1060줄), `src/core/World.cpp`(약 680줄)는 통째로 읽지 않는다.

폴더는 이렇게 나뉜다 — 게임 규칙은 `src/core`(플랫폼 무관, 테스트됨), OS 의존 코드는 `src/win`. 규칙을 `src/win`에 넣으면 테스트할 수 없다. 조정 가능한 수치는 코드에 박지 말고 `include/core/Config.h`의 `cfg::` 상수나 `assets/dragons/<키>/dragon.txt`·`assets/enemies/<키>/enemy.txt`로 뺀다.

규칙의 기준은 `docs/game-design.md`다. 거기서 **"아직 미결정"인 항목은 임의로 정하지 말고 사람에게 물어본다.**

### 2. 작업이 끝나면 아래 문서를 같은 PR에서 갱신한다

나중에 하면 안 한다. `main`에 직접 커밋하지 말고 브랜치로 PR을 올린다.

1. **`docs/codemap.md`** — 함수·파일이 바뀌면 표도 고친다 (**가장 자주 빠뜨린다**)
2. **`docs/changelog.md`** — 추가/바뀜/고침과 **저장 파일 호환 여부**
3. **`docs/implementation-notes.md`** — 임시로 정한 수치가 바뀌었으면. Windows에서 확인할 것이 생기면 목록에 추가
4. **`docs/game-design.md`** — 미결정이 결정됐거나 확정 사항이 바뀌었으면. 바꿀 때는 **본문을 지우지 말고 "바뀐 확정 사항" 절에 적고, 원래 장 맨 위에서 그쪽으로 안내한다**
5. **`docs/roadmap.md`** — 단계가 끝났거나 체크 항목이 무효가 됐으면
6. **`README.md`, `CLAUDE.md`** — 현재 버전 줄
7. 새 문서를 만들면 **`AGENTS.md`의 문서 목록에 추가**

같은 결정은 한 곳에만 쓰고 나머지에서는 링크한다. 두 곳에 있으면 한쪽이 반드시 낡는다.

### 3. Windows 코드는 CI에서만 빌드된다

`src/win`은 Windows에서만 컴파일된다. 리눅스·맥에서는 `src/core`와 테스트만 빌드된다. 아래 세 가지를 CI 실패로 알게 된 적이 있다 — **푸시 전에 훑어본다. CI 왕복 한 번이 비싸다.**

1. **헤더에 쓰는 타입은 헤더에서 포함한다.** `App.h`에 `shellapi.h`가 없어 `NOTIFYICONDATAW`를 인식하지 못했다
2. **`RectF` 모호성**: `App.cpp`에서 그냥 `RectF`는 우리 타입 `dt::RectF`로 해석된다. GDI+ 타입이 필요하면 `Gdiplus::RectF`로 명시한다
3. **파일 안의 정의 순서** — 선언 전에 쓰면 안 된다

빌드: `cmake -S . -B build && cmake --build build`
실행: `build/Debug/dragon_tower.exe` · 저장 파일: `%APPDATA%\DragonTower\save.txt`

## (여기까지)
