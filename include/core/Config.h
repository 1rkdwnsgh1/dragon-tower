// 조정 가능한 임시 수치. 기획서에 적힌 대로 모든 값은 플레이 테스트 후 바꾼다.
#pragma once

#include <cstddef>

namespace dt::cfg {

// 성장
constexpr int kMaxLevel = 500;  // 탑 100층 확장(2026-11-15 사용자 결정). 경험치 곡선은 미결정
// 자동 전투 경험치 상한. v0.5에서 끄기로 했다(기획서 10장 참고).
// 상한을 다시 켜려면 kAutoExpCapEnabled 를 true 로 바꾼다. 수치와 저장 필드는 그대로 둔다.
constexpr bool kAutoExpCapEnabled = false;
constexpr int kAutoExpCapLevel = 5;  // 상한을 켤 때 쓰는 값

// 크기(픽셀)
constexpr float kDragonRadius = 34.f;
constexpr float kImpRadius = 24.f;
constexpr float kGuardianRadius = 62.f;

// 드래곤 공통
constexpr float kBasicAttackRange = 230.f;
constexpr float kBasicAttackCooldown = 0.9f;
constexpr float kBasicAttackPower = 1.0f;
constexpr float kProjectileSpeed = 520.f;
constexpr float kDodgeDistance = 170.f;    // 직접 조작 회피 이동 거리
constexpr float kDodgeDuration = 0.18f;
constexpr float kDodgeInvuln = 0.32f;      // 회피 중 무적 시간
constexpr float kDodgeCooldown = 1.0f;
constexpr float kDefenseScale = 40.f;      // 피해 = 원래 피해 * 40 / (40 + 방어)

// 회복(초당 최대 체력 비율)
constexpr float kRegenIdle = 0.03f;        // 사냥하지 않을 때
constexpr float kRegenBetweenWaves = 0.015f;

// 바탕화면 사냥
constexpr float kWaveBreak = 15.0f;         // 무리 사이 휴식(이때만 모드 변경 가능)
constexpr int kWaveMin = 1;
constexpr int kWaveMax = 3;

// 적: 임프(일반 적)
constexpr float kImpAttackRange = 72.f;
constexpr float kImpZoneRadius = 56.f;
constexpr float kImpWindup = 0.85f;        // 공격 예고 시간
constexpr float kImpAttackCooldown = 2.2f;

// 적: 탑의 수호자(보스)
constexpr float kGuardianSlamRadius = 135.f;
constexpr float kGuardianBeamLength = 620.f;
constexpr float kGuardianBeamHalfWidth = 32.f;
constexpr float kGuardianMeteorRadius = 62.f;
constexpr float kGuardianPatternCooldown = 2.6f;
constexpr float kGuardianPhase2Speedup = 0.8f;  // 2페이즈 예고·대기 시간 배율

// 스킬 효과(FX)의 세 단계 기본 길이. dragon.txt 의 fx_windup/fx_hold/fx_tail 로 스킬마다 덮어쓴다.
// fx= 를 적은 스킬에만 쓰인다. 적지 않은 스킬은 전처럼 도형이 그 자리의 duration 만큼 보인다.
constexpr float kFxWindup = 0.12f;  // 발동 — 터지기 직전의 짧은 예고
constexpr float kFxHold = 0.25f;    // 지속 — 효과가 온전히 보이는 동안
constexpr float kFxTail = 0.18f;    // 잔류 — 옅어지며 사라진다
constexpr int kMaxFx = 48;          // 쌓이는 효과의 상한. 넘으면 오래된 것부터 버린다
// fx= 가 없는 효과의 단계 나눔 비율(합이 1). 전체 길이는 그대로 두고 단계만 나눈다.
constexpr float kFxShapeWindupRatio = 0.22f;
constexpr float kFxShapeHoldRatio = 0.45f;

// 탑
constexpr float kTowerFloorHeal = 0.30f;   // 층 돌파 시 회복 비율
constexpr int kTowerClearExp = 120;
constexpr int kTowerRecommendedLevel = 8;
// assets/tower/floors.txt 를 읽을 때의 상한. 데이터가 터무니없어도 게임이 버티게 하는 울타리다.
constexpr int kMaxTowerFloors = 100;         // 적을 수 있는 층 수
constexpr int kMaxSpawnLinesPerFloor = 12;  // 한 층의 enemy= 줄 수
constexpr int kMaxEnemiesPerFloor = 8;      // enemy= 한 줄의 마리 수(x<N>)
constexpr int kMaxEnemyLevel = 500;          // enemy= 의 레벨(lv<N>)
// 탑 100층 구조(2026-11-15 사용자 결정). 규칙 함수는 core/Tower.h, 설계는 docs/tower-100-design.md
constexpr int kTowerTopFloor = 100;
constexpr int kMidBossEvery = 5;            // 5의 배수는 중간 보스(10의 배수와 겹치면 필드 보스)
constexpr int kFieldBossEvery = 10;         // 10의 배수는 필드 보스(파티)
constexpr int kFieldBossHpMul = 8;          // 필드 보스층 적의 체력 배율(2026-11-24 사용자 결정: 한 방 피해는 그대로, 체력만 크게. 임시값)
constexpr int kFieldBossFirstPotions = 10;  // 필드 보스 계정 첫 클리어: 탑 물약(소) 묶음(임시값)
constexpr int kFieldBossLevelPerFloor = 5;  // 필드 보스 입장: 출전 드래곤 각각 층 × 5 레벨 이하
constexpr int kFieldBossPartySize = 4;
// v1.8 UI 조각 캐시(큐 12번). 그린 결과를 장치 픽셀 크기로 캐시해 매 프레임 다시 줄이지 않는다.
// 끄면 예전처럼 매번 그린다 — 눈에 보이는 차이가 나면 이것부터 꺼서 확인한다.
// 측정과 이유는 docs/implementation-notes.md "프레임 시간 측정" 참고.
constexpr bool kUiPieceCache = true;
constexpr size_t kUiPieceCacheMaxEntries = 256;             // 항목 수 한도(넘으면 통째로 비운다)
constexpr size_t kUiPieceCacheMaxBytes = size_t{64} * 1024 * 1024;  // 64MB
constexpr size_t kUiPieceCacheMaxPixels = size_t{1920} * 1080;  // 이보다 큰 조각은 캐시하지 않는다
// 그림 캐시(2026-11-19). 스프라이트·효과·장판·투사체·숨결의 한 칸을 화면 크기로 미리 키워 두고
// 정수 좌표에 그대로 찍는다. 키우는 일은 따로 도는 스레드가 한다. 설명은 src/win/FrameCache.h.
// 끄면 예전처럼 매 프레임 확대한다(느리다) — 눈에 보이는 차이가 나면 이것부터 꺼서 확인한다.
constexpr bool kFrameCache = true;
constexpr size_t kFrameCacheMaxBytes = size_t{192} * 1024 * 1024;  // 넘으면 오래 안 쓴 것부터 버린다
// 한 장의 장치 픽셀 상한. 이보다 크게 그려지면 이 크기로 만들고 붙일 때 최근접으로 늘린다.
constexpr size_t kFrameCacheMaxPixels = size_t{1600} * 800;
constexpr size_t kFrameCacheMaxQueue = 48;    // 뒤 스레드에 밀린 일의 상한(넘으면 오래된 것을 버린다)
constexpr unsigned kFrameCacheMaxWorkers = 3;  // 뒤 스레드 수 상한(코어 수 - 1 과 비교해 작은 쪽)
// 키운 그림이 아직 없을 때 이보다 작은 그림은 양선형으로, 큰 그림은 최근접으로 대신 그린다(둘 다 빠르다).
constexpr size_t kFrameCacheSmoothFallbackPixels = size_t{400} * 400;
// 캐릭터 크기를 바꾼 뒤 같은 크기가 이만큼 프레임 이어져야 숨결 16장을 미리 만든다(끄는 동안 반복 생성 방지).
constexpr int kBreathCacheSettleFrames = 10;

// 바탕화면 디펜스 전투(v0.5). 전투는 화면 맨 아래 띠에서만 일어난다.
// 띠 높이는 "드래곤이 전투 중에 움직일 수 있는 세로 폭"이다. 그림은 바닥선 위로 그려지므로
// 띠보다 커도 잘리지 않는다. v0.7의 150은 떠 있는 느낌이라 50으로 내렸다.
constexpr float kBandHeight = 50.f;       // 작업 표시줄 바로 위 전투 띠의 높이
constexpr float kBandBottomMargin = 8.f;  // 띠 아래 여백
constexpr float kSpawnOffsetX = 60.f;     // 왼쪽 화면 밖에서 이만큼 떨어져 등장
constexpr float kWaveLevelStep = 3;       // 웨이브 이만큼마다 적 수가 한 마리 늘어난다

// 보기 설정(v0.8). 저장 파일의 `ui.menuScale`·`ui.charScale` 범위이자 버튼 한 번에 바뀌는 폭이다.
constexpr float kMenuScaleMin = 0.7f;
constexpr float kMenuScaleMax = 1.6f;
constexpr float kMenuScaleStep = 0.1f;
// v1.4: 메뉴 모서리를 끌 때 배수 1.0이 바뀌는 데 필요한 픽셀 수. 클수록 천천히 바뀐다.
constexpr float kMenuScaleDragPx = 420.f;
// 캐릭터 크기는 2026-11-17부터 **판정에도** 쓴다(그전에는 보기 전용이었다).
// 몸 반지름(드래곤·몬스터)과 몬스터 공격 사거리는 항상 따라 커지고,
// 스킬 판정 반경은 `scale_with_size` 를 켠 스킬만 커진다. 스킬 사거리는 커지지 않는다.
constexpr float kCharScaleMin = 0.5f;
// v1.9: 상한을 2.0 → 10.0(1000%)으로 올렸다. 그림만 커지고 맞는 거리·공격 범위는 그대로다.
// 아주 크게 키우면 드래곤이 화면 밖으로 삐져나와 보이는데, 서 있는 자리(판정)는 화면 안이라 정상이다.
constexpr float kCharScaleMax = 10.0f;
constexpr float kCharScaleStep = 0.1f;

constexpr float kArenaWidth = 960.f;
constexpr float kArenaHeight = 600.f;

// 저장
constexpr float kAutosaveInterval = 10.f;

// 바탕화면에서 사냥하지 않을 때 스스로 고르는 행동. 전부 임시값이다.
// 고를 확률(합이 1이 아니어도 비율로 쓰인다)
constexpr float kIdleWeightRest = 25.f;   // 쉬기(제자리)
constexpr float kIdleWeightWalk = 10.f;   // 걷기(아래쪽 바닥을 따라)
constexpr float kIdleWeightFly = 65.f;    // 날기(화면 위쪽까지)
// 한 번 고른 행동을 이어 가는 시간(초)
constexpr float kIdleRestMin = 2.0f;
constexpr float kIdleRestMax = 6.0f;
constexpr float kIdleWalkMin = 1.5f;
constexpr float kIdleWalkMax = 3.0f;
constexpr float kIdleFlyMin = 5.0f;
constexpr float kIdleFlyMax = 11.0f;
// 이동 속도 배율(기본 speed 대비)
constexpr float kIdleWalkSpeed = 0.35f;
constexpr float kIdleFlySpeed = 0.80f;
// 날다가 내려오는 데 쓰는 최대 시간(초)과, 지면에 닿았다고 볼 거리(픽셀)
constexpr float kIdleLandMax = 8.0f;
constexpr float kRestLieDelay = 2.0f;   // 이만큼 가만히 쉬면 엎드린다(쉬는 모션)

// v0.9 싸울 때 유지할 간격(스킬 수동·자동에서 AI가 지킨다). 0 근접 / 1 중간 / 2 바깥.
constexpr int kKeepRangeCount = 3;
constexpr float kKeepNearRatio = 0.70f;   // 근접: 기본 공격 사거리의 70%
constexpr float kKeepMidRatio = 1.00f;    // 중간: 기본 공격이 겨우 닿는 거리
constexpr float kKeepFarRatio = 0.80f;    // 바깥: 쓸 수 있는 가장 긴 스킬 사거리의 80%
constexpr float kKeepSlack = 24.f;        // 이보다 가까워지면 물러난다(제자리걸음 방지)

// v1.3 드래곤 보유(관리창). 동시에 가질 수 있는 마리 수와 보관 중 회복 속도.
// 보관 중에는 경험치가 오르지 않는다(한 번에 한 마리만 키운다 = 영구 사망의 무게를 지킨다).
constexpr int kMaxStable = 4;
// 탑 100층 T2(2026-11-18): 계정이 20의 배수 필드 보스층을 깰 때마다 슬롯이 하나씩 는다(최대 +5).
// 늘어난 슬롯을 실제로 쓰는 것은 T6 이다. 저장 파일은 이 값까지 받아 둔다.
constexpr int kStableBonusEvery = 20;
constexpr int kMaxStableTotal = kMaxStable + kTowerTopFloor / kStableBonusEvery;
// 크레딧 저장 상한(64비트). 손으로 고친 저장 파일이 넘쳐도 읽을 때 이 값으로 자른다.
constexpr long long kMaxCredits = 999999999999LL;

// ---- 탑 100층 T6(2026-11-24): 크레딧·상점·탑 물약. 모두 임시값(tower-100-design.md 4.4) ----
constexpr int kCreditKillBase = 2;          // 바탕화면 처치 = 2 + 적 레벨 ÷ kCreditKillLevelDiv
constexpr int kCreditKillLevelDiv = 5;
constexpr int kCreditEliteMul = 3;          // 정예 처치 배율
constexpr float kCreditAutoMul = 0.7f;      // 자동 사냥 배율(버림, 최소 1)
constexpr int kCreditFloorFirst = 10;       // 일반층 첫 돌파 = 층 × 이 값
constexpr int kCreditMidBossFirst = 30;     // 중간 보스층 첫 돌파
constexpr int kCreditFieldBossFirst = 100;  // 필드 보스층 첫 돌파(파티, T7)
constexpr float kCreditRepeatRatio = 0.2f;  // 계정이 이미 깬 층을 다른 드래곤이 돌파
constexpr long long kEggBasePrice = 3000;   // 알 = 기본 + 단계 × (살아 있는 드래곤 수 − 1)
constexpr long long kEggStepPrice = 1000;
constexpr long long kPotionSmallPrice = 50;
constexpr long long kPotionLargePrice = 150;
constexpr float kPotionSmallHeal = 0.30f;   // 최대 체력 비율
constexpr float kPotionLargeHeal = 0.60f;
constexpr long long kPotionFuryPrice = 200;   // 분노의 물약: kFuryTime 초 동안 공격력 × kFuryAtkMul
constexpr long long kPotionGuardPrice = 200;  // 수호의 물약: kGuardTime 초 동안 받는 피해 × kGuardDamageMul
constexpr long long kPotionSwiftPrice = 200;  // 신속의 물약: kSwiftTime 초 동안 스킬 쿨타임이 kSwiftCdMul 배 빨리 돈다
constexpr float kFuryTime = 20.f, kFuryAtkMul = 1.25f;
constexpr float kGuardTime = 15.f, kGuardDamageMul = 0.70f;
constexpr float kSwiftTime = 20.f, kSwiftCdMul = 1.5f;
constexpr long long kHunterCharmPrice = 500;  // 사냥꾼의 부적: 실제 시간 kHunterCharmSeconds 동안 처치 경험치 × kHunterCharmExpMul
constexpr long long kHunterCharmSeconds = 30LL * 60LL; // (직접 조작·스킬 수동만. 자동 사냥 제외 — 2026-11-24 사용자 결정)
constexpr float kHunterCharmExpMul = 1.5f;
constexpr float kPotionDrinkTime = 1.0f;    // 마시는 데 걸리는 시간(초)
constexpr float kPotionDrinkMove = 0.5f;    // 마시는 동안 이동 속도 배율
constexpr int kFreeDragonPicks = 4;       // 처음에 공짜로 고르는 드래곤 수(2026-11-24 사용자 결정)
constexpr int kMaxInventory = 9999;         // 물약·알 보유 상한(저장 값 범위)
// v1.4 드래곤 하나가 가질 수 있는 스킬 수의 상한. 제한이 필요해서가 아니라
// dragon.txt에 [skill.9999] 같은 오타가 들어왔을 때 메모리를 그만큼 잡지 않으려는 안전장치다.
// 레벨마다 하나씩 해제되므로 최고 레벨보다 많아 봐야 열리지 않는다.
constexpr int kMaxSkillsPerDragon = 30;  // 최고 레벨과 따로 고정한다(레벨을 500으로 올려도 스킬 칸은 그대로)
constexpr float kStableRegenRatio = 0.02f;  // 1초에 최대 체력의 이만큼 회복(0에서 꽉 차기까지 50초)

// v0.9 체력 막대 그림이 바뀌는 지점. 남은 비율이 이 아래면 다음 그림을 쓴다.
constexpr float kMidHpRatio = 0.55f;
constexpr float kLowHpRatio = 0.25f;
constexpr float kGroundEpsilon = 2.0f;
// 걷기는 화면 아래 이만큼 아래에서만, 날기는 위쪽 이만큼까지 올라간다(화면 높이 비율)
constexpr float kIdleWalkTopRatio = 0.55f;
constexpr float kIdleFlyTopRatio = 0.08f;

// 조용할 때(전투·효과·입력 없음) 다시 그리는 간격(초). 0.1이면 약 10fps.
constexpr float kIdleFrameInterval = 0.1f;

// 애니메이션(1회 재생 상태를 유지하는 시간). 시트가 더 길면 그만큼만 보인다.
constexpr float kAnimAttackHold = 0.35f;
constexpr float kAnimHitHold = 0.22f;
constexpr float kAnimDeathHold = 3.0f;
// v1.4 스킬 전용 본체 모션(skill_*.png)을 유지하는 시간. 공용 attack(0.35초)보다 길다 —
// 스킬 모션은 충전·발동·복귀가 들어 있어 더 길게 그려져 있다. 시트가 짧으면 마지막 칸에서 멈춘다.
constexpr float kSkillMotionHold = 0.9f;
// 2026-11-18: 타격 프레임 스킬(hit_frame=). 타격 뒤에도 이만큼 제자리에 서 있는다(효과가 몸에서 떨어지지 않게).
constexpr float kSkillCastRecover = 0.25f;
// 타격 전에 끊기면(직접 조작 회피 등) 쿨타임을 이 비율만큼만 남긴다. 임시값
constexpr float kSkillCastCancelCooldown = 0.5f;

}  // namespace dt::cfg
