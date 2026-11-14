// 조정 가능한 임시 수치. 기획서에 적힌 대로 모든 값은 플레이 테스트 후 바꾼다.
#pragma once

namespace dt::cfg {

// 성장
constexpr int kMaxLevel = 30;
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

// 탑
constexpr float kTowerFloorHeal = 0.30f;   // 층 돌파 시 회복 비율
constexpr int kTowerClearExp = 120;
constexpr int kTowerRecommendedLevel = 8;
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
constexpr float kCharScaleMin = 0.5f;   // 캐릭터 크기는 보기 전용이다. 판정 크기는 바뀌지 않는다
constexpr float kCharScaleMax = 2.0f;
constexpr float kCharScaleStep = 0.1f;

constexpr float kArenaWidth = 960.f;
constexpr float kArenaHeight = 600.f;

// 저장
constexpr float kAutosaveInterval = 10.f;

// 바탕화면에서 사냥하지 않을 때 스스로 고르는 행동. 전부 임시값이다.
// 고를 확률(합이 1이 아니어도 비율로 쓰인다)
constexpr float kIdleWeightRest = 3.0f;   // 쉬기(제자리)
constexpr float kIdleWeightWalk = 4.0f;   // 걷기(아래쪽 바닥을 따라)
constexpr float kIdleWeightFly = 2.0f;    // 날기(화면 위쪽까지)
// 한 번 고른 행동을 이어 가는 시간(초)
constexpr float kIdleRestMin = 2.0f;
constexpr float kIdleRestMax = 6.0f;
constexpr float kIdleWalkMin = 4.0f;
constexpr float kIdleWalkMax = 9.0f;
constexpr float kIdleFlyMin = 5.0f;
constexpr float kIdleFlyMax = 11.0f;
// 이동 속도 배율(기본 speed 대비)
constexpr float kIdleWalkSpeed = 0.35f;
constexpr float kIdleFlySpeed = 0.80f;
// 날다가 내려오는 데 쓰는 최대 시간(초)과, 지면에 닿았다고 볼 거리(픽셀)
constexpr float kIdleLandMax = 8.0f;
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

}  // namespace dt::cfg
