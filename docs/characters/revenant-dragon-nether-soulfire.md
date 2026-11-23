# 망령룡 신규 스킬 — 망계의 업화

## 확정 연출과 피해

1. 망령룡이 준비 자세에서 도약하고 공중에서 공격 자세를 잡는다.
2. 입에서 청록색 영혼 화염을 대각선 아래 지면으로 내뿜는다. 본체와 브레스는 별도 에셋이다.
3. 브레스가 지면에 닿는 순간 충돌 효과와 독립 지속 화염이 시작된다.
4. 잔류 화염은 브레스 충돌 지점 뒤쪽에서 투명하게 짧게 자라다가 사라진다. 충돌을 자연스럽게 이어 보이게 하는 시각 효과다.
5. 지면의 독립 지속 화염은 개별 불꽃이 서로 다른 시점에 성장하고 사라지는 방식으로 연출한다. 지속 피해는 이 구간에만 적용한다.
6. 본체는 분사를 마치면 회복 자세를 거쳐 착지하고 기본 동작으로 돌아간다.

지속 피해를 받는 엔티티 위에 불꽃을 표시하는 연출은 후속 작업이다.
초기 충돌의 즉시 피해 여부, 지속 피해량·판정 주기·범위·장판 수명, 해제 레벨·재사용 대기시간·발동 방식은 미정이다.
기존 [망계의 숨결 결정](../decisions.md#d-망령룡-장판-스킬-중복-2026-11-15)과 [전투 규칙](../game-design.md#4-전투-모드)을 별도 기준으로 참조한다. 망계의 업화는 신규 스킬 기획이며 게임 등록 및 기술 설계는 대기 중이다.

## 단계와 ZIP 내부 파일

아래 FPS, 시간, 앵커는 에셋 명세다. 서로 다른 반복 길이의 동기화, 최종 효과 좌표, 게임 판정 시점은 실제 합성에서 검증한다.

| 단계 | 패키지와 내부 구성 | 에셋 명세 |
|---|---|---|
| 본체 준비·도약·공중 공격·착지 | BODY40 V3 REVIEW의 `frames/`, 전체 4×10 시트, 정상·저속 GIF, `FRAME_TIMING.json`, `MOUTH_ANCHORS_21_30.json` | 1024×1024 RGBA, 40장, 10 FPS, 전체 4초. 01–08 준비, 09–16 도약, 17–20 공중 자세 잡기, 21–30 공격 반복(1초), 31–34 회복, 35–40 착지. 공격 입 앵커는 좌상단 기준 (764, 586), 오른쪽 수평에서 시계 방향 55°인 잠정값이다. |
| 대각 브레스 | DIAGONAL V1의 `LAYER_A_CORE/`, `LAYER_B_OUTER/`, `COMPOSITE/`, `FRAME_MANIFEST.json` | 각 시퀀스 1024×1024 RGBA 16장, 20 FPS, 0.8초 반복. 브레스 로컬 앵커 (160, 160), 우하향 55°. B 외곽 위에 A 코어를 올린다. 본체의 55° 앵커와 방향이 일치하며 ZIP에 본체 합성 미리보기가 있다. |
| 지면 충돌 | GROUND IMPACT V5의 `FRAME_MANIFEST.json`, `FRAME_MAPPING.json`, 합성 시퀀스와 분리 언더레이 | 1024×1024 RGBA 72장, 20 FPS. 01–12 초기 충돌(0.6초), 13–72 충돌 지점 지속 연출(3초). 앵커 (440, 700), 전체 시트 8×9. 이 지속 구간은 충돌 에셋 내부 구간이다. |
| 충돌 후면 잔류 | LINGERING V1의 `FRAME_MANIFEST.json`, `PREVIEW_INSTANCES.json`, 합성 시퀀스 | 1024×1024 RGBA 48장, 20 FPS, 2.4초 1회 재생. 앵커 (440, 700). 01–08 생성, 09–24 낮게 연소, 25–36 감쇠, 37–48 소멸. 마지막 장은 완전 투명하다. |
| 독립 지속 화염 | INDEPENDENT V1의 `manifest.json`, A–F 폴더별 `FRAMES/`, `GROUND/`, `REAR/`, `FRONT/`, `SPRITE_SHEETS/` | 20 FPS. A 대형·B 중형·C 상승·D 지면 화염은 각 80장(4초) 반복, E 분출은 24장(1.2초) 1회, F 불티는 40장(2초) 반복. D는 768×384, 앵커 (384, 300). 나머지는 512×512, 앵커 (256, 448). 각 종류는 합성 시퀀스 또는 분리 패스로 표현할 수 있다. |

BODY40 V3 패키지의 `QA_REPORT.md`는 공격 21–30 프레임을 이전 버전에서 보존했다고 기록하며, 전체 본체 모션은 전환부의 골격·외형 편차와 실시간 재생 검증 미완료로 **검토용**으로 판정한다. 실제 게임 적용 전 본체 전환, 입과 브레스의 연결, 충돌 후면의 잔류 배치를 확인해야 한다.

## 첨부 ZIP 원본

이전 기획 대화 첨부 5개를 원래 파일명과 바이트 그대로 보관했다. 각 ZIP의 내부 CRC와 복사본 SHA-256을 확인했다.

| ZIP | 바이트 | SHA-256 |
|---|---:|---|
| [REVENANT_NETHER_SOULFIRE_BODY40_V3_REVIEW.zip](../../assets/references/revenant-dragon/nether-soulfire/REVENANT_NETHER_SOULFIRE_BODY40_V3_REVIEW.zip) | 74,222,990 | `09b429b8df7e6bcd74a2968d64f8ffc93ffd9608fc2b625f13752c290ccde67f` |
| [NETHER_SOULFIRE_DIAGONAL_V1(1).zip](../../assets/references/revenant-dragon/nether-soulfire/NETHER_SOULFIRE_DIAGONAL_V1%281%29.zip) | 32,218,423 | `8a7945cf10f407808211aae6d25d857772d94a9b065e3d992b3babfc47bbc140` |
| [NETHER_SOULFIRE_GROUND_IMPACT_V5(1).zip](../../assets/references/revenant-dragon/nether-soulfire/NETHER_SOULFIRE_GROUND_IMPACT_V5%281%29.zip) | 72,047,567 | `c1fc4805f1bb08fef2002de689461a2b9ebd2bdc64ff3b11ae2593322ef1b568` |
| [NETHER_SOULFIRE_LINGERING_V1(1).zip](../../assets/references/revenant-dragon/nether-soulfire/NETHER_SOULFIRE_LINGERING_V1%281%29.zip) | 14,682,402 | `1c7bd97dedbb9a05908957323726ebdb9188479ff8335b1a4a5c4611c06ecfb0` |
| [NETHER_SOULFIRE_PERSISTENT_GROUND_INDEPENDENT_V1(2).zip](../../assets/references/revenant-dragon/nether-soulfire/NETHER_SOULFIRE_PERSISTENT_GROUND_INDEPENDENT_V1%282%29.zip) | 229,483,442 | `306f6aaac691f78bcf65d822ec474e678340bf43862a0c23c309fbe84b84af20` |
