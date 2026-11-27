// 적 종류의 정의와 등록부.
// 드래곤(Species.h)과 같은 방식이다: 코드에 내장된 기본값이 있고,
// assets/enemies/<키>/enemy.txt 로 덮어쓰거나 새 종류를 추가할 수 있다.
#pragma once

#include <string>

#include "Species.h"  // AnimDef, AnimState

namespace dt {

// 적 종류의 식별자. 값은 등록 순서이며 코드에 하드코딩하지 않는다.
// 키("slime", "lich")로 찾아 쓴다.
enum class EnemyKind : int {};

// 적의 행동 방식. 코드 분기 대신 데이터로 둔다.
enum class EnemyRole {
    Melee,  // 가까이 붙어 때린다(일반 적)
    Boss,   // 거리를 두고 패턴을 쓴다. 밀려나지 않는다
};

// T8(2026-11-25): 일반 몬스터의 공격 모양. enemy.txt 의 [attack] 구역에서 고른다.
// Legacy 는 [attack] 을 적지 않은 적 — 예전처럼 앞쪽 작은 원 + 공통 예고값(cfg::kImp*)을 쓴다.
enum class EnemyAttackKind {
    Legacy,
    Swing,   // 앞쪽 부채꼴
    Slam,    // 원(자기 앞 offset 거리)
    Thrust,  // 앞쪽 가는 줄
    Lunge,   // 착지할 자리 원 → 예고 끝에 달려들어 판정
    Shot,    // 날아갈 줄 예고 → 예고 끝에 투사체가 줄을 따라 날아간다
};

struct EnemyAttackDef {
    EnemyAttackKind kind = EnemyAttackKind::Legacy;
    float windup = 0.85f;    // 예고 시간
    float cooldown = 2.2f;   // 판정이 끝난 뒤 다음 공격까지
    float radius = 56.f;     // Swing·Slam·Lunge: 반경 / Thrust·Shot: 반폭
    float length = 110.f;    // Swing: 부채꼴 길이 / Thrust·Shot: 줄 길이 / Lunge: 달려드는 거리
    float arc = 100.f;       // Swing: 부채꼴 각도(도)
    float offset = 36.f;     // Slam: 원 중심이 몸 앞으로 떨어진 거리
    float shotSpeed = 420.f; // Shot: 투사체 속도(px/초)
};

struct EnemyDef {
    // v1.5: 그림이 어느 쪽을 보고 그려졌는가. 지금 적 그림은 모두 왼쪽을 본다.
    // 드래곤(오른쪽 기준)과 반대라 뒤집기 규칙이 반대로 적용돼 거꾸로 걸어 보였다.
    bool artFacesRight = false;
    EnemyKind id{};
    std::string key;   // 폴더 이름. 저장 파일에 기록된다
    // v1.6: 그림을 다른 적 폴더에서 가져온다(`enemy.txt` 의 `art_from=`).
    // 비어 있으면 자기 폴더에서 읽는다. 시트 파일만 빌려 오고 [anim.*] 설정은 빌리지 않는다 —
    // 변종이 칸 너비·배율을 자기 enemy.txt 에 적어야 한다(원본이 바뀌어도 변종이 따라 흔들리지 않게).
    std::string artFrom;
    std::string name;  // 화면에 보이는 이름
    std::string desc;
    EnemyRole role = EnemyRole::Melee;
    uint32_t color = 0xFFB0B6C8;

    // 1레벨 기준값과 레벨당 증가량
    float baseHp = 40.f, hpGrowth = 15.f;
    float baseAtk = 7.f, atkGrowth = 2.5f;
    float baseDef = 1.f, defGrowth = 0.5f;
    float baseSpeed = 85.f, speedGrowth = 3.f;
    float radius = 24.f;
    int baseExp = 3, expGrowth = 2;

    // 거리 유지와 공격
    float attackRange = 72.f;  // 이 거리 안이면 공격한다
    float keepRatio = 0.8f;    // attackRange * 이 값보다 멀면 다가간다
    float windup = 0.85f;      // 공격 예고 시간. 0이면 기본값을 쓴다
    float attackCooldown = 2.2f;

    // 보스 전용. role == Boss 일 때만 쓰인다
    bool phase2 = false;         // 체력 절반에서 2페이즈로 바뀌는가
    std::string summonKey;       // 2페이즈에서 소환할 적의 키(비어 있으면 소환 없음)
    int summonCount = 0;
    int summonLevel = 5;

    // 분열(10층 필드 보스 슬라임 왕, 2026-11-25). 최대 체력의 split_every 만큼 깎일 때마다
    // split 적을 split_count 마리 낳는다. 분열체가 살아 있는 동안 본체가 받는 피해는 (1 - split_guard) 배.
    // 분열은 공격이 아니므로 예고가 없다. 분열체의 공격은 평소처럼 예고한다.
    std::string splitKey;
    int splitCount = 0;
    float splitEvery = 0.f;   // 0 이면 분열하지 않는다
    int splitLevelDrop = 5;   // 분열체 레벨 = 본체 레벨 − 이 값(최소 1)
    float splitGuard = 0.f;

    EnemyAttackDef attack;
    // [anim.attack] hit_frame — 공격 시트에서 판정이 나는 칸(1부터). 0 이면 시트를 처음부터 재생한다.
    // 예고가 끝나는 순간 이 칸이 보이도록 재생 시작을 늦춘다(그리는 쪽).
    int attackHitFrame = 0;

    AnimDef anims[kAnimStateCount];
};

// 등록부. 드래곤과 같은 규칙이다.
int enemyCount();
const EnemyDef& enemyDef(EnemyKind k);
bool enemyFromKey(const std::string& key, EnemyKind& out);
EnemyKind registerOrUpdateEnemy(const EnemyDef& def);
// 없으면 자리 표시로 등록한다(폴더가 사라진 적의 기록을 지키기 위해).
EnemyKind ensureEnemy(const std::string& key);
// 내장 종류의 수. 폴더로 추가된 것은 포함하지 않는다.
extern const int kBuiltinEnemyCount;

struct EnemyStats {
    float maxHp, atk, def, speed;
    int expReward;
};
EnemyStats enemyStatsFor(EnemyKind k, int level);

// 첫 번째로 등록된 일반 적과 보스. 내장 기본값이 있으므로 항상 유효하다.
EnemyKind defaultMelee();
EnemyKind defaultBoss();

}  // namespace dt
