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
| `STEEL_DRAGON_A_BASIC_ATTACK_10F_V1.zip` | `30d62628e88ed2d84bcb6f67bb1daf63abca87fb99f3020fb914bf05fec0fd3d` |
| `STEEL_DRAGON_A_BASIC_PROJECTILE_FX_V2.zip` | `94e02cfa16424c4bb8a8e12b6f65f822df77b7c42e22f253a8adbcb39e80df9f` |
| `STEEL_DRAGON_A_BASIC_HIT_FX_V2.zip` | `0063d09efb63be2c3eb9084f895780fc474a7642238caa44eb56eafde27b0a46` |
| `STEEL_DRAGON_A_SKILL01_DOUBLE_SHOCKWAVE_KEYFRAMES_V3.zip` | `91d9b1144dca7ac4f35355c9426c61369fbe120ccd08e616ca6b241301be6738` |

FLY는 2400×1588 RGBA, 6열×4행, 셀 400×397px이다. 24개 슬롯에 10개 고유 자세가 들어 있으며 각 슬롯의 RGBA 해시가 두 V2 패키지 기록과 일치한다. 2048×1355 사본의 동일성은 미검증이다.

WALK 마스터 RGBA 7200×3616(4열×4행)의 16개 셀이 개별 PNG와 일치한다. 홀수 8장은 승인 키프레임 ZIP의 PNG와 바이트 단위로 일치한다.

WALK VENT V2.1의 7200×3616 RGBA 시트 6장은 WALK 마스터와 같은 4×4 배열 및 원점 (0, 0)을 사용한다. 개별 레이어와 `alignment.json`의 합성 순서로 변환했다. 동봉 미리보기와 재합성은 F05·F06의 작은 겹침 영역에서만 달랐다.

ATTACK과 HEAT LEAK는 6400×1360 RGBA(5열×2행), 화염탄은 4096×576, 명중은 1024×512다(둘 다 4열×2행). 승인 레이어 합성과 동봉 합성 시트가 픽셀 단위로 일치했다. 게임용 본체와 발광은 269/1280 균일 배율 및 공통 접지점 변환을 적용했다. 화염탄은 절반 크기의 비정사각형 셀, 명중 FX는 256px 정사각 셀로 각각 한 줄 배열을 사용한다.

HIT·DEATH·REST는 기존 `STEEL_DRAGON_IDLE_WING_TAIL_PREVIEW.zip`의 연기 없는 대체 시트다. [기존 승인 패키지](https://chatgpt.com/library?file_id=libfile_c1a7fece9cc4819180dd6810118995b4&account_id=personal)

노심 과열파 V2는 승인 ZIP의 두 게임 시트를 그대로 사용한다. ZIP SHA-256 `f936fc46d84d788162fbfa689b7351338bf020cb9bf22af79cb01d2130a29920`. 기존 과열 마스크와 본체 시트는 유지한다.

| 게임용 승인 PNG | SHA-256 |
|---|---|
| `fx_overheat_rear.png` | `f336ac68a30a2666efcc9ba5afb2d70d30b9292637849d69efb4f93424985749` |
| `fx_overheat_front.png` | `b24bccb50d48019037058e0ed0300ce37438206eef327b0f7632203bd4f16a88` |

V1은 사용자 제공 용광 해방형 콘셉트와 강철룡 마스터를 참고한 절차적 폭발이었다. 포즈별 열 마스크는 V1에서 기존 본체 원본으로부터 추출했다.

용광 폭렬탄 폭발은 승인 V3의 7개 RGBA 레이어와 키 6장을 보존했다. 신규 중간 컷과 독립 포탄은 저장소의 제작 스크립트로 생성한다. BASIC ATTACK·PROJECTILE 두 패키지는 발사 포즈와 화염탄 스타일의 참조다.
