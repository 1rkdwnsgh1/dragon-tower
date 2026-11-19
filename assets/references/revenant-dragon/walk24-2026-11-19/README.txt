망령룡 WALK24 적용 자료 — 2026-11-19

출처: 사용자가 전달한 DRAGON_TOWER_WORK_WALK24_HANDOFF.zip.
변형: 24_FRAME_COMPAT_REPLACE_18. 17.5 보조 자세가 18번을 대체한다.
이미지 품질 NEEDS_REVIEW / Windows 실제 재생 미검증.

DRAGON_TOWER_WORK_WALK24_HANDOFF.zip: 전달 원본을 바이트 그대로 보존.
SHA-256: 19cdafb8247a046234668f17a05ef363125052efd9f660da281ada0185bfbbda
ZIP의 WORK_HANDOFF.md, SOURCE_MANIFEST.json, source/frames 24장,
원본 6x4 시트 및 정상/저속 GIF를 함께 보존했다.
원본을 풀 때 PNG 24장 SHA-256, RGBA, 1024x1024와 이름 순서를 검증했다.
원본은 왼쪽 지향. 게임용 move는 오른쪽 지향이다.

move-before-8frames.png: 기준 main 16f16de의 기존 이동 시트.
SHA-256: 13d04ec28401b9304481a5d5bb61b4f022078581afe735df59e911f83a5015a6
기존 dragon.txt는 같은 커밋에서 복원 가능하다.

변환 (저장소 루트 기준, 별도 임시 폴더에 ZIP을 해제):
python tools/make_sheet.py <해제폴더>/source/frames -o assets/dragons/revenant/move.png --mirror --align union --cell 256

기본 ratio=0.70, bg=none, 발 기준 (128,240).
01~24 순서와 공통 상자/배율/위치를 유지한다. 25번 추가/프레임별 재중심화 없음.
다른 상태를 보존하기 위해 move만 변환했다. 모든 상태를 재변환하는 경우에는
저장소의 --set 공통 배율 규칙을 따를 것. 이번에 idle/fly를 재생성하지 않았다.
결과 6144x256 RGBA, 가로 24칸. SHA-256:
b2596c943039d8e4a86892c3eae06a32b047ede58cacb3664ffef6adc690dd8c
fps=8, loop=yes, frame_w=256, render_scale=1.13 유지. 24/8=3초 루프.

validation.json: 원본 공통 변환과 결과 셀의 바이트 일치, 상자/배율/알파 기록.
contact.png: 24프레임과 IDLE/이전 MOVE/새 MOVE 정적 비교.
contact-120px.png: 120px 칸으로 줄인 정적 비교(실제 게임 렌더링 검증 아님).
sheet-comparison.png: 저장소 check_sheets.py가 만든 상태별 비교.

정적 검사 통과는 보행·해부학·발 미끄러짐·루프 승인과 다르다.
특히 17→18→19, 24→01, 좌우 반전, IDLE↔MOVE 크기 및 3초 속도는
Windows 미리보기/게임에서 확인해야 한다. 본체 코어 기준 정밀 크기 일치는 미검증.
상세 검사·빌드·테스트 결과는 docs/characters/revenant-dragon-progress.md의
walk24-2026-11-19 절에 기록한다.
