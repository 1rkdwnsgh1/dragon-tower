#include "core/World.h"

#include <algorithm>
#include <cstdio>

#include "core/Config.h"

namespace dt {

float mitigate(float raw, float def) {
    return raw * cfg::kDefenseScale / (cfg::kDefenseScale + std::max(0.f, def));
}

namespace {
std::string intText(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", std::max(1, static_cast<int>(v + 0.5f)));
    return buf;
}
constexpr uint32_t kColorDamageToDragon = 0xFFFF5A5A;
constexpr uint32_t kColorDamageToEnemy = 0xFFFFF2B0;
constexpr uint32_t kColorDodge = 0xFF9FF0FF;
constexpr uint32_t kColorHeal = 0xFF7CFF8A;
}  // namespace

World::World() = default;

void World::setupDragon(Species s, int level, float hp, Vec2 pos) {
    dragon_ = DragonActor{};
    dragon_.species = s;
    dragon_.level = level;
    dragon_.stats = statsFor(s, level);
    dragon_.hp = (hp <= 0.f) ? dragon_.stats.maxHp : std::min(hp, dragon_.stats.maxHp);
    dragon_.pos = pos;
    dragon_.wanderTarget = pos;
    dragon_.groundY = pos.y;  // 처음 선 자리를 걷는 높이로 삼는다
    dragon_.alive = true;
}

void World::setDragonAnim(AnimState s, float hold) {
    DragonActor& d = dragon_;
    // 사망 상태는 어떤 것으로도 덮이지 않는다.
    if (d.anim == AnimState::Death) return;
    if (d.anim != s) {
        d.anim = s;
        d.animTime = 0.f;
    }
    d.animHold = std::max(d.animHold, hold);
}

void World::updateDragonAnim(float dt) {
    DragonActor& d = dragon_;
    d.animTime += dt;
    d.animHold = std::max(0.f, d.animHold - dt);
    if (!d.alive) {
        if (d.anim != AnimState::Death) {
            d.anim = AnimState::Death;
            d.animTime = 0.f;
            d.animHold = cfg::kAnimDeathHold;
        }
        return;
    }
    if (d.animHold > 0.f) return;  // 1회 재생이 끝나지 않았다
    // 날고 있으면 잠시 멈춰 있어도 공중이므로 Fly를 유지한다.
    AnimState want = d.flying ? AnimState::Fly : (d.moving ? AnimState::Move : AnimState::Idle);
    if (d.anim != want) {
        d.anim = want;
        d.animTime = 0.f;
    }
}

void World::setDragonLevel(int level) {
    float oldMax = dragon_.stats.maxHp;
    dragon_.level = level;
    dragon_.stats = statsFor(dragon_.species, level);
    dragon_.hp = std::min(dragon_.stats.maxHp, dragon_.hp + (dragon_.stats.maxHp - oldMax));
}

bool World::skillUnlocked(int i) const {
    if (i < 0 || i > 2) return false;
    return dragon_.level >= speciesDef(dragon_.species).skills[i].unlockLevel;
}

Enemy& World::spawnEnemy(EnemyKind kind, int level, Vec2 pos) {
    Enemy e;
    e.id = nextEnemyId_++;
    e.kind = kind;
    e.level = std::max(1, level);
    e.pos = pos;
    float n = static_cast<float>(e.level);
    if (kind == EnemyKind::Imp) {
        e.radius = cfg::kImpRadius;
        e.maxHp = 40.f + 15.f * n;
        e.atk = 7.f + 2.5f * n;
        e.def = 1.f + 0.5f * n;
        e.speed = 85.f + 3.f * n;
        e.expReward = 3 + 2 * e.level;
        e.attackCd = rng_.uniform(0.6f, 1.6f);
    } else {
        e.radius = cfg::kGuardianRadius;
        e.maxHp = 110.f * n;
        e.atk = 3.f * n + 2.f;
        e.def = 0.75f * n;
        e.speed = 70.f;
        e.expReward = 40 + 10 * e.level;
        e.attackCd = 1.5f;
    }
    e.hp = e.maxHp;
    enemies_.push_back(e);
    events_.push_back({WorldEventType::EnemySpawned, kind, 0});
    addFx(FxType::Poof, pos, pos, e.radius * 1.4f, 0.4f, 0xFFB08CFF);
    return enemies_.back();
}

Vec2 World::randomEdgePoint(float margin) {
    int side = rng_.range(0, 3);
    float x = rng_.uniform(bounds_.x + margin, bounds_.right() - margin);
    float y = rng_.uniform(bounds_.y + margin, bounds_.bottom() - margin);
    switch (side) {
        case 0: return {bounds_.x + margin, y};
        case 1: return {bounds_.right() - margin, y};
        case 2: return {x, bounds_.y + margin};
        default: return {x, bounds_.bottom() - margin};
    }
}

void World::clearEnemies(bool withPoof) {
    if (withPoof) {
        for (const Enemy& e : enemies_)
            if (e.alive) addFx(FxType::Poof, e.pos, e.pos, e.radius * 1.4f, 0.4f, 0xFFB08CFF);
    }
    enemies_.clear();
    zones_.clear();
    projectiles_.clear();
    dots_.clear();
}

int World::aliveEnemyCount() const {
    int n = 0;
    for (const Enemy& e : enemies_)
        if (e.alive) ++n;
    return n;
}

Enemy* World::nearestEnemy(Vec2 from, float maxDist) {
    Enemy* best = nullptr;
    float bestD = maxDist;
    for (Enemy& e : enemies_) {
        if (!e.alive) continue;
        float d = distance(from, e.pos) - e.radius;
        if (d <= bestD) {
            bestD = d;
            best = &e;
        }
    }
    return best;
}

Enemy* World::findEnemy(int id) {
    for (Enemy& e : enemies_)
        if (e.id == id && e.alive) return &e;
    return nullptr;
}

void World::addText(const std::string& text, Vec2 pos, uint32_t color, float size, float dur) {
    FloatText t;
    t.text = text;
    t.pos = pos;
    t.color = color;
    t.size = size;
    t.duration = dur;
    texts_.push_back(t);
}

void World::addFx(FxType type, Vec2 a, Vec2 b, float radius, float dur, uint32_t color) {
    Fx f;
    f.type = type;
    f.a = a;
    f.b = b;
    f.radius = radius;
    f.duration = dur;
    f.color = color;
    fx_.push_back(f);
}

std::vector<WorldEvent> World::takeEvents() {
    std::vector<WorldEvent> out;
    out.swap(events_);
    return out;
}

void World::healPercent(float pct) {
    if (!dragon_.alive) return;
    dragon_.hp = std::min(dragon_.stats.maxHp, dragon_.hp + dragon_.stats.maxHp * pct);
}

void World::regen(float pctPerSec, float dt) {
    if (!dragon_.alive) return;
    dragon_.hp = std::min(dragon_.stats.maxHp, dragon_.hp + dragon_.stats.maxHp * pctPerSec * dt);
}

float World::dragonDodgeChance() const {
    float c = dragon_.stats.dodge;
    if (dragon_.hasteTime > 0.f) c += dragon_.hasteDodge;
    return std::min(0.8f, c);
}

void World::moveDragon(float dt, Vec2 dir, float speedMul) {
    DragonActor& d = dragon_;
    d.moving = false;
    if (d.dashTime > 0.f) {
        d.pos += d.dashVel * dt;
        d.moving = true;
    } else if (dir.length() > 0.01f) {
        float speed = d.stats.speed * speedMul * (d.hasteTime > 0.f ? 1.f + d.hasteSpeed : 1.f);
        d.pos += dir.normalized() * (speed * dt);
        d.moving = true;
    }
    if (std::fabs(dir.x) > 0.01f) d.facing = Vec2{dir.x > 0.f ? 1.f : -1.f, 0.f};
    d.pos = bounds_.clampPoint(d.pos, cfg::kDragonRadius);
}

void World::basicAttack() {
    DragonActor& d = dragon_;
    if (d.basicCd > 0.f) return;
    Enemy* t = nearestEnemy(d.pos, cfg::kBasicAttackRange);
    if (!t) return;
    Projectile p;
    p.pos = d.pos;
    p.targetId = t->id;
    p.speed = cfg::kProjectileSpeed;
    p.damage = d.stats.atk * cfg::kBasicAttackPower;
    projectiles_.push_back(p);
    d.basicCd = cfg::kBasicAttackCooldown;
    if (std::fabs(t->pos.x - d.pos.x) > 4.f) d.facing = Vec2{t->pos.x > d.pos.x ? 1.f : -1.f, 0.f};
    setDragonAnim(AnimState::Attack, cfg::kAnimAttackHold);
}

bool World::castSkill(int i, bool aiChoice) {
    DragonActor& d = dragon_;
    const SpeciesDef& sp = speciesDef(d.species);
    const SkillDef& s = sp.skills[i];
    if (!skillUnlocked(i)) {
        if (!aiChoice) addText("Lv." + std::to_string(s.unlockLevel) + " 필요", d.pos - Vec2{0.f, 60.f}, 0xFFCCCCCC, 14.f, 0.8f);
        return false;
    }
    if (d.skillCd[i] > 0.f) return false;

    Vec2 facingDir = d.facing.length() > 0.01f ? d.facing : Vec2{1.f, 0.f};
    switch (s.kind) {
        case SkillKind::Projectile: {
            Enemy* t = nearestEnemy(d.pos, s.range);
            if (!t) {
                if (!aiChoice) addText("대상 없음", d.pos - Vec2{0.f, 60.f}, 0xFFCCCCCC, 14.f, 0.6f);
                return false;
            }
            Projectile p;
            p.pos = d.pos;
            p.targetId = t->id;
            p.speed = cfg::kProjectileSpeed * 1.1f;
            p.damage = d.stats.atk * s.power;
            p.big = true;
            projectiles_.push_back(p);
            break;
        }
        case SkillKind::Burst: {
            bool any = false;
            for (Enemy& e : enemies_)
                if (e.alive && distance(e.pos, d.pos) <= s.radius + e.radius) any = true;
            if (aiChoice && !any) return false;
            for (Enemy& e : enemies_) {
                if (!e.alive || distance(e.pos, d.pos) > s.radius + e.radius) continue;
                damageEnemy(e, d.stats.atk * s.power);
                if (s.extra > 0.f) e.stun = std::max(e.stun, s.extra);
            }
            addFx(FxType::Ring, d.pos, d.pos, s.radius, 0.45f, sp.color);
            break;
        }
        case SkillKind::Line: {
            Enemy* t = nearestEnemy(d.pos, s.range + 80.f);
            if (aiChoice && !t) return false;
            Vec2 dir = t ? (t->pos - d.pos).normalized() : facingDir;
            Vec2 end = d.pos + dir * s.range;
            for (Enemy& e : enemies_) {
                if (!e.alive) continue;
                if (distancePointSegment(e.pos, d.pos, end) <= s.radius + e.radius) damageEnemy(e, d.stats.atk * s.power);
            }
            addFx(FxType::LineBlast, d.pos, end, s.radius, 0.45f, sp.color);
            break;
        }
        case SkillKind::Dash: {
            Enemy* t = nearestEnemy(d.pos, s.range + 150.f);
            if (aiChoice && !t) return false;
            Vec2 dir = t ? (t->pos - d.pos).normalized() : facingDir;
            if (dir.length() < 0.01f) dir = facingDir;
            Vec2 end = bounds_.clampPoint(d.pos + dir * s.range, cfg::kDragonRadius);
            for (Enemy& e : enemies_) {
                if (!e.alive) continue;
                if (distancePointSegment(e.pos, d.pos, end) <= s.radius + e.radius) damageEnemy(e, d.stats.atk * s.power);
            }
            addFx(FxType::Dash, d.pos, end, cfg::kDragonRadius, 0.35f, sp.color);
            const float dashDur = 0.2f;
            d.dashTime = dashDur;
            d.dashVel = (end - d.pos) * (1.f / dashDur);
            d.invuln = std::max(d.invuln, 0.3f);
            break;
        }
        case SkillKind::Heal: {
            if (aiChoice && d.hp > d.stats.maxHp * 0.55f) return false;
            float amount = d.stats.maxHp * s.power;
            d.hp = std::min(d.stats.maxHp, d.hp + amount);
            addFx(FxType::Heal, d.pos, d.pos, 60.f, 0.7f, kColorHeal);
            addText("+" + intText(amount), d.pos - Vec2{0.f, 50.f}, kColorHeal, 18.f);
            break;
        }
        case SkillKind::Shield: {
            if (aiChoice) {
                bool threat = !zones_.empty() || nearestEnemy(d.pos, 220.f) != nullptr;
                if (!threat) return false;
            }
            d.shieldTime = s.duration;
            d.shieldPct = s.power;
            addFx(FxType::Shield, d.pos, d.pos, 56.f, 0.5f, sp.color);
            break;
        }
        case SkillKind::Haste: {
            if (aiChoice && !nearestEnemy(d.pos, 350.f)) return false;
            d.hasteTime = s.duration;
            d.hasteSpeed = s.power;
            d.hasteDodge = s.extra;
            addFx(FxType::Dash, d.pos, d.pos, 50.f, 0.4f, sp.color);
            break;
        }
        case SkillKind::Zone: {
            Enemy* t = nearestEnemy(d.pos, s.range);
            if (!t) {
                if (!aiChoice) addText("대상 없음", d.pos - Vec2{0.f, 60.f}, 0xFFCCCCCC, 14.f, 0.6f);
                return false;
            }
            DotZone z;
            z.pos = t->pos;
            z.radius = s.radius;
            z.dps = d.stats.atk * s.power;
            z.remaining = s.duration;
            z.duration = s.duration;
            dots_.push_back(z);
            break;
        }
    }
    d.skillCd[i] = s.cooldown;
    setDragonAnim(AnimState::Attack, cfg::kAnimAttackHold);
    addText(s.name, d.pos - Vec2{0.f, 78.f}, sp.color | 0xFF000000, 15.f, 0.9f);
    return true;
}

void World::dragonAi(float dt, bool castSkills) {
    DragonActor& d = dragon_;
    Enemy* t = nearestEnemy(d.pos, 1e9f);
    if (!t) {
        wander(dt);  // 애니메이션은 updateCombat 끝에서 한 번만 진행한다
        return;
    }
    float desired = cfg::kBasicAttackRange * 0.7f;
    float dist = distance(d.pos, t->pos) - t->radius;
    Vec2 dir{0.f, 0.f};
    if (dist > desired) dir = (t->pos - d.pos).normalized();
    moveDragon(dt, dir, 1.f);
    if (dir.length() < 0.01f && std::fabs(t->pos.x - d.pos.x) > 4.f) d.facing = Vec2{t->pos.x > d.pos.x ? 1.f : -1.f, 0.f};
    if (castSkills) {
        for (int i = 0; i < 3; ++i) {
            if (skillReady(i) && castSkill(i, true)) break;
        }
    }
}

void World::updateWander(float dt) {
    wander(dt);
    updateDragonAnim(dt);
}

const char* idleBehaviorName(IdleBehavior b) {
    switch (b) {
        case IdleBehavior::Rest: return "쉬는 중";
        case IdleBehavior::Walk: return "걷는 중";
        case IdleBehavior::Fly: return "나는 중";
    }
    return "";
}

float World::randomRoamX() {
    const float m = 80.f;
    return rng_.uniform(bounds_.x + m, bounds_.right() - m);
}

Vec2 World::randomRoamTarget(bool fly) {
    const float m = 80.f;
    // 걷기는 아래쪽 바닥을 따라, 날기는 화면 위쪽까지 올라간다.
    float topRatio = fly ? cfg::kIdleFlyTopRatio : cfg::kIdleWalkTopRatio;
    float top = bounds_.y + bounds_.h * topRatio;
    float bottom = bounds_.bottom() - m;
    if (top > bottom) top = bottom;  // 화면이 아주 낮을 때를 대비
    return {rng_.uniform(bounds_.x + m, bounds_.right() - m), rng_.uniform(top, bottom)};
}

float World::randomGroundY() {
    const float m = 80.f;
    float top = bounds_.y + bounds_.h * cfg::kIdleWalkTopRatio;
    float bottom = bounds_.bottom() - m;
    if (top > bottom) top = bottom;
    return rng_.uniform(top, bottom);
}

void World::pickBehavior() {
    DragonActor& d = dragon_;

    // 공중에 있으면 먼저 내려온다. 걷기는 땅에서만 하고, 내려오는 동안에도 나는 모습이다.
    if (std::fabs(d.pos.y - d.groundY) > cfg::kGroundEpsilon) {
        d.behavior = IdleBehavior::Fly;
        d.landing = true;
        d.flying = true;
        d.groundY = randomGroundY();
        d.wanderTarget = {d.pos.x, d.groundY};
        d.behaviorLeft = cfg::kIdleLandMax;  // 도착하면 곧바로 다시 고른다
        return;
    }

    d.landing = false;
    float total = cfg::kIdleWeightRest + cfg::kIdleWeightWalk + cfg::kIdleWeightFly;
    float r = rng_.uniform(0.f, total);
    if (r < cfg::kIdleWeightRest) {
        d.behavior = IdleBehavior::Rest;
        d.behaviorLeft = rng_.uniform(cfg::kIdleRestMin, cfg::kIdleRestMax);
        d.wanderTarget = d.pos;
    } else if (r < cfg::kIdleWeightRest + cfg::kIdleWeightWalk) {
        // 걷기는 수평 이동만 한다. 목적지의 y는 지금 서 있는 높이 그대로다.
        d.behavior = IdleBehavior::Walk;
        d.behaviorLeft = rng_.uniform(cfg::kIdleWalkMin, cfg::kIdleWalkMax);
        d.wanderTarget = {randomRoamX(), d.groundY};
    } else {
        d.behavior = IdleBehavior::Fly;
        d.behaviorLeft = rng_.uniform(cfg::kIdleFlyMin, cfg::kIdleFlyMax);
        d.wanderTarget = randomRoamTarget(true);
    }
    d.flying = d.behavior == IdleBehavior::Fly;
}

void World::wander(float dt) {
    DragonActor& d = dragon_;
    if (d.dashTime > 0.f) {
        moveDragon(dt, {0.f, 0.f}, 1.f);
        return;
    }

    d.behaviorLeft -= dt;
    if (d.behaviorLeft <= 0.f) pickBehavior();

    if (d.behavior == IdleBehavior::Rest) {
        d.flying = false;
        d.moving = false;
        return;
    }

    // 목적지에 닿았을 때
    if (distance(d.pos, d.wanderTarget) < 8.f) {
        if (d.landing) {
            d.pos.y = d.groundY;  // 지면에 정확히 맞춘다
            pickBehavior();       // 내려왔으니 바로 다음 행동을 고른다
            d.moving = false;
            return;               // 착지한 프레임에는 더 움직이지 않는다
        }
        if (d.behavior == IdleBehavior::Walk) {
            d.wanderTarget = {randomRoamX(), d.groundY};  // 수평으로만
        } else {
            d.wanderTarget = randomRoamTarget(true);
        }
    }

    d.flying = d.behavior == IdleBehavior::Fly;
    float mul = d.behavior == IdleBehavior::Fly ? cfg::kIdleFlySpeed : cfg::kIdleWalkSpeed;
    Vec2 dir = d.wanderTarget - d.pos;
    float step = d.stats.speed * mul * dt;
    if (dir.length() <= step) {
        d.pos = d.wanderTarget;
        d.moving = false;
    } else {
        moveDragon(dt, dir, mul);
    }
}

void World::enemyAttack(Enemy& e) {
    Vec2 toDragon = dragon_.pos - e.pos;
    Vec2 dir = toDragon.normalized();
    if (dir.length() < 0.01f) dir = {e.facing, 0.f};
    if (e.kind == EnemyKind::Imp) {
        AttackZone z;
        z.shape = ZoneShape::Circle;
        z.a = e.pos + dir * 36.f;
        z.radius = cfg::kImpZoneRadius;
        z.windup = cfg::kImpWindup;
        z.damage = e.atk;
        z.ownerId = e.id;
        zones_.push_back(z);
        e.castTimer = z.windup;
        e.attackCd = cfg::kImpAttackCooldown + z.windup;
        return;
    }
    // 수호자 패턴: 직선 광선 → 낙석 3연타 → 근접 내려찍기(가까울 때)
    float k = e.phase2 ? cfg::kGuardianPhase2Speedup : 1.f;
    float dist = toDragon.length();
    int pattern = e.patternIndex % 3;
    if (pattern == 2 && dist > cfg::kGuardianSlamRadius + 60.f) pattern = 0;
    if (dist < cfg::kGuardianSlamRadius * 0.9f) pattern = 2;
    e.patternIndex++;
    float cast = 1.0f * k;
    if (pattern == 0) {
        AttackZone z;
        z.shape = ZoneShape::Line;
        z.a = e.pos;
        z.b = e.pos + dir * cfg::kGuardianBeamLength;
        z.radius = cfg::kGuardianBeamHalfWidth;
        z.windup = 1.0f * k;
        z.damage = e.atk * 1.3f;
        z.ownerId = e.id;
        zones_.push_back(z);
        cast = z.windup;
    } else if (pattern == 1) {
        for (int i = 0; i < 3; ++i) {
            AttackZone z;
            z.shape = ZoneShape::Circle;
            Vec2 off = i == 0 ? Vec2{0.f, 0.f} : Vec2{rng_.uniform(-90.f, 90.f), rng_.uniform(-90.f, 90.f)};
            z.a = bounds_.clampPoint(dragon_.pos + off, 20.f);
            z.radius = cfg::kGuardianMeteorRadius;
            z.windup = (1.0f + 0.3f * i) * k;
            z.damage = e.atk * 1.0f;
            z.ownerId = e.id;
            zones_.push_back(z);
        }
        cast = 0.8f * k;
    } else {
        AttackZone z;
        z.shape = ZoneShape::Circle;
        z.a = e.pos;
        z.radius = cfg::kGuardianSlamRadius;
        z.windup = 1.2f * k;
        z.damage = e.atk * 1.6f;
        z.ownerId = e.id;
        zones_.push_back(z);
        cast = z.windup;
    }
    e.castTimer = cast;
    e.attackCd = cfg::kGuardianPatternCooldown * k + cast;
}

void World::updateEnemies(float dt) {
    for (Enemy& e : enemies_) {
        if (!e.alive) continue;
        e.hitFlash = std::max(0.f, e.hitFlash - dt);
        if (e.stun > 0.f) {
            e.stun -= dt;
            continue;
        }
        e.attackCd -= dt;
        if (e.castTimer > 0.f) {
            e.castTimer -= dt;
            continue;
        }
        if (!dragon_.alive) continue;

        if (e.kind == EnemyKind::Guardian && !e.phase2 && e.hp < e.maxHp * 0.5f) {
            e.phase2 = true;
            addText("수호자가 분노했다!", e.pos - Vec2{0.f, 90.f}, 0xFFFF7070, 20.f, 1.6f);
            if (!e.spawnedAdds) {
                e.spawnedAdds = true;
                Vec2 p1 = bounds_.clampPoint(e.pos + Vec2{-120.f, -80.f}, 40.f);
                Vec2 p2 = bounds_.clampPoint(e.pos + Vec2{-120.f, 80.f}, 40.f);
                spawnEnemy(EnemyKind::Imp, 5, p1);
                spawnEnemy(EnemyKind::Imp, 5, p2);
                return;  // 벡터가 재할당되었을 수 있으므로 이번 프레임은 여기서 멈춘다
            }
        }

        Vec2 to = dragon_.pos - e.pos;
        float dist = to.length();
        if (std::fabs(to.x) > 4.f) e.facing = to.x > 0.f ? 1.f : -1.f;
        float attackRange = e.kind == EnemyKind::Imp ? cfg::kImpAttackRange : 700.f;
        float keepDist = e.kind == EnemyKind::Imp ? cfg::kImpAttackRange * 0.8f : 240.f;
        if (dist <= attackRange && e.attackCd <= 0.f) {
            enemyAttack(e);
            continue;
        }
        if (dist > keepDist) e.pos += to.normalized() * (e.speed * dt);
        e.pos = bounds_.clampPoint(e.pos, e.radius);
    }
    // 적끼리 겹치지 않게 밀어낸다
    for (size_t i = 0; i < enemies_.size(); ++i) {
        for (size_t j = i + 1; j < enemies_.size(); ++j) {
            Enemy& a = enemies_[i];
            Enemy& b = enemies_[j];
            if (!a.alive || !b.alive) continue;
            Vec2 d = b.pos - a.pos;
            float len = d.length();
            float minD = a.radius + b.radius;
            if (len < minD && len > 0.01f) {
                Vec2 push = d.normalized() * ((minD - len) * 0.5f);
                if (a.kind != EnemyKind::Guardian) a.pos -= push;
                if (b.kind != EnemyKind::Guardian) b.pos += push;
            }
        }
    }
}

void World::damageEnemy(Enemy& e, float raw, bool showNumber) {
    if (!e.alive) return;
    float dmg = mitigate(raw, e.def);
    e.hp -= dmg;
    e.hitFlash = 0.15f;
    if (showNumber) addText(intText(dmg), e.pos - Vec2{0.f, e.radius + 14.f}, kColorDamageToEnemy, 16.f, 0.8f);
    if (e.hp <= 0.f) {
        e.alive = false;
        events_.push_back({WorldEventType::EnemyKilled, e.kind, e.expReward});
        addFx(FxType::Death, e.pos, e.pos, e.radius * 1.6f, 0.5f, 0xFFFFFFFF);
        addText("+" + std::to_string(e.expReward) + " EXP", e.pos - Vec2{0.f, e.radius + 34.f}, 0xFFB6E3FF, 14.f, 1.1f);
    }
}

void World::damageDragon(float raw) {
    DragonActor& d = dragon_;
    if (!d.alive) return;
    float dmg = mitigate(raw, d.stats.def);
    if (d.shieldTime > 0.f) dmg *= (1.f - d.shieldPct);
    d.hp -= dmg;
    d.hitFlash = 0.2f;
    setDragonAnim(AnimState::Hit, cfg::kAnimHitHold);
    statHitsTaken++;
    addText(intText(dmg), d.pos - Vec2{0.f, 52.f}, kColorDamageToDragon, 20.f, 0.9f);
    addFx(FxType::Hit, d.pos, d.pos, 46.f, 0.25f, kColorDamageToDragon);
    if (d.hp <= 0.f) {
        d.hp = 0.f;
        d.alive = false;
        events_.push_back({WorldEventType::DragonDied, EnemyKind::Imp, 0});
        addFx(FxType::Death, d.pos, d.pos, 90.f, 0.9f, speciesDef(d.species).color);
    }
}

void World::resolveZone(const AttackZone& z, CombatMode mode) {
    DragonActor& d = dragon_;
    if (z.shape == ZoneShape::Circle) addFx(FxType::Ring, z.a, z.a, z.radius, 0.25f, 0xFFFF6040);
    else addFx(FxType::LineBlast, z.a, z.b, z.radius, 0.3f, 0xFFFF6040);
    if (!d.alive) return;

    float reach = cfg::kDragonRadius * 0.4f;
    bool inside = z.shape == ZoneShape::Circle ? distance(d.pos, z.a) <= z.radius + reach
                                               : distancePointSegment(d.pos, z.a, z.b) <= z.radius + reach;
    if (!inside) return;

    bool dodged = d.invuln > 0.f;
    // 스킬 수동·자동 모드는 공격마다 확률로 회피한다(무적 보장 없음)
    if (!dodged && mode != CombatMode::Direct) dodged = rng_.chance(dragonDodgeChance());
    if (dodged) {
        statDodges++;
        addText("회피!", d.pos - Vec2{0.f, 56.f}, kColorDodge, 20.f, 0.8f);
        addFx(FxType::Dodge, d.pos, d.pos, 50.f, 0.35f, kColorDodge);
        return;
    }
    damageDragon(z.damage);
}

void World::updateCombat(float dt, CombatMode mode, const PlayerInput& in) {
    currentMode_ = mode;
    DragonActor& d = dragon_;
    // 전투 중에는 자동 행동(날기)을 쓰지 않는다. 적이 없어 wander가 돌면 그 안에서 다시 켜진다.
    d.flying = false;
    // 타이머
    d.basicCd = std::max(0.f, d.basicCd - dt);
    for (float& c : d.skillCd) c = std::max(0.f, c - dt);
    d.dodgeCd = std::max(0.f, d.dodgeCd - dt);
    d.invuln = std::max(0.f, d.invuln - dt);
    d.hitFlash = std::max(0.f, d.hitFlash - dt);
    if (d.shieldTime > 0.f) d.shieldTime -= dt;
    if (d.hasteTime > 0.f) d.hasteTime -= dt;

    if (d.alive) {
        if (d.stats.regen > 0.f) d.hp = std::min(d.stats.maxHp, d.hp + d.stats.regen * dt);

        if (mode == CombatMode::Direct) {
            if (in.dodge && d.dodgeCd <= 0.f && d.dashTime <= 0.f) {
                Vec2 dir{in.moveX, in.moveY};
                if (dir.length() < 0.01f) dir = d.facing;
                dir = dir.normalized();
                d.dashVel = dir * (cfg::kDodgeDistance / cfg::kDodgeDuration);
                d.dashTime = cfg::kDodgeDuration;
                d.invuln = std::max(d.invuln, cfg::kDodgeInvuln);
                d.dodgeCd = cfg::kDodgeCooldown;
                addFx(FxType::Dash, d.pos, d.pos + dir * cfg::kDodgeDistance, cfg::kDragonRadius, 0.3f, 0xFFDDF6FF);
            }
            moveDragon(dt, {in.moveX, in.moveY}, 1.f);
        } else {
            dragonAi(dt, mode == CombatMode::Auto);
        }
        if (d.dashTime > 0.f) d.dashTime -= dt;

        if (mode != CombatMode::Auto) {
            for (int i = 0; i < 3; ++i)
                if (in.skill[i]) castSkill(i, false);
        }
        basicAttack();
    }

    updateEnemies(dt);

    // 공격 예고 판정
    for (AttackZone& z : zones_) z.t += dt;
    std::vector<AttackZone> keep;
    keep.reserve(zones_.size());
    std::vector<AttackZone> fire;
    for (const AttackZone& z : zones_) {
        if (!findEnemy(z.ownerId)) continue;  // 시전자가 죽으면 취소
        if (z.t >= z.windup) fire.push_back(z);
        else keep.push_back(z);
    }
    zones_.swap(keep);
    for (const AttackZone& z : fire) resolveZone(z, mode);

    // 투사체
    for (Projectile& p : projectiles_) {
        Enemy* t = findEnemy(p.targetId);
        if (!t) {
            p.alive = false;
            continue;
        }
        Vec2 to = t->pos - p.pos;
        float step = p.speed * dt;
        if (to.length() <= step + t->radius * 0.6f) {
            damageEnemy(*t, p.damage);
            addFx(FxType::Hit, t->pos, t->pos, p.big ? 40.f : 24.f, 0.2f, speciesDef(d.species).color);
            p.alive = false;
        } else {
            p.pos += to.normalized() * step;
        }
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(), [](const Projectile& p) { return !p.alive; }),
                       projectiles_.end());

    // 지속 피해 장판
    for (DotZone& z : dots_) {
        z.remaining -= dt;
        for (Enemy& e : enemies_) {
            if (e.alive && distance(e.pos, z.pos) <= z.radius + e.radius * 0.5f) damageEnemy(e, z.dps * dt, false);
        }
    }
    dots_.erase(std::remove_if(dots_.begin(), dots_.end(), [](const DotZone& z) { return z.remaining <= 0.f; }), dots_.end());

    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(), [](const Enemy& e) { return !e.alive; }), enemies_.end());

    updateDragonAnim(dt);
}

void World::updateFx(float dt) {
    for (Fx& f : fx_) f.t += dt;
    fx_.erase(std::remove_if(fx_.begin(), fx_.end(), [](const Fx& f) { return f.t >= f.duration; }), fx_.end());
    for (FloatText& t : texts_) {
        t.t += dt;
        t.pos.y -= 32.f * dt;
    }
    texts_.erase(std::remove_if(texts_.begin(), texts_.end(), [](const FloatText& t) { return t.t >= t.duration; }), texts_.end());
}

}  // namespace dt
