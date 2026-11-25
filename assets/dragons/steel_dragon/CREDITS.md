# 강철룡 에셋 출처

사용자가 승인한 A안 원본을 사용했다.

| 원본 | SHA-256 |
|---|---|
| `STEEL_DRAGON_A_HEAVY_VENT_V5.zip` | `039fff959e32d45ac02f2b11fe3cb41ded45854ae5c33a7a60364407a233e84b` |
| `STEEL_DRAGON_A_FLY_24F.png` | `12c557c7e8cea27178d17200e59aff9aa55e877ba11454886240b7f43f9bcfc2` |
| `STEEL_DRAGON_A_FLY_THRUST_FX_V2.zip` | `8fff462ae769e2502e0a4c7fd10586399ba77003706700450f3a8bc32cc052f4` |
| `STEEL_DRAGON_A_FLY_VENT_FX_V2.zip` | `0199da237be01b734221753a6eb96277cc7c6033994f94e1f0918500a3ec849c` |
| `STEEL_DRAGON_A_WALK_16F_V2.zip` | `661d64df054d157ca452767047a8b9138f3983f4fef7d01f776e0bef98364ff8` |
| `STEEL_DRAGON_A_WALK_KEYFRAMES_V2.zip` | `929889082e2c72a05fec07f007fd07f447546bc0c91cd85026b620f0a418f494` |
| `STEEL_DRAGON_A_WALK_VENT_FX_V2_1.zip` | `729e2f642a39037c8189e9d428b0db62e0b2c3a406d9330fabef569495c26fdd` |

FLY는 2400×1588 RGBA, 6열×4행, 셀 400×397px이다. 24개 슬롯에 10개 고유 자세가 들어 있으며 각 슬롯의 RGBA 해시가 두 V2 패키지 기록과 일치한다. 2048×1355 사본의 동일성은 미검증이다.

WALK 마스터 RGBA 7200×3616(4열×4행)의 16개 셀이 개별 PNG와 일치한다. 홀수 8장은 승인 키프레임 ZIP의 PNG와 바이트 단위로 일치한다.

WALK VENT V2.1의 7200×3616 RGBA 시트 6장은 WALK 마스터와 같은 4×4 배열 및 원점 (0, 0)을 사용한다. 개별 레이어와 `alignment.json`의 합성 순서로 변환했다. 동봉 미리보기와 재합성은 F05·F06의 작은 겹침 영역에서만 달랐다.

ATTACK·HIT·DEATH·REST는 기존 `STEEL_DRAGON_IDLE_WING_TAIL_PREVIEW.zip`의 연기 없는 대체 시트다. [기존 승인 패키지](https://chatgpt.com/library?file_id=libfile_c1a7fece9cc4819180dd6810118995b4&account_id=personal)
