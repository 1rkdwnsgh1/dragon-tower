망령룡 승인 키 기반 FLY24 — 2026-11-19

입력: 사용자 제공 REVENANT_FLY24_FROM_APPROVED_KEYS_V1.zip 전체 원본 보존.
SHA-256: c946b4caaa454edfb0a36b1e1ee7df8fe676637c3577ea91538c87ce41e00b2e
기준 main: 9292d4d (WALK24 PR #96 병합 이후).
fly-before-8frames.png: 이 기준의 기존 비행 시트. 설정은 같은 Git 커밋에서 복구 가능.

원본 ZIP에는 승인 키 기반 24장(frames/), 6x4 시트, GIF 3종, 제작 QA 보고서,
원본 8키 시트와 QA/VALIDATION.json, PLAYBACK.html을 그대로 보존했다.
검사: 24장 RGBA 1024x1024, 투명도, 파일명 순서, 6x4 시트와 각 셀 픽셀 일치.
01,04,07,10,13,16,19,22가 원본 8키와 픽셀 일치. 나머지는 광학 흐름 중간 프레임.

재현: 원본 ZIP을 별도 임시 폴더로 해제하고 저장소 루트에서
python tools/make_sheet.py <해제폴더>/frames -o assets/dragons/revenant/fly.png --mirror --align union --cell 256 --ratio 0.90

다른 모션을 보존하기 위해 fly만 변환. 모든 상태를 새로 만들 때는 저장소의 --set 규칙을 따른다.
반전 후 공통 윤곽 상자 (78,40,906,985), 202x230 축소, (27,10) 배치.
프레임별 재중심화/재생성/크로스페이드 없음. 원본 상대 움직임을 공통 변환으로 유지한다.
결과: 6144x256 RGBA, 오른쪽 지향, 256x256 가로 24칸.
출력 SHA-256: 31c03726eced8e054ca99f9db49b8ac00dba20a5805f2b674dbb276c3c48b4e8
기존 fps=10, loop=yes, render_scale=1.37, frame_w=256 유지. 게임 루프 2.4초.
원본 GIF 루프는 2.88초이므로 동일 속도가 아니다. Windows 실제 확인 전 임의 튜닝 없음.

contact.png: 게임 시트 24칸 정적 비교.
sheet-comparison.png: check_sheets.py 상태별 대조표.
validation.json: 원본 프레임 해시, 상자·위치, 결과 셀과 공통 변환 일치 검사.

상태: NEEDS_REVIEW / Windows 실제 재생 미검증.
첨부 QA 보고서의 날개 전환 왜곡 구간 11~13,14~16,20~22와 24→01,
사슬·꼬리 연속성, 좌우 반전, IDLE/WALK↔FLY 크기, 120px 선명도를 실제 게임에서 확인해야 한다.
승인된 키 사용과 보간 비행 품질 최종 승인을 구분한다.
검사·빌드·테스트 결과의 기준은 docs/characters/revenant-dragon-progress.md의 FLY24 기록이다.
