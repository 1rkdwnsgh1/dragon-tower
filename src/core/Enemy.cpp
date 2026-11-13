#include "core/Enemy.h"

#include <algorithm>
#include <deque>
#include <unordered_map>

#include "core/Config.h"

namespace dt {

namespace {

// 내장 기본값. assets/enemies/<키>/enemy.txt 로 덮어쓸 수 있다.
// 수치는 예전 임프·수호자의 값을 그대로 옮긴 것이다(임시안).
std::deque<EnemyDef>& registry() {
    static std::deque<EnemyDef> list;
    return list;
}

std::unordered_map<std::string, int>& index() {
    static std::unordered_map<std::string, int> m;
    return m;
}

EnemyDef makeSlime() {
    EnemyDef d;
    d.key = "slime";
    d.name = "슬라임";
    d.desc = "느리지만 끈질기다.";
    d.color = 0xFF6BD36B;
    d.baseHp = 46.f; d.hpGrowth = 16.f;
    d.baseAtk = 6.f; d.atkGrowth = 2.2f;
    d.baseSpeed = 70.f; d.speedGrowth = 2.f;
    d.radius = 24.f;
    d.windup = 0.95f;
    return d;
}

EnemyDef makeGoblin() {
    EnemyDef d;
    d.key = "goblin";
    d.name = "고블린";
    d.desc = "빠르게 파고들어 찌른다.";
    d.color = 0xFF9FBF5A;
    d.baseHp = 40.f; d.hpGrowth = 15.f;
    d.baseAtk = 7.f; d.atkGrowth = 2.5f;
    d.baseSpeed = 95.f; d.speedGrowth = 3.5f;
    d.radius = 24.f;
    d.windup = 0.8f;
    return d;
}

EnemyDef makeOgre() {
    EnemyDef d;
    d.key = "ogre";
    d.name = "오우거";
    d.desc = "한 방이 무겁다. 예고를 보고 피해야 한다.";
    d.color = 0xFF8C9A7A;
    d.baseHp = 70.f; d.hpGrowth = 22.f;
    d.baseAtk = 10.f; d.atkGrowth = 3.2f;
    d.baseDef = 2.f; d.defGrowth = 0.7f;
    d.baseSpeed = 72.f; d.speedGrowth = 2.f;
    d.radius = 32.f;
    d.windup = 1.1f;
    d.attackCooldown = 2.8f;
    d.baseExp = 5; d.expGrowth = 3;
    return d;
}

EnemyDef makeLich() {
    EnemyDef d;
    d.key = "lich";
    d.name = "리치";
    d.desc = "탑의 주인. 멀리서 패턴을 건다.";
    d.role = EnemyRole::Boss;
    d.color = 0xFFB07CE0;
    d.baseHp = 0.f; d.hpGrowth = 110.f;   // 레벨 비례(기존 수호자와 같은 식)
    d.baseAtk = 2.f; d.atkGrowth = 3.f;
    d.baseDef = 0.f; d.defGrowth = 0.75f;
    d.baseSpeed = 70.f; d.speedGrowth = 0.f;
    d.radius = cfg::kGuardianRadius;
    d.attackRange = 700.f;
    d.keepRatio = 240.f / 700.f;
    d.attackCooldown = 1.5f;
    d.baseExp = 40; d.expGrowth = 10;
    d.phase2 = true;
    d.summonKey = "goblin";
    d.summonCount = 2;
    d.summonLevel = 5;
    return d;
}

void ensureBuiltins() {
    if (!registry().empty()) return;
    for (const EnemyDef& d : {makeSlime(), makeGoblin(), makeOgre(), makeLich()}) {
        registerOrUpdateEnemy(d);
    }
}

}  // namespace

const int kBuiltinEnemyCount = 4;

int enemyCount() {
    ensureBuiltins();
    return static_cast<int>(registry().size());
}

const EnemyDef& enemyDef(EnemyKind k) {
    ensureBuiltins();
    int i = static_cast<int>(k);
    if (i < 0 || i >= static_cast<int>(registry().size())) i = 0;
    return registry()[static_cast<size_t>(i)];
}

bool enemyFromKey(const std::string& key, EnemyKind& out) {
    ensureBuiltins();
    auto it = index().find(key);
    if (it == index().end()) return false;
    out = static_cast<EnemyKind>(it->second);
    return true;
}

EnemyKind registerOrUpdateEnemy(const EnemyDef& def) {
    auto it = index().find(def.key);
    if (it != index().end()) {
        EnemyDef& slot = registry()[static_cast<size_t>(it->second)];
        EnemyKind keep = slot.id;
        slot = def;
        slot.id = keep;
        return keep;
    }
    int i = static_cast<int>(registry().size());
    registry().push_back(def);
    registry().back().id = static_cast<EnemyKind>(i);
    index()[def.key] = i;
    return static_cast<EnemyKind>(i);
}

EnemyKind ensureEnemy(const std::string& key) {
    ensureBuiltins();
    EnemyKind k;
    if (enemyFromKey(key, k)) return k;
    EnemyDef d;
    d.key = key;
    d.name = key;
    d.desc = "데이터 파일이 없는 적입니다. assets/enemies/" + key + "/enemy.txt 를 확인하세요.";
    return registerOrUpdateEnemy(d);
}

EnemyStats enemyStatsFor(EnemyKind k, int level) {
    const EnemyDef& d = enemyDef(k);
    float n = static_cast<float>(std::max(1, level));
    EnemyStats s;
    s.maxHp = std::max(1.f, d.baseHp + d.hpGrowth * n);
    s.atk = std::max(0.f, d.baseAtk + d.atkGrowth * n);
    s.def = std::max(0.f, d.baseDef + d.defGrowth * n);
    s.speed = std::max(1.f, d.baseSpeed + d.speedGrowth * n);
    s.expReward = std::max(1, d.baseExp + d.expGrowth * std::max(1, level));
    return s;
}

EnemyKind defaultMelee() {
    ensureBuiltins();
    for (int i = 0; i < enemyCount(); ++i) {
        EnemyKind k = static_cast<EnemyKind>(i);
        if (enemyDef(k).role == EnemyRole::Melee) return k;
    }
    return static_cast<EnemyKind>(0);
}

EnemyKind defaultBoss() {
    ensureBuiltins();
    for (int i = 0; i < enemyCount(); ++i) {
        EnemyKind k = static_cast<EnemyKind>(i);
        if (enemyDef(k).role == EnemyRole::Boss) return k;
    }
    return defaultMelee();
}

}  // namespace dt
