// 조정 가능한 임시 수치. 기획서에 적힌 대로 모든 값은 플레이 테스트 후 바꾼다.
#pragma once

namespace dt::cfg {

// 성장
constexpr int kMaxLevel = 30;
constexpr int kAutoExpCapLevel = 5;  // 자동 사냥 누적 경험치 상한: 이 레벨 도달에 필요한 총 경험치

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
constexpr float kArenaWidth = 960.f;
constexpr float kArenaHeight = 600.f;

// 저장
constexpr float kAutosaveInterval = 10.f;

}  // namespace dt::cfg
