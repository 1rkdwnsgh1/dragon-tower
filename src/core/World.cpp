#include "core/World.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

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
constexpr double kCurseZoneInterval = 1.0;

void applyCurse(Enemy& enemy) {
    enemy.curseStacks = std::min(enemy.curseStacks + 1, kCurseMaxStacks);
    enemy.curseRemaining = kCurseDuration;
}
}  // namespace

World::World() = default;

// 판정에 쓰는 몸 반지름. 보기 크기를 따라 커진다 — "겹쳐 보이면 맞는다"를 지키기 위해서다.
// 그리기 쪽(Renderer)은 자기 몫으로 charScale 을 따로 곱하므로 여기서 e.radius 를 바꾸지 않는다.
static inline float scaledBody(float r, float k) { return r * k; }

static bool inSkillCircle(const Enemy& enemy, Vec2 center, float radius, float scale) {
    return enemy.alive && distance(enemy.pos, center) <= radius + scaledBody(enemy.radius, scale);
}

void World::resetSouls() {
    cancelProcession();
    dragon_.souls = 0;
    pendingUltimatePayment_.reset();
}

UltimateCharge World::ultimateCharge() const {
    const SpeciesDef& species = speciesDef(dragon_.species);
    if (!dragon_.alive) return {};
    for (const SkillDef& skill : species.skills) {
        if (skill.kind == SkillKind::PassiveOnKill && skill.grantsSoul && dragon_.level >= skill.unlockLevel)
            return {true, std::clamp(dragon_.souls, 0, kUltimateSoulCost)};
    }
    return {};
}

bool World::canUseUltimate(const UltimateUseConditions& conditions) const {
    return conditions.skillUnlocked && conditions.inCombat && conditions.castAllowed &&
           !pendingUltimatePayment_ && ultimateCharge().full();
}

std::optional<uint64_t> World::beginUltimatePayment(const UltimateUseConditions& conditions) {
    if (!canUseUltimate(conditions) || nextUltimatePayment_ == 0) return std::nullopt;
    pendingUltimatePayment_ = nextUltimatePayment_++;
    return pendingUltimatePayment_;
}

bool World::commitUltimatePayment(uint64_t id) {
    if (pendingUltimatePayment_ != id) return false;
    pendingUltimatePayment_.reset();
    if (!ultimateCharge().full()) return false;
    dragon_.souls -= kUltimateSoulCost;
    return true;
}

void World::cancelUltimatePayment(uint64_t id) {
    if (pendingUltimatePayment_ == id) pendingUltimatePayment_.reset();
}

void World::setupDragon(Species s, int level, float hp, Vec2 pos) {
    dragon_ = DragonActor{};
    resetSouls();
    dragon_.species = s;
    dragon_.level = level;
    dragon_.stats = statsFor(s, level);
    dragon_.hp = (hp <= 0.f) ? dragon_.stats.maxHp : std::min(hp, dragon_.stats.maxHp);
    dragon_.pos = pos;
    dragon_.wanderTarget = pos;
    dragon_.groundY = pos.y;  // 처음 선 자리를 걷는 높이로 삼는다
    dragon_.alive = true;
    // v1.4: 재사용 대기시간은 스킬 수만큼 둔다. 종마다 개수가 다르다.
    const SpeciesDef& sp = speciesDef(s);
    dragon_.skillCd.assign(sp.skills.size(), 0.f);
    dragon_.skillMotion = -1;
    // 장착 칸은 해제된 액티브로 앞에서부터 채워 둔다. Game은 저장해 둔 칸으로 곧바로
    // 덮어쓰지만, World만 따로 쓰는 곳(테스트·밸런스 시뮬레이션)에서도 스킬이 나가야 한다.
    slots_.fill(-1);
    autoFillSlots();
}

// 빈 칸을 해제된 액티브로 채운다. 이미 들어 있는 칸은 건드리지 않는다.
void World::autoFillSlots() {
    const SpeciesDef& sp = speciesDef(dragon_.species);
    for (int idx : unlockedActiveSkills(sp, dragon_.level)) {
        if (sp.skills[static_cast<size_t>(idx)].ultimate) continue;
        bool already = false;
        for (int v : slots_) if (v == idx) { already = true; break; }
        if (already) continue;
        for (int& v : slots_) {
            if (v < 0) { v = idx; break; }
        }
    }
}

void World::setDragonAnim(AnimState s, float hold) {
    DragonActor& d = dragon_;
    // 사망 상태는 어떤 것으로도 덮이지 않는다.
    if (d.anim == AnimState::Death) return;
    if (d.anim != s) {
        d.steelAttack.reset();
        d.anim = s;
        d.animTime = 0.f;
    }
    d.animHold = std::max(d.animHold, hold);
}

void World::updateDragonAnim(float dt, bool allowForcedMotion) {
    DragonActor& d = dragon_;
    d.animTime += dt;
    d.animHold = std::max(0.f, d.animHold - dt);
    if (d.skillCast && d.skillCast->soulfireCastId)
        d.animHold = std::max(0.f, d.skillCast->lockTime - d.skillCast->elapsed);
    if (allowForcedMotion && d.animForced && d.alive && !d.held && !confining_ && !d.landing &&
        d.dashTime <= 0.f && d.animHold <= 0.f) {
        d.skillMotion = -1;
        setDragonAnim(d.forcedAnim, 0.f);
        return;
    }
    if (!d.alive) {
        if (d.anim != AnimState::Death) {
            d.anim = AnimState::Death;
            d.animTime = 0.f;
            d.animHold = cfg::kAnimDeathHold;
        }
        return;
    }
    if (d.animHold > 0.f) return;  // 1회 재생이 끝나지 않았다
    d.skillMotion = -1;            // v1.4 스킬 전용 모션은 1회 재생이 끝나면 내려놓는다
    // 날고 있으면 잠시 멈춰 있어도 공중이므로 Fly를 유지한다.
    // v1.0: 대쉬(회피)는 짧게 날아오르는 동작으로 본다. 나는 시트가 없으면 move -> idle 로 대체된다.
    AnimState want = (d.flying || d.dashTime > 0.f) ? AnimState::Fly
                     : d.moving                     ? AnimState::Move
                                                    : AnimState::Idle;
    // v0.9: 바탕화면에서 한동안 가만히 쉬면 엎드린다(쉬는 모션). 시트가 없으면 idle로 대체된다.
    // 바로 엎드리지 않고 조금 기다리는 이유는, 잠깐 멈출 때마다 누웠다 일어나면 부산해 보이기 때문이다.
    if (want == AnimState::Idle && !d.held && d.behavior == IdleBehavior::Rest &&
        d.restTime >= cfg::kRestLieDelay) {
        want = AnimState::Rest;
    }
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
    // 레벨이 올라 새로 열린 스킬을 빈 칸에 넣는다. Game은 곧바로 저장 기록으로 덮어쓴다.
    autoFillSlots();
}

bool World::skillUnlocked(int i) const {
    const SpeciesDef& sp = speciesDef(dragon_.species);
    if (!skillUnlockedAt(sp, i, dragon_.level)) return false;
    // 패시브는 "쓰는" 스킬이 아니다. 해제됐더라도 발동 대상이 아니다.
    return !skillIsPassive(sp.skills[static_cast<size_t>(i)].kind);
}

bool World::skillReady(int i) const {
    if (!skillUnlocked(i)) return false;
    if (i >= static_cast<int>(dragon_.skillCd.size())) return false;
    return dragon_.skillCd[static_cast<size_t>(i)] <= 0.f;
}

Enemy& World::spawnEnemy(EnemyKind kind, int level, Vec2 pos, bool spawnFx) {
    Enemy e;
    e.id = nextEnemyId_++;
    e.kind = kind;
    e.level = std::max(1, level);
    e.pos = pos;
    const EnemyDef& def = enemyDef(kind);
    EnemyStats st = enemyStatsFor(kind, e.level);
    e.radius = def.radius;
    e.maxHp = st.maxHp;
    e.atk = st.atk;
    e.def = st.def;
    e.speed = st.speed;
    e.expReward = st.expReward;
    e.attackCd = def.role == EnemyRole::Boss ? def.attackCooldown : rng_.uniform(0.6f, 1.6f);
    e.hp = e.maxHp;
    // 경계 밖에서 생겨났으면 들어오는 중으로 표시한다(그 전까지는 가두지 않는다)
    e.entering = !bounds_.contains(e.pos.x, e.pos.y);
    enemies_.push_back(e);
    events_.push_back({WorldEventType::EnemySpawned, kind, 0});
    if (spawnFx) addFx(FxType::Poof, pos, pos, e.radius * 1.4f, 0.4f, 0xFFB08CFF);
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
    dragon_.overheatSkill = -1;
    dragon_.steelAttack.reset();
    cancelProcession();
    if (dragon_.skillCast && (dragon_.skillCast->soulfireCastId || dragon_.skillCast->boneShotOrigin))
        advanceSkillCast(0.f, true);
    if (dragon_.breath) {
        if (dragon_.breath->inputLock > 0.f) dragon_.animHold = 0.f;
        dragon_.breath.reset();
    }
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
        if (!e.alive || e.hopping()) continue;  // 분열해 날아가는 중에는 노리지 않는다
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

FxPhase Fx::phase() const {
    if (t < windup) return FxPhase::Windup;
    if (t < windup + hold) return FxPhase::Hold;
    return FxPhase::Tail;
}

float Fx::phaseProgress() const {
    float lo = 0.f, len = windup;
    switch (phase()) {
        case FxPhase::Windup: break;
        case FxPhase::Hold:
            lo = windup;
            len = hold;
            break;
        case FxPhase::Tail:
            lo = windup + hold;
            len = tail;
            break;
    }
    if (len <= 0.f) return 1.f;
    return std::clamp((t - lo) / len, 0.f, 1.f);
}

void World::addFx(FxType type, Vec2 a, Vec2 b, float radius, float dur, uint32_t color) {
    Fx f;
    f.type = type;
    f.a = a;
    f.b = b;
    f.radius = radius;
    f.duration = dur;
    f.color = color;
    // 단계를 전체 길이의 비율로 나눈다. 전체 길이는 건드리지 않으므로 보이는 모습은 전과 같다.
    f.windup = dur * cfg::kFxShapeWindupRatio;
    f.hold = dur * cfg::kFxShapeHoldRatio;
    f.tail = dur - f.windup - f.hold;
    // 오래 켜 두면 효과가 쌓인다. 상한을 두고 오래된 것부터 버린다.
    if (fx_.size() >= static_cast<size_t>(cfg::kMaxFx)) fx_.erase(fx_.begin());
    fx_.push_back(f);
}

void World::addSkillFx(const SkillDef& s, FxType fallback, Vec2 a, Vec2 b, float radius, float shapeDur,
                       uint32_t color) {
    if (s.fx.empty()) {
        addFx(fallback, a, b, radius, shapeDur, color);  // 전과 완전히 같다
        return;
    }
    Fx f;
    f.type = fallback;  // 시트가 없으면 이 도형으로 대신 그린다(그리는 쪽에서 판단)
    f.a = a;
    f.b = b;
    f.radius = radius;
    f.color = color;
    f.key = s.fx;
    f.t = -s.fxDelay;
    f.inFront = s.fxInFront;
    f.scale = s.fxScale;
    f.offsetX = s.fxOffsetX;
    f.offsetY = s.fxOffsetY;
    f.windup = s.fxWindup > 0.f ? s.fxWindup : cfg::kFxWindup;
    f.hold = s.fxHold > 0.f ? s.fxHold : cfg::kFxHold;
    f.tail = s.fxTail > 0.f ? s.fxTail : cfg::kFxTail;
    f.duration = f.windup + f.hold + f.tail;
    if (fx_.size() >= static_cast<size_t>(cfg::kMaxFx)) fx_.erase(fx_.begin());
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
        float speed = d.stats.speed * speedMul * d.moveScale * (d.hasteTime > 0.f ? 1.f + d.hasteSpeed : 1.f);
        d.pos += dir.normalized() * (speed * dt);
        d.moving = true;
    }
    if (std::fabs(dir.x) > 0.01f) d.facing = Vec2{dir.x > 0.f ? 1.f : -1.f, 0.f};
    d.pos = bounds_.clampPoint(d.pos, cfg::kDragonRadius);
}

void World::basicAttack(float dt) {
    DragonActor& d = dragon_;
    if (d.basicCd > 0.f || (d.breath && d.breath->elapsed < d.breath->inputLock)) return;
    if (d.awakened) {
        if (d.skillCast) return;
        const auto& skills = speciesDef(d.species).skills;
        const auto shot = std::find_if(skills.begin(), skills.end(),
                                       [](const SkillDef& skill) { return skill.boneShot; });
        if (shot != skills.end()) {
            if (const Enemy* target = nearestEnemy(d.pos, std::numeric_limits<float>::infinity())) {
                beginBoneShot(static_cast<int>(shot - skills.begin()), *target, true);
                d.basicCd = cfg::kBasicAttackCooldown;
            }
            return;
        }
    }
    Enemy* t = nearestEnemy(d.pos, cfg::kBasicAttackRange);
    if (!t) return;
    if (speciesDef(d.species).key == "steel_dragon") {
        if (d.steelAttack || (d.anim == AnimState::Attack && d.animHold > 0.f) || d.skillCast || d.breath)
            return;
        setDragonAnim(AnimState::Attack, kSteelBasicDuration + dt);
        // 입력을 받은 틱의 끝에서 F01을 시작한다.
        d.animTime = -dt;
        d.steelAttack = SteelAttackCast{t->id};
        d.basicCd = cfg::kBasicAttackCooldown;
        return;
    }
    Projectile p;
    p.pos = d.pos;
    p.targetId = t->id;
    p.speed = cfg::kProjectileSpeed;
    p.damage = d.attack() * cfg::kBasicAttackPower;
    projectiles_.push_back(p);
    d.basicCd = cfg::kBasicAttackCooldown;
    if (std::fabs(t->pos.x - d.pos.x) > 4.f) d.facing = Vec2{t->pos.x > d.pos.x ? 1.f : -1.f, 0.f};
    setDragonAnim(AnimState::Attack, cfg::kAnimAttackHold);
}

void World::advanceCoreOverheat(float dt) {
    DragonActor& d = dragon_;
    if (d.overheatSkill < 0) return;
    if (!d.alive || d.held) {
        d.overheatSkill = -1;
        return;
    }
    d.overheatWindup -= dt;
    if (d.overheatWindup > 0.f) return;

    const SpeciesDef& sp = speciesDef(d.species);
    const SkillDef& skill = sp.skills[static_cast<size_t>(d.overheatSkill)];
    d.overheatSkill = -1;
    d.overheatResidual = kCoreOverheatResidual + dt;
    const Vec2 center = d.pos;
    for (Enemy& enemy : enemies_) {
        if (inSkillCircle(enemy, center, skillRadius(skill), sizeScale_))
            damageEnemy(enemy, d.attack() * skill.power);
    }
    // 뒤의 원형 파동과 앞의 불꽃은 같은 월드 좌표와 수명을 사용한다.
    const float height = 2.4f * skillRadius(skill);
    addFx(FxType::Ring, center, center, height, skill.duration, sp.color);
    fx_.back().key = skill.fx;
    fx_.back().inFront = false;
    fx_.back().t = -dt;
    addFx(FxType::Hit, center, center, height, skill.duration, sp.color);
    fx_.back().key = skill.fxImpact;
    fx_.back().t = -dt;
}

void World::advanceSteelAttack(float dt) {
    DragonActor& d = dragon_;
    if (!d.steelAttack) return;
    const SteelAttackCast cast = *d.steelAttack;
    const Enemy* target = findEnemy(cast.targetId);
    if (!d.alive || d.anim != AnimState::Attack || d.skillMotion >= 0 || d.skillCast || d.breath || !target) {
        d.steelAttack.reset();
        return;
    }
    // 이동 입력과 조준 방향은 별개다. 발사 전에는 표적 쪽을 바라본다.
    if (target->pos.x != d.pos.x)
        d.facing = {target->pos.x < d.pos.x ? -1.f : 1.f, 0.f};
    if (cast.startsThisStep) {
        d.steelAttack->startsThisStep = false;
        d.animTime = -dt;
        d.animHold = kSteelBasicDuration + dt;
        return;
    }
    const float untilLaunch = kSteelBasicLaunch - d.animTime;
    if (dt < untilLaunch) return;
    d.steelAttack.reset();

    const float height = cfg::kDragonRadius * 3.f * sizeScale_ *
        speciesDef(d.species).anims[static_cast<int>(AnimState::Attack)].renderScale;
    Projectile shot;
    shot.pos = d.pos + Vec2{d.facing.x * kSteelBasicMouthX * height,
                            cfg::kDragonRadius - kSteelBasicMouthY * height};
    shot.targetId = target->id;
    const float travelStep = std::clamp(dt - untilLaunch, 0.f, dt);
    if (cast.skill >= 0) {
        const SkillDef& skill = speciesDef(d.species).skills[static_cast<size_t>(cast.skill)];
        shot.skill = cast.skill;
        shot.speed = cfg::kProjectileSpeed * 1.1f;
        shot.damage = d.attack() * skill.power;
        shot.furnace = FurnaceFlight{travelStep, skill.range};
        projectiles_.push_back(shot);
        return;
    }
    shot.speed = cfg::kProjectileSpeed;
    shot.damage = d.stats.atk * cfg::kBasicAttackPower;
    const Vec2 to = target->pos - shot.pos;
    Vec2 aim = to.normalized();
    if (aim.length() == 0.f) aim = d.facing;
    shot.steelBasic = SteelBasicFlight{aim, sizeScale_, travelStep};
    projectiles_.push_back(shot);
}

void World::advanceSteelBasicFlight(Projectile& shot, float dt) {
    auto& flight = *shot.steelBasic;
    const float elapsed = flight.firstStep >= 0.f ? flight.firstStep : dt;
    flight.firstStep = -1.f;
    const float distanceLeft = std::max(0.f, cfg::kBasicAttackRange - shot.speed * shot.t);
    const float step = std::clamp(shot.speed * elapsed, 0.f, distanceLeft);
    Enemy* target = findEnemy(shot.targetId);
    if (target) {
        // 지정된 단일 표적의 기존 충돌 반경과 직선 구간의 첫 교점을 찾는다.
        const Vec2 to = target->pos - shot.pos;
        const float along = to.x * flight.aim.x + to.y * flight.aim.y;
        const float radius = target->radius * 0.6f;
        const float perpendicular = to.x * flight.aim.y - to.y * flight.aim.x;
        const float chordSquared = radius * radius - perpendicular * perpendicular;
        if (chordSquared >= 0.f) {
            const float chord = std::sqrt(chordSquared);
            const float contact = std::max(0.f, along - chord);
            if (along + chord >= 0.f && contact <= step) {
                const Vec2 at = shot.pos + flight.aim * contact;
                damageEnemy(*target, shot.damage);
                addFx(FxType::Hit, at, at + flight.aim, 80.f * flight.scale, kSteelBasicHitDuration,
                      speciesDef(dragon_.species).color);
                fx_.back().key = "basic_hit";
                fx_.back().t = -dt;  // 같은 틱의 updateFx 후 F01로 시작한다.
                shot.alive = false;
                return;
            }
        }
    }
    shot.pos += flight.aim * step;
    shot.t += elapsed;
    shot.alive = step < distanceLeft && bounds_.contains(shot.pos.x, shot.pos.y);
}

void World::beginBoneShot(int skillIndex, const Enemy& target, bool basic) {
    DragonActor& d = dragon_;
    const SkillDef& skill = speciesDef(d.species).skills[static_cast<size_t>(skillIndex)];
    SkillCast cast;
    cast.skill = skillIndex;
    cast.boneShotBasic = basic;
    if (basic) cast.boneShotTargetId = target.id;
    cast.facingLeft = target.pos.x == d.pos.x ? d.facing.x < 0.f : target.pos.x < d.pos.x;
    const float direction = cast.facingLeft ? -1.f : 1.f;
    const float height = cfg::kDragonRadius * 3.f * sizeScale_ * skill.motionAnim.renderScale * kAwakenedBodyScale;
    const Vec2 origin = d.pos + Vec2{direction * kBoneShotLaunchX * height,
        cfg::kDragonRadius - kBoneShotLaunchY * height};
    cast.boneShotOrigin = origin;
    cast.aim = (target.pos - origin).normalized();
    if (cast.aim.length() == 0.f) cast.aim = {direction, 0.f};
    cast.hitTime = cast.fxTime = 7.f / 16.f;
    cast.lockTime = basic ? cast.hitTime : 1.f;
    d.facing = {direction, 0.f};
    d.dashTime = 0.f;
    d.moving = false;
    d.skillCast = cast;
    if (basic) {
        d.skillMotion = skillIndex;
        d.animTime = 0.f;
        setDragonAnim(AnimState::Attack, 1.f);
    }
}

void World::setSlots(const std::array<int, kSkillSlots>& s) {
    slots_ = s;
    const SpeciesDef& sp = speciesDef(dragon_.species);
    for (int& v : slots_)
        if (v >= 0 && v < static_cast<int>(sp.skills.size()) && sp.skills[static_cast<size_t>(v)].ultimate) v = -1;
}

int World::ultimateSkill() const {
    const SpeciesDef& sp = speciesDef(dragon_.species);
    for (size_t i = 0; i < sp.skills.size(); ++i)
        if (sp.skills[i].ultimate) return static_cast<int>(i);
    return -1;
}

// 2026-11-20: 궁극기는 장착 칸 밖의 전용 칸이고 자동 모드에서도 쓴다(사용자 결정). 그래서 예전의
// "장착 필요"·"수동 모드 전용" 조건이 없다. 영혼을 쓰는 궁극기만 영혼 10개를 요구한다.
const char* World::ultimateBlockedReason(bool inCombat) const {
    const DragonActor& d = dragon_;
    const int i = ultimateSkill();
    if (i < 0 || !skillUnlocked(i) || i >= static_cast<int>(d.skillCd.size())) return "궁극기 미해제";
    const SkillDef& s = speciesDef(d.species).skills[static_cast<size_t>(i)];
    if (d.procession || d.skillCast || (d.breath && d.breath->elapsed < d.breath->inputLock) || d.held)
        return "시전 중 잠금";
    if (d.skillCd[static_cast<size_t>(i)] > 0.f) return "궁극기 재사용 대기";
    if (s.ultimateUsesSouls && !ultimateCharge().full()) return "영혼 10개 필요";
    if (!inCombat) return "전투 중에 사용 가능";
    if (aliveEnemyCount() == 0) return "대상 없음";
    return nullptr;
}

bool World::castUltimate(bool aiChoice) {
    const int i = ultimateSkill();
    if (i < 0) return false;
    if (const char* blocked = ultimateBlockedReason(true)) {
        if (!aiChoice) addText(blocked, dragon_.pos - Vec2{0.f, 60.f}, 0xFFCCCCCC, 14.f, 0.8f);
        return false;
    }
    const SkillDef& s = speciesDef(dragon_.species).skills[static_cast<size_t>(i)];
    // 죽음의 행렬은 첫 충돌에서 영혼을 확정한다(시전 안에서 처리). 다른 궁극기는 시전 성공 때 바로 쓴다.
    if (s.kind == SkillKind::Procession) return castSkill(i, aiChoice);
    const bool cast = castSkill(i, aiChoice);
    if (cast && s.ultimateUsesSouls) dragon_.souls = std::max(0, dragon_.souls - kUltimateSoulCost);
    return cast;
}

bool World::castSkill(int i, bool aiChoice) {
    DragonActor& d = dragon_;
    if (d.breath && d.breath->elapsed < d.breath->inputLock) return false;
    if (d.steelAttack && d.steelAttack->skill >= 0) return false;
    if (d.skillCast || d.procession) return false;  // 2026-11-18: 타격 프레임 스킬이 끝날 때까지 다른 스킬을 막는다
    const SpeciesDef& sp = speciesDef(d.species);
    if (i < 0 || i >= static_cast<int>(sp.skills.size())) return false;
    const SkillDef& s = sp.skills[static_cast<size_t>(i)];
    if (s.kind == SkillKind::Procession) {
        const char* blocked = i == ultimateSkill() ? ultimateBlockedReason(true) : "궁극기 미해제";
        const Enemy* target = blocked ? nullptr : nearestEnemy(d.pos, std::numeric_limits<float>::max());
        if (!blocked && !target) blocked = "대상 없음";
        if (blocked) {
            if (!aiChoice) addText(blocked, d.pos - Vec2{0.f, 60.f}, 0xFFCCCCCC, 14.f, 0.8f);
            return false;
        }
        const auto payment = beginUltimatePayment({true, true, true});
        if (!payment) return false;
        const float dx = target->pos.x - d.pos.x;
        const bool left = std::fabs(dx) > 0.01f ? dx < 0.f : d.facing.x < 0.f;
        Vec2 aim = (target->pos - d.pos).normalized();
        if (aim.length() == 0.f) aim = {left ? -1.f : 1.f, 0.f};
        const float rearOffset = 55.f * std::clamp(sizeScale_, 0.5f, 3.f);
        const Vec2 rear = d.pos - aim * rearOffset;
        const float crossingDistance = std::hypot(bounds_.w, bounds_.h) + rearOffset;
        const float flightLimit = crossingDistance / s.processionSpeed + s.processionLifetime;
        d.procession = ProcessionCast{i, *payment, d.pos, rear, left, flightLimit, 0.f, 0, false, {}, {}};
        d.procession->waves.reserve(static_cast<size_t>(s.processionShots));
        d.procession->claimedTargets.reserve(static_cast<size_t>(s.processionShots));
        d.facing = {left ? -1.f : 1.f, 0.f};
        d.dashTime = 0.f;
        d.moving = false;
        d.skillMotion = i;
        d.animTime = 0.f;
        setDragonAnim(AnimState::Attack, ProcessionCast::firstLaunch);
        return true;
    }
    if (!skillUnlocked(i)) {
        if (!aiChoice) addText("Lv." + std::to_string(s.unlockLevel) + " 필요", d.pos - Vec2{0.f, 60.f}, 0xFFCCCCCC, 14.f, 0.8f);
        return false;
    }
    if (i >= static_cast<int>(d.skillCd.size())) return false;
    if (s.boneShot && d.awakened) {
        if (aiChoice) return false;  // 자동 선택은 변신을 해제하지 않는다.
        d.awakened = false;
        addText("각성 해제", d.pos - Vec2{0.f, 78.f}, sp.color, 15.f, 0.9f);
        return true;
    }
    if (d.skillCd[static_cast<size_t>(i)] > 0.f) return false;

    Vec2 facingDir = d.facing.length() > 0.01f ? d.facing : Vec2{1.f, 0.f};
    switch (s.kind) {
        case SkillKind::Projectile: {
            Enemy* t = nearestEnemy(d.pos, s.range);
            if (!t) {
                if (s.boneShot && !aiChoice) {
                    d.awakened = true;
                    d.skillCd[static_cast<size_t>(i)] = s.cooldown;
                    addText("각성", d.pos - Vec2{0.f, 78.f}, sp.color, 15.f, 0.9f);
                    return true;
                }
                if (!aiChoice) addText("대상 없음", d.pos - Vec2{0.f, 60.f}, 0xFFCCCCCC, 14.f, 0.6f);
                return false;
            }
            if (s.boneShot) {
                beginBoneShot(i, *t, false);
                break;
            }
            if (s.furnaceBurst) {
                d.skillMotion = -1;
                setDragonAnim(AnimState::Attack, kSteelBasicDuration);
                d.steelAttack = SteelAttackCast{t->id, i, true};
                d.skillCd[static_cast<size_t>(i)] = s.cooldown;
                addText(s.name, d.pos - Vec2{0.f, 78.f}, sp.color | 0xFF000000, 15.f, 0.9f);
                return true;
            }
            Projectile p;
            p.pos = d.pos;
            p.targetId = t->id;
            p.speed = cfg::kProjectileSpeed * 1.1f;
            p.damage = d.attack() * s.power;
            p.big = true;
            p.appliesCurse = sp.key == "revenant" && s.appliesCurse;
            p.skill = i;  // 날아가는 동안 쓸 효과 그림을 그리는 쪽에서 찾는다(2026-11-17)
            projectiles_.push_back(p);
            break;
        }
        case SkillKind::Burst: {
            bool any = false;
            for (Enemy& e : enemies_)
                if (inSkillCircle(e, d.pos, skillRadius(s), sizeScale_)) any = true;
            if (aiChoice && !any) return false;
            if (s.coreOverheat) {
                if (d.overheatSkill >= 0) return false;
                d.overheatSkill = i;
                d.overheatWindup = kCoreOverheatWindup;
                d.skillCd[static_cast<size_t>(i)] = s.cooldown;
                return true;
            }
            for (Enemy& e : enemies_) {
                if (!inSkillCircle(e, d.pos, skillRadius(s), sizeScale_)) continue;
                damageEnemy(e, d.attack() * s.power, true, s.detonatesCurse);
                if (s.extra > 0.f) e.stun = std::max(e.stun, s.extra);
            }
            addSkillFx(s, FxType::Ring, d.pos, d.pos, skillRadius(s), 0.45f, sp.color);
            break;
        }
        case SkillKind::Line: {
            Enemy* t = nearestEnemy(d.pos, s.spectralCleave ? skillRange(s) : s.range + 80.f);
            if (aiChoice && !t) return false;
            Vec2 dir = t ? (t->pos - d.pos).normalized() : facingDir;
            if (s.usesTimedHit()) {
                // 2026-11-18: 피해는 hit_frame 에, 효과는 fx_frame 에 낸다(advanceSkillCast).
                if (dir.length() < 0.01f) dir = facingDir;
                dir = dir.normalized();
                const float fps = std::max(1.f, s.motionAnim.fps);
                SkillCast c;
                c.skill = i;
                c.aim = dir;
                c.hitTime = static_cast<float>(s.hitFrame - 1) / fps;
                c.fxTime = static_cast<float>((s.fxFrame > 0 ? s.fxFrame : s.hitFrame) - 1) / fps;
                c.lockTime = std::max(c.hitTime, c.fxTime) + cfg::kSkillCastRecover;
                c.facingLeft = std::fabs(dir.x) > 0.01f ? dir.x < 0.f : d.facing.x < 0.f;
                if (s.spectralCleave)
                    c.lockTime = static_cast<float>(s.motionFrames) / fps;
                d.facing = {c.facingLeft ? -1.f : 1.f, 0.f};
                d.dashTime = 0.f;
                d.moving = false;
                d.skillCast = c;
                break;
            }
            if (!s.layeredBreath.empty()) {
                if (d.breath) return false;
                if (dir.length() < 0.01f) dir = facingDir;
                dir = dir.normalized();
                d.breath = BreathCast{i, d.pos, dir, 0.f, s.range, skillRadius(s),
                                      d.attack() * s.power, s.breathInputLock, false, false,
                                      s.sustainedBreath, 0, {}};
                if (s.sustainedBreath) d.breath->inputLock = std::max(d.breath->inputLock, d.breath->length());
                d.breath->facingLeft = std::fabs(dir.x) > 0.01f ? dir.x < 0.f : d.facing.x < 0.f;
                if (!s.breathDeferHit && !s.sustainedBreath) {
                    d.breath->hit = true;
                    hitLine(d.pos, d.pos + dir * s.range, d.breath->radius, d.breath->damage, s.detonatesCurse);
                }
                if (!s.usesUmbralBreath()) {
                    if (std::fabs(dir.x) > 0.01f) d.facing = {dir.x < 0.f ? -1.f : 1.f, 0.f};
                    d.dashTime = 0.f;
                    d.moving = false;
                }
                break;
            }
            Vec2 end = d.pos + dir * s.range;
            hitLine(d.pos, end, skillRadius(s), d.attack() * s.power, s.detonatesCurse);
            addSkillFx(s, FxType::LineBlast, d.pos, end, skillRadius(s), 0.45f, sp.color);
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
                if (distancePointSegment(e.pos, d.pos, end) <= skillRadius(s) + scaledBody(e.radius, sizeScale_)) damageEnemy(e, d.attack() * s.power);
            }
            addSkillFx(s, FxType::Dash, d.pos, end, cfg::kDragonRadius, 0.35f, sp.color);
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
            addSkillFx(s, FxType::Heal, d.pos, d.pos, 60.f, 0.7f, kColorHeal);
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
            addSkillFx(s, FxType::Shield, d.pos, d.pos, 56.f, 0.5f, sp.color);
            break;
        }
        case SkillKind::Haste: {
            if (aiChoice && !nearestEnemy(d.pos, 350.f)) return false;
            d.hasteTime = s.duration;
            d.hasteSpeed = s.power;
            d.hasteDodge = s.extra;
            addSkillFx(s, FxType::Dash, d.pos, d.pos, 50.f, 0.4f, sp.color);
            break;
        }
        case SkillKind::Zone: {
            Enemy* t = nearestEnemy(d.pos, skillRange(s));
            if (s.soulfire) {
                if ((aiChoice && !t) || d.dashTime > 0.f) return false;
                SkillCast cast;
                cast.skill = i;
                cast.soulfireCastId = nextSoulfireCastId_++;
                cast.facingLeft = d.facing.x < 0.f;
                cast.fxTime = s.soulfireTakeoff;
                cast.lockTime = cast.fxTime + kSoulfireBreathDuration + s.soulfireLanding;
                d.skillCast = cast;
                d.facing = {cast.facingLeft ? -1.f : 1.f, 0.f};
                d.moving = false;
                d.animTime = 0.f;
                break;
            }
            if (!t) {
                if (!aiChoice) addText("대상 없음", d.pos - Vec2{0.f, 60.f}, 0xFFCCCCCC, 14.f, 0.6f);
                return false;
            }
            DotZone z;
            z.pos = t->pos;
            z.radius = skillRadius(s);
            z.dps = d.attack() * s.power;
            z.remaining = s.duration;
            z.duration = s.duration;
            z.appliesCurse = sp.key == "revenant" && s.appliesCurse;
            if (s.fxZoneSpriteOnly && !s.fx.empty()) z.spriteSkill = i;
            dots_.push_back(z);
            // 2026-11-17: 장판에도 효과 그림을 쓸 수 있게 했다(implementation-notes 의
            // "필요하면 장판 수명 전체를 덮는 Fx 를 하나 띄운다" 안 그대로).
            // **fx= 를 적은 스킬만** Fx 를 띄운다 — 안 적은 장판은 전과 똑같이 dots_ 로만 그려진다.
            // 길이는 fx_windup+fx_hold+fx_tail 로 정해지므로 dragon.txt 에서 duration 과 맞춘다.
            // 안 맞추면 잔류가 장판이 사라진 뒤에 남는다(그게 예전에 미뤘던 이유다).
            if (!s.fx.empty() && z.spriteSkill < 0)
                addSkillFx(s, FxType::Ring, z.pos, z.pos, z.radius, s.duration, sp.color);
            break;
        }
        case SkillKind::Procession:  // 위의 Procession 분기에서 처리한다.
        case SkillKind::PassiveStat:
        case SkillKind::PassiveOnKill:
            // 패시브는 발동하는 스킬이 아니다. skillUnlocked에서 이미 걸러지지만,
            // 새 패시브 종류를 더했을 때 조용히 액티브처럼 동작하지 않도록 여기서도 막는다.
            return false;
    }
    d.skillCd[static_cast<size_t>(i)] = s.cooldown;
    // v1.4: 스킬 전용 본체 모션이 있으면 그것으로 재생한다. 없으면 예전처럼 공용 attack.
    float hold = cfg::kAnimAttackHold;
    if (!s.motion.empty()) {
        d.skillMotion = i;
        // 1회 재생이 끝날 만큼 유지한다(프레임 수는 그리는 쪽이 알고 있으므로 넉넉히 잡는다).
        hold = std::max(hold, cfg::kSkillMotionHold);
        // 2026-11-18: 프레임 수를 적었으면 모션이 끝날 때까지 자세를 유지한다.
        if (s.motionFrames > 0) hold = std::max(hold, static_cast<float>(s.motionFrames) / std::max(1.f, s.motionAnim.fps));
    } else {
        d.skillMotion = -1;
    }
    if (d.breath && d.breath->skill == i && !s.usesUmbralBreath()) hold = std::max(hold, d.breath->length());
    if ((s.soulfire || s.spectralCleave || s.boneShot) && d.skillCast) hold = d.skillCast->lockTime;
    setDragonAnim(AnimState::Attack, hold);
    addText(s.name, d.pos - Vec2{0.f, 78.f}, sp.color | 0xFF000000, 15.f, 0.9f);
    return true;
}

void World::cancelProcession() {
    DragonActor& d = dragon_;
    if (!d.procession) return;
    cancelUltimatePayment(d.procession->payment);
    d.procession.reset();
    d.animHold = 0.f;
    d.skillMotion = -1;
    if (d.alive && d.anim == AnimState::Attack) setDragonAnim(AnimState::Idle, 0.f);
}

const Enemy* World::chooseProcessionTarget(ProcessionCast& cast, Vec2 from) const {
    const Enemy* nearest[2]{nullptr, nullptr};
    float best[2]{0.f, 0.f};
    for (const Enemy& enemy : enemies_) {
        if (!enemy.alive) continue;
        const bool used = std::find(cast.claimedTargets.begin(), cast.claimedTargets.end(), enemy.id) !=
                          cast.claimedTargets.end();
        const float gap = distance(enemy.pos, from) - enemy.radius;
        if (!nearest[used] || gap < best[used]) {
            best[used] = gap;
            nearest[used] = &enemy;
        }
    }
    if (!nearest[0]) return nearest[1];
    cast.claimedTargets.push_back(nearest[0]->id);
    return nearest[0];
}

bool World::advanceProcessionWaves(ProcessionCast& cast, const SkillDef& skill, float dt) {
    for (ProcessionWave& wave : cast.waves) {
        if (wave.impacted || wave.age >= cast.flightLimit) {
            wave.age += dt;
            continue;
        }
        const float flight = std::min(dt, cast.flightLimit - wave.age);
        wave.age += dt;
        const Enemy* target = wave.targetId != 0 ? findEnemy(wave.targetId) : nullptr;
        if (!target) wave.targetId = 0;
        if (!target && wave.age >= wave.nextSearchAt) {
            target = chooseProcessionTarget(cast, wave.pos);
            wave.targetId = target ? target->id : 0;
            wave.nextSearchAt = target ? 0.f : wave.age + ProcessionCast::targetRetry;
        }
        if (!target) continue;

        const Vec2 toward = target->pos - wave.pos;
        const float distanceToTarget = toward.length();
        const float step = skill.processionSpeed * flight;
        if (std::fabs(toward.x) > 0.01f) wave.facingLeft = toward.x < 0.f;
        const float contact = target->radius * 0.6f;
        if (distanceToTarget > step + contact) {
            wave.pos += toward * (step / distanceToTarget);
            continue;
        }

        if (!cast.paid) {
            if (!commitUltimatePayment(cast.payment)) return false;
            cast.paid = true;
            dragon_.skillCd[static_cast<size_t>(cast.skill)] = skill.cooldown;
        }
        wave.pos = target->pos;
        wave.impacted = true;
        wave.age = std::max(0.f, dt - std::max(0.f, distanceToTarget - contact) / skill.processionSpeed);
        const float radius = skillRadius(skill);
        for (Enemy& enemy : enemies_)
            if (inSkillCircle(enemy, wave.pos, radius, sizeScale_))
                damageEnemy(enemy, dragon_.attack() * skill.power);
    }
    cast.waves.erase(std::remove_if(cast.waves.begin(), cast.waves.end(), [&](const ProcessionWave& wave) {
        if (wave.impacted) return wave.age >= ProcessionCast::aftermath;
        const float limit = wave.targetId == 0 ? skill.processionLifetime : cast.flightLimit;
        return wave.age >= limit;
    }), cast.waves.end());
    return true;
}

void World::advanceProcession(float dt) {
    DragonActor& d = dragon_;
    if (!d.procession) return;
    if (!d.alive || d.held) {
        cancelProcession();
        return;
    }
    ProcessionCast& cast = *d.procession;
    const SkillDef& skill = speciesDef(d.species).skills[static_cast<size_t>(cast.skill)];
    const float end = cast.elapsed + dt;
    float cursor = cast.elapsed;
    while (cast.launched < skill.processionShots) {
        const float launch = ProcessionCast::firstLaunch + cast.launched * skill.processionInterval;
        if (launch > end) break;
        if (!advanceProcessionWaves(cast, skill, std::max(0.f, launch - cursor))) {
            cancelProcession();
            return;
        }
        const Enemy* target = chooseProcessionTarget(cast, cast.launchOrigin);
        const bool left = target ? target->pos.x < cast.launchOrigin.x : cast.facingLeft;
        cast.waves.push_back({cast.launchOrigin, target ? target->id : 0, 0.f, left, false,
                              target ? 0.f : ProcessionCast::targetRetry});
        ++cast.launched;
        cursor = launch;
    }
    cast.elapsed = end;
    if (!advanceProcessionWaves(cast, skill, std::max(0.f, end - cursor)) ||
        (cast.launched == skill.processionShots && cast.waves.empty())) cancelProcession();
}

void World::hitLine(Vec2 start, Vec2 end, float radius, float damage, bool detonatesCurse) {
    for (Enemy& e : enemies_) {
        if (e.alive && distancePointSegment(e.pos, start, end) <= radius + scaledBody(e.radius, sizeScale_))
            damageEnemy(e, damage, true, detonatesCurse);
    }
}

void World::hitBreathTick(BreathCast& cast, const SkillDef& skill) {
    const Vec2 end = cast.origin + cast.aim * cast.range;
    for (Enemy& enemy : enemies_) {
        if (!enemy.alive) continue;
        const Vec2 offset = enemy.pos - cast.origin;
        if (offset.x * cast.aim.x + offset.y * cast.aim.y < 0.f ||
            distancePointSegment(enemy.pos, cast.origin, end) > cast.radius + scaledBody(enemy.radius, sizeScale_))
            continue;

        const bool firstHit = skill.detonatesCurse &&
            std::find(cast.curseChecked.begin(), cast.curseChecked.end(), enemy.id) == cast.curseChecked.end();
        if (firstHit) cast.curseChecked.push_back(enemy.id);
        damageEnemy(enemy, cast.damage / BreathCast::sustainedTicks, true, firstHit);
        if (!enemy.alive || enemyDef(enemy.kind).role == EnemyRole::Boss) continue;
        const int gap = enemy.level - dragon_.level;
        enemy.breathSlowLeft = BreathCast::tickInterval;
        enemy.breathMoveFactor = gap <= 0 ? 0.f : std::min(0.75f, 0.30f + 0.05f * gap);
        if (gap < 0 && !enemy.entering) {
            const float push = std::min(30.f, 12.f + 3.f * (-gap - 1));
            enemy.pos = bounds_.clampPoint(enemy.pos + cast.aim * push, enemy.radius);
        }
    }
}

void World::advanceBreath(float dt, bool combatEnded) {
    DragonActor& d = dragon_;
    if (!d.breath) return;
    if (!d.alive || d.held || (combatEnded && (!d.breath->hit || d.breath->sustained))) {
        if (d.breath->inputLock > 0.f) d.animHold = 0.f;
        d.breath.reset();
        return;
    }
    BreathCast& cast = *d.breath;
    const SkillDef& skill = speciesDef(d.species).skills[static_cast<size_t>(cast.skill)];
    cast.elapsed += dt;
    constexpr float epsilon = 1e-6f;
    if (cast.sustained) {
        while (cast.ticksApplied < BreathCast::sustainedTicks &&
               cast.elapsed + epsilon >= BreathCast::impactTime + cast.ticksApplied * BreathCast::tickInterval) {
            ++cast.ticksApplied;
            hitBreathTick(cast, skill);
        }
    } else if (!cast.hit && cast.elapsed + epsilon >= BreathCast::impactTime) {
        cast.hit = true;
        hitLine(cast.origin, cast.origin + cast.aim * cast.range, cast.radius, cast.damage, skill.detonatesCurse);
    }
    if (cast.elapsed + epsilon >= cast.length()) {
        const bool umbral = skill.usesUmbralBreath();
        d.breath.reset();
        if (!umbral && d.anim == AnimState::Attack) {
            const float fps = speciesDef(d.species).anims[static_cast<int>(AnimState::Attack)].fps;
            d.animTime = (BreathCast::openMouthFrame + 1) / std::max(1.f, fps);
            d.animHold = std::max(d.animHold, 4.f / std::max(1.f, fps));
        }
    }
}

// 2026-11-18: 타격 프레임 스킬 진행. 숨결(advanceBreath)과 같은 규칙으로 끊는다.
void World::advanceSkillCast(float dt, bool combatEnded) {
    DragonActor& d = dragon_;
    if (!d.skillCast) return;
    SkillCast& c = *d.skillCast;
    if (c.soulfireCastId && (!d.alive || d.held || combatEnded)) {
        cancelSkillCast(0.f);
        d.moving = false;
        d.animHold = 0.f;
        d.skillMotion = -1;
        if (d.alive && d.anim == AnimState::Attack) setDragonAnim(AnimState::Idle, 0.f);
        return;
    }
    if (!d.alive || d.held) {
        d.skillCast.reset();  // 죽거나 붙잡히면 효과도 피해도 없이 끝난다
        return;
    }
    if (combatEnded && c.boneShotOrigin && c.hitDone) {
        d.skillCast.reset();
        d.animHold = 0.f;
        d.skillMotion = -1;
        return;
    }
    if (combatEnded && !c.hitDone) {
        cancelSkillCast(0.f);  // 전투가 끝나 끊긴 것은 쿨타임을 모두 돌려준다
        return;
    }
    const SpeciesDef& sp = speciesDef(d.species);
    if (c.skill < 0 || c.skill >= static_cast<int>(sp.skills.size())) {
        d.skillCast.reset();
        return;
    }
    const SkillDef& s = sp.skills[static_cast<size_t>(c.skill)];
    const float previous = c.elapsed;
    c.elapsed += dt;
    constexpr float epsilon = 1e-6f;
    if (c.soulfireCastId) {
        advanceSoulfire(c, s, previous);
        if (c.elapsed + epsilon >= c.lockTime) {
            d.animHold = 0.f;
            d.skillCast.reset();
        }
        return;
    }
    if (!c.fxDone && c.elapsed + epsilon >= c.fxTime) {
        c.fxDone = true;
        if (!s.spectralCleave && !s.boneShot) spawnAnchoredFx(s, c.facingLeft);
    }
    if (!c.hitDone && c.elapsed + epsilon >= c.hitTime) {
        c.hitDone = true;
        if (s.spectralCleave) {
            const float direction = c.facingLeft ? -1.f : 1.f;
            const float bodyHeight = cfg::kDragonRadius * 3.f * sizeScale_;
            Projectile blade;
            blade.pos = d.pos + Vec2{direction * kCleaveLaunchX * bodyHeight,
                                     cfg::kDragonRadius - kCleaveCenterY * bodyHeight};
            blade.speed = skillRange(s) / kCleaveFlight;
            blade.damage = d.attack() * s.power;
            blade.skill = c.skill;
            blade.cleave = CleaveFlight{direction, skillRadius(s), std::max(0.f, c.elapsed - c.hitTime), sizeScale_, {}};
            projectiles_.push_back(std::move(blade));
        } else if (c.boneShotOrigin) {
            d.awakened = true;
            Projectile shot;
            shot.pos = *c.boneShotOrigin;
            shot.targetId = c.boneShotTargetId;
            if (const Enemy* target = findEnemy(shot.targetId)) {
                const float facing = c.facingLeft ? -1.f : 1.f;
                // 가까운 적의 중심을 이마가 지나친 경우 몸 중심에서 전방으로 보낸다.
                if ((target->pos.x - shot.pos.x) * facing <= 0.f) shot.pos = d.pos;
            }
            shot.speed = c.boneShotBasic ? kAwakenedBasicBoneShotSpeed : kBoneShotSpeed;
            shot.damage = d.attack() * (c.boneShotBasic ? cfg::kBasicAttackPower : s.power);
            shot.big = true;
            shot.skill = c.skill;
            const float range = c.boneShotBasic ? std::numeric_limits<float>::infinity() : s.range;
            shot.boneShot = BoneShotFlight{c.aim, range, std::max(0.f, c.elapsed - c.hitTime), sizeScale_, {}};
            projectiles_.push_back(std::move(shot));
        } else {
            hitLine(d.pos, d.pos + c.aim * s.range, skillRadius(s), d.attack() * s.power, s.detonatesCurse);
        }
    }
    if (c.fxDone && c.hitDone && c.elapsed + epsilon >= c.lockTime) d.skillCast.reset();
}

float soulfireLift(float elapsed, float takeoff, float landing, float bodyHeight) {
    const float rise = std::clamp((elapsed - takeoff * 0.4f) / (takeoff * 0.6f), 0.f, 1.f);
    const float land = 1.f - std::clamp((elapsed - takeoff - kSoulfireBreathDuration) / landing, 0.f, 1.f);
    // Accelerate out of the crouch, then ease into the airborne pose.
    const float launch = rise * rise * (3.f - 2.f * rise);
    // Contact precedes the end of the landing recovery.
    const float descent = std::clamp((1.f - land) / 0.72f, 0.f, 1.f);
    return bodyHeight * 0.18f * launch * (1.f - descent * descent);
}

std::optional<SoulfireBeam> World::soulfireBeam(bool includeLanding) const {
    const DragonActor& d = dragon_;
    if (!d.skillCast || !d.skillCast->soulfireCastId || !d.alive) return std::nullopt;
    const SkillCast& cast = *d.skillCast;
    const float flight = cast.elapsed - cast.fxTime;
    if (flight < 0.f || (!includeLanding && flight >= kSoulfireBreathDuration)) return std::nullopt;
    return soulfireBeamAt(d.pos, cast, speciesDef(d.species).skills[cast.skill]);
}

std::optional<SoulfireBeam> World::soulfireBeamAt(Vec2 position, const SkillCast& cast, const SkillDef& s) const {
    const float baseBody = cfg::kDragonRadius * 3.f * sizeScale_;
    const float body = baseBody * s.motionAnim.renderScale;
    const float sign = cast.facingLeft ? -1.f : 1.f;
    const float groundY = std::min(position.y + cfg::kDragonRadius, bounds_.bottom());
    const Vec2 mouth{position.x + sign * (kSoulfireBodyOffsetX * baseBody +
                                         (s.soulfireMouthX - 0.5f) * body),
                     position.y + cfg::kDragonRadius + (s.soulfireMouthY - kSoulfireFoot) * body -
                     soulfireLift(cast.fxTime, cast.fxTime, s.soulfireLanding, baseBody)};
    const float reach = (groundY - mouth.y) / std::tan(s.soulfireAngle * kPi / 180.f);
    const float groundX = mouth.x + sign * reach;
    if (groundX < bounds_.x || groundX > bounds_.right()) return std::nullopt;
    return SoulfireBeam{mouth, {groundX, groundY}};
}

void World::advanceSoulfire(SkillCast& cast, const SkillDef& s, float previous) {
    const float flightEnd = cast.fxTime + kSoulfireBreathDuration;
    const float start = std::max(previous, cast.fxTime);
    const float end = std::min(cast.elapsed, flightEnd);
    if (end <= start) return;

    const Vec2 origin = dragon_.pos;
    const float sign = cast.facingLeft ? -1.f : 1.f;
    const float flightMultiplier = cfg::kIdleFlySpeed * sizeScale_;
    const float speed = dragon_.stats.speed * flightMultiplier *
                        (dragon_.hasteTime > 0.f ? 1.f + dragon_.hasteSpeed : 1.f);
    moveDragon(end - start, {sign, 0.f}, flightMultiplier);
    const float traveled = std::abs(dragon_.pos.x - origin.x) / sizeScale_;
    // Sample the swept path, so frame rate and standing against a boundary do not change spacing.
    for (; cast.soulfireTrailNext <= traveled; cast.soulfireTrailNext += kSoulfireTrailSpacing) {
        const float along = cast.soulfireTrailNext * sizeScale_;
        const auto beam = soulfireBeamAt({origin.x + sign * along, origin.y}, cast, s);
        if (!beam) continue;
        cast.hitDone = true;
        DotZone z;
        z.pos = beam->ground;
        z.radius = skillRadius(s);
        z.dps = dragon_.attack() * s.power;
        z.remaining = z.duration = s.duration;
        z.startDelay = start - previous + (speed > 0.f ? along / speed : 0.f);
        z.soulfireCastId = cast.soulfireCastId;
        const float baseBody = cfg::kDragonRadius * 3.f * sizeScale_;
        z.flameHeight = baseBody * s.soulfireFlameScale;
        z.flames.reserve(kSoulfireFlameCount);
        for (int n = 0; n < kSoulfireFlameCount; ++n) {
            const float across = static_cast<float>(n) / (kSoulfireFlameCount - 1);
            const float x = sign * ((across - 0.5f) * 0.63f
                                    + soulfireVisualRng_.uniform(-0.052f, 0.052f));
            const int row = n % 3;
            const float depth = (row - 1) * 0.065f + soulfireVisualRng_.uniform(-0.027f, 0.027f);
            const float delay = across * 0.12f + soulfireVisualRng_.uniform(0.f, 0.28f);
            // Alternate tall silhouettes, middle tongues, and low foreground flames.
            constexpr int variants[][4] = {{0, 1, 2, 4}, {5, 2, 1, 0}, {3, 5, 3, 3}};
            const int variant = variants[row][(n / 3) % 4];
            const float size = row == 0 ? soulfireVisualRng_.uniform(1.45f, 1.9f)
                             : row == 1 ? soulfireVisualRng_.uniform(0.98f, 1.35f)
                                        : soulfireVisualRng_.uniform(0.72f, 1.02f);
            z.flames.push_back({{x, depth}, size, delay,
                                soulfireVisualRng_.uniform(0.90f, 2.35f), variant,
                                soulfireVisualRng_.uniform(0.f, 1.f) < 0.5f});
        }
        std::sort(z.flames.begin(), z.flames.end(), [](const SoulfireFlame& a, const SoulfireFlame& b) {
            return a.offset.y < b.offset.y;
        });
        dots_.push_back(std::move(z));
    }
    cast.soulfireTrailNext -= traveled;
    dragon_.moving = traveled > 0.f && cast.elapsed < flightEnd;
}

// 타격 전에 끊는다. 남는 쿨타임 = 원래 쿨타임 × cooldownRatio (0이면 바로 다시 쓸 수 있다).
void World::cancelSkillCast(float cooldownRatio) {
    DragonActor& d = dragon_;
    if (!d.skillCast) return;
    const int i = d.skillCast->skill;
    const bool hit = d.skillCast->hitDone;
    const bool basic = d.skillCast->boneShotBasic;
    d.skillCast.reset();
    if (hit) return;  // 타격 뒤에는 쿨타임을 돌려주지 않는다
    const SpeciesDef& sp = speciesDef(d.species);
    if (!basic && i >= 0 && i < static_cast<int>(sp.skills.size()) && i < static_cast<int>(d.skillCd.size()))
        d.skillCd[static_cast<size_t>(i)] = sp.skills[static_cast<size_t>(i)].cooldown * cooldownRatio;
    d.animHold = 0.f;
    d.skillMotion = -1;
}

// fx_anchor(본체 칸 안 좌표)를 화면 좌표로 바꿔, 효과 칸의 fx_origin 이 거기에 오도록 띄운다.
// 그리는 쪽(Renderer::drawDragon)과 같은 규칙을 쓴다: 본체 칸 높이 = 반지름×3×보기 크기×render_scale,
// 칸의 바닥 기준선(240/256)이 판정 원의 아래 끝(pos.y + 반지름)에 온다.
void World::spawnAnchoredFx(const SkillDef& s, bool facingLeft) {
    DragonActor& d = dragon_;
    const SpeciesDef& sp = speciesDef(d.species);
    const float sign = (sp.flipAllowed && facingLeft) ? -1.f : 1.f;
    const float aimSign = facingLeft ? -1.f : 1.f;
    if (s.fx.empty() || !s.hasFxAnchor) {
        // 시트나 기준점이 없으면 예전처럼 몸에서 앞으로 뻗는 도형/시트로 띄운다.
        Vec2 end = d.pos + Vec2{aimSign, 0.f} * s.range;
        addSkillFx(s, FxType::LineBlast, d.pos, end, skillRadius(s), 0.45f, sp.color);
        return;
    }
    constexpr float kFoot = 240.f / 256.f;
    const float bodyCell = s.motionAnim.frameW > 0 ? static_cast<float>(s.motionAnim.frameW) : 256.f;
    const float bodyH = cfg::kDragonRadius * 3.f * sizeScale_ * s.motionAnim.renderScale;
    const Vec2 anchor{d.pos.x + sign * (s.fxAnchorX / bodyCell - 0.5f) * bodyH,
                      d.pos.y + cfg::kDragonRadius + (s.fxAnchorY / bodyCell - kFoot) * bodyH};
    const float fxCell = s.fxAnim.frameW > 0 ? static_cast<float>(s.fxAnim.frameW) : bodyCell;
    const float fxH = cfg::kDragonRadius * 3.f * sizeScale_ * s.fxScale * s.fxAnim.renderScale;
    const float ox = s.fxOriginX >= 0.f ? s.fxOriginX : fxCell * 0.5f;
    const float oy = s.fxOriginY >= 0.f ? s.fxOriginY : fxCell * 0.5f;
    const Vec2 center{anchor.x - sign * (ox / fxCell - 0.5f) * fxH, anchor.y - (oy / fxCell - 0.5f) * fxH};
    // b 는 뒤집기 판정(Renderer::drawFxSheet 의 dirSign)에만 쓰인다.
    addSkillFx(s, FxType::LineBlast, center, center + Vec2{sign, 0.f}, skillRadius(s), 0.45f, sp.color);
}

// 패시브 PassiveOnKill. 적을 쓰러뜨릴 때마다 해제된 것을 모두 적용한다.
void World::onEnemyKilled(Vec2 at) {
    DragonActor& d = dragon_;
    if (!d.alive) return;
    const SpeciesDef& sp = speciesDef(d.species);
    bool soulAwarded = false;
    for (const SkillDef& s : sp.skills) {
        if (s.kind != SkillKind::PassiveOnKill) continue;
        if (d.level < s.unlockLevel) continue;
        if (!soulAwarded && sp.key == "revenant" && s.grantsSoul && d.souls < kRevenantSoulCap) {
            ++d.souls;
            soulAwarded = true;
            const float hudTop = d.pos.y + cfg::kDragonRadius - cfg::kDragonRadius * 3.f * sizeScale_;
            addText("영혼 +1", {d.pos.x, hudTop - 60.f}, 0xFFA6FFF0, 12.f, 0.7f);
        }
        if (s.power <= 0.f) continue;
        float before = d.hp;
        d.hp = std::min(d.stats.maxHp, d.hp + d.stats.maxHp * s.power);
        float healed = d.hp - before;
        if (healed >= 1.f) {
            // 2026-11-17: fx= 를 적은 처치 패시브면 그 시트를, 아니면 전처럼 도형을 띄운다.
            // 자리는 쓰러진 적(at)이다 — 영혼이 거기서 솟는다.
            addSkillFx(s, FxType::Heal, at, d.pos, 44.f, 0.5f, sp.color | 0xFF000000);
            addText("+" + intText(healed), d.pos - Vec2{0.f, 64.f}, kColorHeal, 14.f, 0.7f);
        }
    }
}

// v0.9: 싸울 때 유지할 간격. 설정에 따라 붙거나 떨어져서 싸운다.
// 바깥 간격은 "지금 쓸 수 있는 가장 긴 스킬"을 기준으로 한다. 레벨이 올라 사거리가 긴 스킬이
// 열리면 자연히 더 떨어져서 싸우게 된다.
float World::desiredRange() const {
    switch (keepRange_) {
        case 1: return cfg::kBasicAttackRange * cfg::kKeepMidRatio;
        case 2: {
            const SpeciesDef& sp = speciesDef(dragon_.species);
            float best = 0.f;
            // v1.4: 장착한 칸만 본다. 해제만 되고 빼 둔 스킬 때문에 멀찍이 서 있으면 안 된다.
            for (int slot = 0; slot < kSkillSlots; ++slot) {
                int i = slotSkill(slot);
                if (!skillUnlocked(i) || sp.skills[static_cast<size_t>(i)].ultimate) continue;
                best = std::max(best, skillRange(sp.skills[static_cast<size_t>(i)]));
            }
            // 쓸 수 있는 스킬이 없으면 붙어서 기본 공격으로 싸우는 수밖에 없다
            if (best <= 0.f) return cfg::kBasicAttackRange * cfg::kKeepNearRatio;
            return best * cfg::kKeepFarRatio;
        }
        default: return cfg::kBasicAttackRange * cfg::kKeepNearRatio;
    }
}

void World::dragonAi(float dt, bool castSkills, const PlayerInput& in, bool allowManual) {
    DragonActor& d = dragon_;
    Enemy* t = nearestEnemy(d.pos, 1e9f);
    if (!t) {
        wander(dt);  // 애니메이션은 updateCombat 끝에서 한 번만 진행한다
        return;
    }
    // 사람이 방향키를 잡고 있으면 그동안은 사람 뜻대로 움직인다(스킬 수동에서만).
    if (allowManual && (std::fabs(in.moveX) > 0.01f || std::fabs(in.moveY) > 0.01f)) {
        moveDragon(dt, {in.moveX, in.moveY}, 1.f);
        return;
    }
    float desired = desiredRange();
    float dist = distance(d.pos, t->pos) - t->radius;
    Vec2 dir{0.f, 0.f};
    if (dist > desired) {
        dir = (t->pos - d.pos).normalized();
    } else if (dist < desired - cfg::kKeepSlack) {
        // 너무 붙었으면 물러난다. 여유(kKeepSlack)를 두어 앞뒤로 떠는 것을 막는다.
        dir = (d.pos - t->pos).normalized();
    }
    moveDragon(dt, dir, 1.f);
    // 물러나는 중에도 적을 바라본다(뒷걸음질). 그래야 스킬이 적 쪽으로 나간다.
    if (std::fabs(t->pos.x - d.pos.x) > 4.f) d.facing = Vec2{t->pos.x > d.pos.x ? 1.f : -1.f, 0.f};
    if (castSkills) {
        // 2026-11-20: 궁극기는 쓸 수 있게 되면 가장 먼저 쓴다(자동 모드 AI 사용, 사용자 결정).
        if (!ultimateBlockedReason(true) && castUltimate(true)) return;
        // v1.4: 자동 전투도 장착한 칸만 쓴다. 칸 순서가 곧 우선순위다.
        for (int slot = 0; slot < kSkillSlots; ++slot) {
            int i = slotSkill(slot);
            if (slotReady(slot) && castSkill(i, true)) break;
        }
    }
}

void World::updateWander(float dt) {
    if (dragon_.alive) wander(dt);
    updateDragonAnim(dt, true);
}

const char* idleBehaviorName(IdleBehavior b) {
    switch (b) {
        case IdleBehavior::Rest: return "쉬는 중";
        case IdleBehavior::Walk: return "걷는 중";
        case IdleBehavior::Fly: return "나는 중";
    }
    return "";
}

void World::sampleRoamCursor(float normalizedX, float dt) {
    // 이동 평균은 화면 좌우 비율로 유지한다. 중앙 부근에서는 기존 방향을 유지한다.
    roamCursorX_ += (std::clamp(normalizedX, 0.f, 1.f) - roamCursorX_) *
                    (1.f - std::exp(-std::max(0.f, dt) / 10.f));
    if (roamCursorX_ < 0.45f) roamSide_ = 1;
    if (roamCursorX_ > 0.55f) roamSide_ = -1;
}

float World::randomRoamX() {
    const float center = bounds_.center().x;
    const float margin = std::min(80.f, bounds_.w * 0.2f);
    const float left = roamSide_ > 0 ? center : bounds_.x + margin;
    const float right = roamSide_ < 0 ? center : bounds_.right() - margin;
    return rng_.uniform(left, right);
}

// 서 있을 수 있는 가장 아래 줄. 영역이 얇아도 발이 바닥 밖으로 나가지 않는다.
float World::groundLine() const { return bounds_.bottom() - cfg::kDragonRadius; }

Vec2 World::randomRoamTarget(bool fly) {
    // 걷기는 아래쪽 바닥을 따라, 날기는 화면 위쪽까지 올라간다.
    float topRatio = fly ? cfg::kIdleFlyTopRatio : cfg::kIdleWalkTopRatio;
    float top = bounds_.y + bounds_.h * topRatio;
    float bottom = groundLine();
    if (top > bottom) top = bottom;  // 영역이 드래곤보다 얇을 때(얇은 전투 띠)를 대비
    return {randomRoamX(), rng_.uniform(top, bottom)};
}

float World::randomGroundY() {
    float top = bounds_.y + bounds_.h * cfg::kIdleWalkTopRatio;
    float bottom = groundLine();
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

// v1.0: "아래쪽에만" 을 켜면 순간이동시키지 않고 날아서 내려간다.
// 경계는 도착한 뒤에 띠로 바꾼다. 미리 바꾸면 moveDragon 이 매 프레임 끌어내려 순간이동이 된다.
void World::beginConfine(const RectF& band) {
    confineBand_ = band;
    DragonActor& d = dragon_;
    float targetY = band.bottom() - cfg::kDragonRadius;
    if (d.pos.y >= targetY - cfg::kGroundEpsilon) {
        // 이미 띠 높이에 있으면 바로 경계만 바꾼다
        bounds_ = band;
        confining_ = false;
        d.pos = bounds_.clampPoint(d.pos, cfg::kDragonRadius);
        d.groundY = d.pos.y;
        return;
    }
    confining_ = true;
    d.behavior = IdleBehavior::Fly;
    d.flying = true;
    d.landing = true;
    d.moving = true;
    d.restTime = 0.f;
    d.wanderTarget = {d.pos.x, targetY};
    d.behaviorLeft = cfg::kIdleLandMax;
}

void World::wander(float dt) {
    DragonActor& d = dragon_;
    // 띠로 내려가는 중에는 다른 행동을 고르지 않는다. 닿으면 경계를 띠로 바꾼다.
    if (confining_) {
        float targetY = confineBand_.bottom() - cfg::kDragonRadius;
        d.wanderTarget = {d.pos.x, targetY};
        if (d.pos.y >= targetY - cfg::kGroundEpsilon) {
            bounds_ = confineBand_;
            confining_ = false;
            d.flying = false;
            d.landing = false;
            d.moving = false;
            d.pos = bounds_.clampPoint(d.pos, cfg::kDragonRadius);
            d.groundY = d.pos.y;
            d.behaviorLeft = 0.f;
            return;
        }
        moveDragon(dt, (d.wanderTarget - d.pos).normalized(), cfg::kIdleFlySpeed);
        d.flying = true;
        d.moving = true;
        return;
    }
    // v0.9: 사람이 잡고 있는 동안에는 스스로 움직이지 않는다. 그렇지 않으면 끌어다 놓는 사이에
    // 자동 행동이 위치를 덮어써서 손에서 빠져나가는 것처럼 보였다.
    if (d.held) {
        d.flying = false;
        d.moving = false;
        d.restTime = 0.f;
        d.behaviorLeft = 0.f;  // 놓으면 바로 새 행동을 고른다
        return;
    }
    if (d.dashTime > 0.f) {
        moveDragon(dt, {0.f, 0.f}, 1.f);
        return;
    }

    if (d.animForced && !d.landing && (d.forcedAnim == AnimState::Idle || d.forcedAnim == AnimState::Rest)) {
        d.moving = false;
        return;
    }

    d.behaviorLeft -= dt;
    if (d.behaviorLeft <= 0.f && !d.landing) pickBehavior();
    if (d.landing && distance(d.pos, d.wanderTarget) < 8.f) {
        d.pos.y = d.groundY;
        pickBehavior();
        d.moving = false;
        d.flying = false;
        return;
    }

    const bool walkForFlyPose = d.animForced && d.forcedAnim == AnimState::Fly && d.behavior == IdleBehavior::Rest;

    if (d.behavior == IdleBehavior::Rest && !walkForFlyPose) {
        d.flying = false;
        d.moving = false;
        d.restTime += dt;  // 오래 쉬면 엎드린다
        return;
    }
    d.restTime = 0.f;

    // 목적지에 닿았을 때
    const bool wrongSide = !d.landing && (d.wanderTarget.x - bounds_.center().x) * roamSide_ < 0.f;
    if (wrongSide || distance(d.pos, d.wanderTarget) < 8.f) {
        if (d.behavior != IdleBehavior::Fly) {
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
        d.moving = dir.length() > 0.f;
        if (std::fabs(dir.x) > 0.01f) d.facing = {dir.x > 0.f ? 1.f : -1.f, 0.f};
    } else {
        moveDragon(dt, dir, mul);
    }
}

void World::enemyAttack(Enemy& e) {
    Vec2 toDragon = dragon_.pos - e.pos;
    Vec2 dir = toDragon.normalized();
    if (dir.length() < 0.01f) dir = {e.facing, 0.f};
    if (enemyDef(e.kind).role != EnemyRole::Boss) {
        AttackZone z;
        z.shape = ZoneShape::Circle;
        z.a = e.pos + dir * 36.f * sizeScale_;
        z.radius = cfg::kImpZoneRadius * sizeScale_;
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
        z.b = e.pos + dir * cfg::kGuardianBeamLength * sizeScale_;
        z.radius = cfg::kGuardianBeamHalfWidth * sizeScale_;
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
            z.radius = cfg::kGuardianMeteorRadius * sizeScale_;
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
        z.radius = cfg::kGuardianSlamRadius * sizeScale_;
        z.windup = 1.2f * k;
        z.damage = e.atk * 1.6f;
        z.ownerId = e.id;
        zones_.push_back(z);
        cast = z.windup;
    }
    e.castTimer = cast;
    e.attackCd = cfg::kGuardianPatternCooldown * k + cast;
}

bool World::updateEnemy(Enemy& e, float dt) {
    if (!e.alive) return false;
    e.squashLeft = std::max(0.f, e.squashLeft - dt);
    e.landLeft = std::max(0.f, e.landLeft - dt);
    if (e.hopping()) {
        e.hopLeft = std::max(0.f, e.hopLeft - dt);
        const float p = 1.f - e.hopLeft / std::max(0.01f, e.hopTime);
        e.pos = e.hopFrom + (e.hopTo - e.hopFrom) * p;
        if (!e.hopping()) e.landLeft = cfg::kSplitLandTime;  // 착지 — 그리는 쪽이 점액(없으면 고리)을 그린다
        return false;
    }
    if (!e.alive) return false;
    e.hitFlash = std::max(0.f, e.hitFlash - dt);
    const float slowed = std::min(dt, e.breathSlowLeft);
    const float moveTime = dt - slowed * (1.f - e.breathMoveFactor);
    e.breathSlowLeft -= slowed;
    if (e.stun > 0.f) {
        e.stun -= dt;
        return false;
    }
    e.attackCd -= dt;
    if (e.castTimer > 0.f) {
        e.castTimer -= dt;
        return false;
    }
    if (!dragon_.alive) return false;

    const EnemyDef& ed = enemyDef(e.kind);
    if (ed.phase2 && !e.phase2 && e.hp < e.maxHp * 0.5f) {
        e.phase2 = true;
        addText(ed.name + "이(가) 분노했다!", e.pos - Vec2{0.f, 90.f}, 0xFFFF7070, 20.f, 1.6f);
        EnemyKind add;
        if (!e.spawnedAdds && ed.summonCount > 0 && enemyFromKey(ed.summonKey, add)) {
            e.spawnedAdds = true;
            // spawnEnemy 가 enemies_ 를 재할당하면 e 는 그 자리에서 무효가 된다.
            // 소환 좌표의 기준점은 반드시 먼저 복사해 둔다 (두 번째 소환이 해제된 메모리를 읽었다).
            const Vec2 origin = e.pos;
            const int count = ed.summonCount;
            const int summonLevel = ed.summonLevel;
            // 아래 spawnEnemy 는 enemies_ 를 재할당할 수 있다. 루프가 끝나면 바로 return
            // 하므로 범위 for 의 반복자는 다시 쓰이지 않고, 좌표·횟수도 위에서 복사해 두었다.
            // 이 셋이 동시에 성립해야 안전하다.
            for (int i = 0; i < count; ++i) {
                float dy = -80.f + 160.f * (count == 1 ? 0.5f : static_cast<float>(i) / (count - 1));
                // cppcheck-suppress invalidContainerLoop
                spawnEnemy(add, summonLevel, bounds_.clampPoint(origin + Vec2{-120.f, dy}, 40.f));
            }
            return true;  // 벡터가 재할당되었을 수 있으므로 이번 프레임은 여기서 멈춘다
        }
    }

    Vec2 to = dragon_.pos - e.pos;
    float dist = to.length();
    if (std::fabs(to.x) > 4.f) e.facing = to.x > 0.f ? 1.f : -1.f;
    // 몬스터도 몸집이 커지면 닿는 거리가 늘어난다(2026-11-17 사람 결정).
    float attackRange = ed.attackRange * sizeScale_;
    float keepDist = attackRange * ed.keepRatio;
    if (dist <= attackRange && e.attackCd <= 0.f) {
        enemyAttack(e);
        return false;
    }
    if (dist > keepDist) e.pos += to.normalized() * (e.speed * moveTime);
    if (e.entering) {
        // 경계 안으로 완전히 들어오면 그때부터 가둔다
        if (bounds_.contains(e.pos.x, e.pos.y)) e.entering = false;
    } else {
        e.pos = bounds_.clampPoint(e.pos, e.radius);
    }
    return false;
}

void World::updateEnemies(float dt) {
    for (Enemy& e : enemies_) {
        // T7: 가장 가까운 살아 있는 드래곤(조작 드래곤 또는 동료)을 노린다
        const int target = e.alive ? nearestDragonFor(e.pos) : -1;
        if (target >= 0) swapAlly(target);
        const bool stop = updateEnemy(e, dt);
        if (target >= 0) swapAlly(target);
        if (stop) return;  // enemies_ 가 재할당되었을 수 있다
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
                if (enemyDef(a.kind).role != EnemyRole::Boss && !(a.breathSlowLeft > 0.f && a.breathMoveFactor == 0.f)) a.pos -= push;
                if (enemyDef(b.kind).role != EnemyRole::Boss && !(b.breathSlowLeft > 0.f && b.breathMoveFactor == 0.f)) b.pos += push;
            }
        }
    }
}

void World::damageEnemy(Enemy& e, float raw, bool showNumber, bool detonatesCurse) {
    if (!e.alive || e.hopping()) return;  // 분열해 날아가는 중에는 맞지 않는다
    const int consumed = detonatesCurse && e.curseRemaining > 0.0 ? e.curseStacks : 0;
    const bool exploded = consumed > 0;
    if (exploded) {
        e.curseStacks = 0;
        e.curseRemaining = 0.0;
    }
    float dmg = mitigate(raw * (1.f + static_cast<float>(consumed) / kCurseMaxStacks), e.def);
    const EnemyDef& edef = enemyDef(e.kind);
    const bool guarded = edef.splitGuard > 0.f && aliveChildren(e.id) > 0;
    if (guarded) dmg *= 1.f - edef.splitGuard;  // 분열체가 본체를 지킨다
    e.hp -= dmg;
    // 분열: 체력이 정해진 만큼 깎일 때마다(죽는 타격이면 낳지 않는다)
    if (edef.splitEvery > 0.f && edef.splitCount > 0 && e.hp > 0.f) {
        EnemyKind child{};
        if (enemyFromKey(edef.splitKey, child)) {
            if (e.splitNext < 0.f) e.splitNext = e.maxHp * (1.f - edef.splitEvery);
            int guard = 0;
            while (e.hp <= e.splitNext && e.splitNext > 0.f && guard++ < 8) {
                pendingSplits_.push_back({child, std::max(1, e.level - edef.splitLevelDrop), e.pos, edef.splitCount, e.id, e.kind});
                e.squashLeft = cfg::kSplitSquashTime;
                e.splitNext -= e.maxHp * edef.splitEvery;
                addText("분열!", e.pos - Vec2{0.f, e.radius + 40.f}, 0xFF8CFF8C, 20.f, 1.0f);
            }
        }
    }
    e.hitFlash = 0.15f;
    if (showNumber) {
        const std::string number = exploded ? "저주 폭발! " + intText(dmg) : intText(dmg);
        addText(number, e.pos - Vec2{0.f, e.radius + 14.f},
                exploded ? 0xFFE4AEFF : guarded ? 0xFF9AA0B4 : kColorDamageToEnemy, exploded ? 19.f : 16.f, 0.8f);
    }
    if (e.hp <= 0.f) {
        e.alive = false;
        e.curseStacks = 0;
        e.curseRemaining = 0.0;
        e.curseZoneCooldown = 0.0;
        events_.push_back({WorldEventType::EnemyKilled, e.kind, e.expReward, e.level});
        addFx(FxType::Death, e.pos, e.pos, e.radius * 1.6f, 0.5f, 0xFFFFFFFF);
        addText("+" + std::to_string(e.expReward) + " EXP", e.pos - Vec2{0.f, e.radius + 34.f}, 0xFFB6E3FF, 14.f, 1.1f);
        onEnemyKilled(e.pos);  // v1.4 처치 패시브
    }
}

void World::damageDragon(float raw) {
    DragonActor& d = dragon_;
    if (!d.alive) return;
    float dmg = mitigate(raw, d.stats.def);
    if (d.shieldTime > 0.f) dmg *= (1.f - d.shieldPct);
    if (d.guardTime > 0.f) dmg *= cfg::kGuardDamageMul;  // T6 수호의 물약
    d.hp -= dmg;
    d.hitFlash = 0.2f;
    if (!d.procession && (!d.breath || d.breath->inputLock <= 0.f) &&
        (!d.skillCast || !d.skillCast->soulfireCastId)) setDragonAnim(AnimState::Hit, cfg::kAnimHitHold);
    statHitsTaken++;
    addText(intText(dmg), d.pos - Vec2{0.f, 52.f}, kColorDamageToDragon, 20.f, 0.9f);
    addFx(FxType::Hit, d.pos, d.pos, 46.f, 0.25f, kColorDamageToDragon);
    if (d.hp <= 0.f) {
        d.hp = 0.f;
        d.alive = false;
        resetSouls();
        d.breath.reset();
        if (d.skillCast && d.skillCast->soulfireCastId) advanceSkillCast(0.f, true);
        events_.push_back({WorldEventType::DragonDied, EnemyKind{}, 0, 0, swappedAlly_});
        addFx(FxType::Death, d.pos, d.pos, 90.f, 0.9f, speciesDef(d.species).color);
    }
}

void World::resolveZone(const AttackZone& z, CombatMode mode) {
    if (z.shape == ZoneShape::Circle) addFx(FxType::Ring, z.a, z.a, z.radius, 0.25f, 0xFFFF6040);
    else addFx(FxType::LineBlast, z.a, z.b, z.radius, 0.3f, 0xFFFF6040);
    // T7: 예고 범위 안의 모든 드래곤이 맞는다. 동료는 자동 사냥과 같이 확률로 피한다.
    const auto hit = [&](CombatMode m) {
        DragonActor& d = dragon_;
        if (!d.alive) return;
        float reach = cfg::kDragonRadius * 0.4f * sizeScale_;
        bool inside = z.shape == ZoneShape::Circle ? distance(d.pos, z.a) <= z.radius + reach
                                                   : distancePointSegment(d.pos, z.a, z.b) <= z.radius + reach;
        if (!inside) return;
        bool dodged = d.invuln > 0.f;
        // 스킬 수동·자동 모드는 공격마다 확률로 회피한다(무적 보장 없음)
        if (!dodged && m != CombatMode::Direct) dodged = rng_.chance(dragonDodgeChance());
        if (dodged) {
            statDodges++;
            addText("회피!", d.pos - Vec2{0.f, 56.f}, kColorDodge, 20.f, 0.8f);
            addFx(FxType::Dodge, d.pos, d.pos, 50.f, 0.35f, kColorDodge);
            return;
        }
        damageDragon(z.damage);
    };
    hit(mode);
    for (int k = 0; k < static_cast<int>(party_.size()); ++k) {
        swapAlly(k);
        hit(CombatMode::Auto);
        swapAlly(k);
    }
}

void SoulfireHit::advance(float dt, bool damaged) {
    const bool wasVisible = intensity > 0.f;
    if (damaged) {
        intensity = wasVisible ? std::min(1.f, intensity + dt * (0.5f / 0.08f)) : 0.5f;
        releaseAge = 0.f;
    } else if (intensity > 0.f && dt > 0.f) {
        if (releaseAge == 0.f) releaseStart = intensity;
        releaseAge += dt;
        intensity = releaseStart * std::max(0.f, 1.f - releaseAge / 0.10f);
    }
    if (intensity == 0.f) *this = {};
    else if (wasVisible) loopAge = std::fmod(loopAge + dt, 0.8f);
}

void World::advanceBoneShot(Projectile& shot, BoneShotFlight& flight, float dt) {
    const RectF viewport = projectileViewport_.value_or(bounds_);
    if (!viewport.contains(shot.pos.x, shot.pos.y)) {
        shot.alive = false;
        return;
    }

    const float elapsed = flight.firstStep >= 0.f ? flight.firstStep : dt;
    flight.firstStep = -1.f;
    if (const Enemy* target = findEnemy(shot.targetId)) {
        const Vec2 towardTarget = (target->pos - shot.pos).normalized();
        if (towardTarget.length() > 0.f) flight.aim = towardTarget;
    } else {
        shot.targetId = 0;
    }
    const Vec2 dir = flight.aim;
    float edge = std::numeric_limits<float>::max();
    if (dir.x > 0.f) edge = (viewport.right() - shot.pos.x) / dir.x;
    if (dir.x < 0.f) edge = (viewport.x - shot.pos.x) / dir.x;
    if (dir.y > 0.f) edge = std::min(edge, (viewport.bottom() - shot.pos.y) / dir.y);
    if (dir.y < 0.f) edge = std::min(edge, (viewport.y - shot.pos.y) / dir.y);
    const float limit = std::min(flight.remaining, edge);
    const float step = std::min(limit, shot.speed * elapsed);
    if (step > 0.f) {
        const SpeciesDef& species = speciesDef(dragon_.species);
        const SkillDef& skill = species.skills[shot.skill];
        for (Enemy& enemy : enemies_) {
            if (!enemy.alive || std::find(flight.hitIds.begin(), flight.hitIds.end(), enemy.id) != flight.hitIds.end())
                continue;
            const Vec2 offset = enemy.pos - shot.pos;
            const float along = offset.x * dir.x + offset.y * dir.y;
            const float across = offset.x * dir.y - offset.y * dir.x;
            const float radius = scaledBody(enemy.radius, flight.scale);
            const float halfChordSquared = radius * radius - across * across;
            if (halfChordSquared < 0.f) continue;
            const float halfChord = std::sqrt(halfChordSquared);
            const float contact = std::max(0.f, along - halfChord);
            if (contact > step || along + halfChord < 0.f) continue;

            flight.hitIds.push_back(enemy.id);
            if (shot.targetId == enemy.id) shot.targetId = 0;
            const Vec2 at = shot.pos + dir * contact;
            addFx(FxType::Hit, at, at, enemy.radius * 2.6f * flight.scale, kBoneShotImpactDuration, species.color);
            Fx& impact = fx_.back();
            impact.key = skill.fxImpact;
            impact.inFront = true;
            damageEnemy(enemy, shot.damage);
        }
    }
    shot.pos += dir * step;
    shot.t += step / shot.speed;
    flight.remaining -= step;
    shot.alive = step < limit;
}

void World::controlDragon(float dt, CombatMode mode, const PlayerInput& in, bool ultimateInput) {
    DragonActor& d = dragon_;
    // 전투 중에는 자동 행동(날기)을 쓰지 않는다. 적이 없어 wander가 돌면 그 안에서 다시 켜진다.
    d.flying = false;
    // 타이머
    d.basicCd = std::max(0.f, d.basicCd - dt);
    const float cdStep = dt * (d.swiftTime > 0.f ? cfg::kSwiftCdMul : 1.f);  // T6 신속의 물약
    for (float& c : d.skillCd) c = std::max(0.f, c - cdStep);
    d.dodgeCd = std::max(0.f, d.dodgeCd - dt);
    d.invuln = std::max(0.f, d.invuln - dt);
    d.hitFlash = std::max(0.f, d.hitFlash - dt);
    if (d.shieldTime > 0.f) d.shieldTime -= dt;
    if (d.hasteTime > 0.f) d.hasteTime -= dt;
    d.furyTime = std::max(0.f, d.furyTime - dt);
    d.guardTime = std::max(0.f, d.guardTime - dt);
    d.swiftTime = std::max(0.f, d.swiftTime - dt);

    if (mode == CombatMode::Direct && in.dodge && d.dodgeCd <= 0.f && d.procession)
        cancelProcession();
    const auto& activeProcession = d.procession;
    const float windupLeft = activeProcession && activeProcession->launched == 0
        ? std::max(0.f, ProcessionCast::firstLaunch - activeProcession->elapsed) : 0.f;
    const float soulfireLock = d.skillCast && d.skillCast->soulfireCastId &&
        !(mode == CombatMode::Direct && in.dodge && d.dodgeCd <= 0.f && !d.skillCast->hitDone)
        ? d.skillCast->lockTime - d.skillCast->elapsed : 0.f;
    const float moveDt = std::max(0.f, dt - std::max(windupLeft, soulfireLock));
    advanceProcession(dt);
    advanceBreath(dt);
    if (mode == CombatMode::Direct && in.dodge && d.dodgeCd <= 0.f && d.skillCast &&
        d.skillCast->soulfireCastId && !d.skillCast->hitDone)
        cancelSkillCast(cfg::kSkillCastCancelCooldown);
    if (mode != CombatMode::Auto && d.skillCast && d.skillCast->boneShotBasic) {
        for (int slot = 0; slot < kSkillSlots; ++slot) {
            if (in.skill[slot] && slotSkill(slot) == d.skillCast->skill) {
                cancelSkillCast(0.f);
                break;
            }
        }
    }
    advanceSkillCast(dt);
    if (d.alive && d.stats.regen > 0.f) d.hp = std::min(d.stats.maxHp, d.hp + d.stats.regen * dt);
    // 2026-11-18: 타격 전 직접 조작 회피는 시전을 끊고 회피한다(쿨타임 일부 반환).
    if (mode == CombatMode::Direct && in.dodge && d.dodgeCd <= 0.f && d.skillCast && !d.skillCast->hitDone)
        cancelSkillCast(cfg::kSkillCastCancelCooldown);
    if (d.alive && ultimateInput) castUltimate(false);
    const auto& procession = d.procession;
    const bool preparingProcession = procession && procession->launched == 0;
    if (d.alive && !preparingProcession && (!d.breath || d.breath->elapsed >= d.breath->inputLock) && !skillCastLocked()) {
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
            moveDragon(moveDt, {in.moveX, in.moveY}, 1.f);
        } else {
            // 스킬 수동에서는 방향키로 직접 밀 수 있다. 자동은 손대지 않는다.
            dragonAi(moveDt, mode == CombatMode::Auto && !d.procession, in, mode == CombatMode::SkillManual);
        }
        if (d.dashTime > 0.f) d.dashTime -= moveDt;

        if (mode != CombatMode::Auto && !d.procession) {
            // v1.4: 키 1~4는 장착 칸이다. 빈 칸을 누르면 아무 일도 일어나지 않는다.
            for (int slot = 0; slot < kSkillSlots; ++slot) {
                if (!in.skill[slot]) continue;
                int i = slotSkill(slot);
                if (i < 0) {
                    addText("빈 칸", d.pos - Vec2{0.f, 60.f}, 0xFFCCCCCC, 14.f, 0.6f);
                    continue;
                }
                castSkill(i, false);
            }
        }
        if (!d.procession) basicAttack(dt);
    }
    advanceSteelAttack(dt);
    advanceCoreOverheat(dt);

}

void World::updateCombat(float dt, CombatMode mode, const PlayerInput& in) {
    currentMode_ = mode;
    DragonActor& d = dragon_;
    for (Enemy& enemy : enemies_) {
        enemy.curseRemaining = std::max(0.0, enemy.curseRemaining - dt);
        enemy.curseZoneCooldown = std::max(0.0, enemy.curseZoneCooldown - dt);
        if (enemy.curseRemaining == 0.0) enemy.curseStacks = 0;
    }

    const bool ultimateInput = std::exchange(ultimateRequested_, false) || (mode != CombatMode::Auto && in.ultimate);
    controlDragon(dt, mode, in, ultimateInput);
    // T7: 동료는 자동 사냥과 같은 AI 로 싸운다(dragon_ 자리에 잠깐 바꿔 넣는다)
    for (int k = 0; k < static_cast<int>(party_.size()); ++k) {
        if (!party_[static_cast<size_t>(k)].actor.alive) continue;
        swapAlly(k);
        controlDragon(dt, CombatMode::Auto, PlayerInput{}, false);
        swapAlly(k);
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
        if (p.steelBasic) {
            advanceSteelBasicFlight(p, dt);
            continue;
        }
        if (p.boneShot) {
            advanceBoneShot(p, *p.boneShot, dt);
            continue;
        }
        if (p.cleave) {
            CleaveFlight& blade = *p.cleave;
            const float elapsed = blade.firstStep >= 0.f ? blade.firstStep : dt;
            blade.firstStep = -1.f;
            const float step = std::clamp(kCleaveFlight - p.t, 0.f, elapsed);
            const Vec2 next = p.pos + Vec2{blade.direction * p.speed * step, 0.f};
            for (Enemy& e : enemies_) {
                if (step == 0.f || !e.alive || distancePointSegment(e.pos, p.pos, next) >
                        blade.radius + scaledBody(e.radius, sizeScale_) ||
                    std::find(blade.hitIds.begin(), blade.hitIds.end(), e.id) != blade.hitIds.end()) continue;
                blade.hitIds.push_back(e.id);
                const SkillDef& skill = speciesDef(d.species).skills[p.skill];
                const float targetHeight = e.radius * 2.6f * sizeScale_;
                const Vec2 contact{e.pos.x, e.pos.y + e.radius - (240.f / 256.f - 0.5f) * targetHeight};
                addFx(FxType::CleaveImpact, contact, contact + Vec2{blade.direction, 0.f},
                      targetHeight, kCleaveImpactDuration, 0);
                fx_.back().key = skill.fx;
                damageEnemy(e, p.damage);
            }
            p.pos = next;
            p.t += elapsed;
            p.alive = p.t < kCleaveFlight + kCleaveTail;
            continue;
        }
        Enemy* t = findEnemy(p.targetId);
        if (!t) {
            p.alive = false;
            continue;
        }
        Vec2 to = t->pos - p.pos;
        const float flightDt = p.furnace && p.furnace->firstStep >= 0.f
            ? std::exchange(p.furnace->firstStep, -1.f) : dt;
        const float step = p.furnace ? std::min(p.speed * flightDt, p.furnace->remaining) : p.speed * dt;
        if (to.length() <= step + t->radius * 0.6f) {
            if (p.furnace) {
                const SpeciesDef& sp = speciesDef(d.species);
                const SkillDef& skill = sp.skills[static_cast<size_t>(p.skill)];
                const Vec2 center = t->pos;
                for (Enemy& enemy : enemies_) {
                    if (!enemy.alive) continue;
                    if (enemy.id == p.targetId) damageEnemy(enemy, p.damage);
                    else if (distance(enemy.pos, center) <= skillRadius(skill))
                        damageEnemy(enemy, p.damage * skill.extra);
                }
                addFx(FxType::Ring, center, center, 200.f * sizeScale_, skill.duration, sp.color);
                fx_.back().key = skill.fxImpact;
                fx_.back().t = -dt;
                p.alive = false;
                continue;
            }
            if (p.appliesCurse) applyCurse(*t);
            if (p.skill < 0 && t->curseStacks > 0 && speciesDef(d.species).key == "revenant")
                t->curseRemaining = kCurseDuration;
            damageEnemy(*t, p.damage);
            // 2026-11-17: fx_impact 를 적은 스킬이면 그 시트를, 아니면 전처럼 도형을 띄운다.
            const SpeciesDef& psp = speciesDef(d.species);
            const SkillDef* ps = (p.skill >= 0 && p.skill < static_cast<int>(psp.skills.size()))
                                     ? &psp.skills[static_cast<size_t>(p.skill)] : nullptr;
            if (ps && !ps->fxImpact.empty()) {
                SkillDef hit = *ps;
                hit.fx = ps->fxImpact;  // 맞는 순간용 시트로 바꿔 띄운다
                addSkillFx(hit, FxType::Hit, t->pos, t->pos, p.big ? 40.f : 24.f, 0.2f, psp.color);
            } else {
                addFx(FxType::Hit, t->pos, t->pos, p.big ? 40.f : 24.f, 0.2f, psp.color);
            }
            p.alive = false;
        } else {
            p.pos += to.normalized() * step;
            p.t += flightDt;  // 따라다니는 효과 그림의 재생 시간
            if (p.furnace) {
                p.furnace->remaining -= step;
                p.alive = p.furnace->remaining > 0.f && bounds_.contains(p.pos.x, p.pos.y);
            }
        }
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(), [](const Projectile& p) { return !p.alive; }),
                       projectiles_.end());

    // 지속 피해 장판
    std::vector<std::pair<const DotZone*, float>> trail;
    for (DotZone& z : dots_) {
        float damageDt = dt;
        if (z.soulfireCastId) {
            if (!d.alive) {
                z.remaining = 0.f;
                continue;
            }
            damageDt = std::clamp(dt - z.startDelay, 0.f, z.remaining);
            z.startDelay = 0.f;
            if (damageDt <= 0.f) continue;
            z.remaining -= damageDt;
            trail.emplace_back(&z, damageDt);
            continue;
        }
        z.remaining -= damageDt;
        for (Enemy& e : enemies_) {
            if (e.alive && distance(e.pos, z.pos) <= z.radius + e.radius * 0.5f) {
                if (z.appliesCurse && dt > 0.f && z.remaining >= 0.f && e.curseZoneCooldown == 0.0) {
                    applyCurse(e);
                    e.curseZoneCooldown = kCurseZoneInterval;
                }
                damageEnemy(e, z.dps * damageDt, false);
            }
        }
    }
    std::vector<std::pair<unsigned, float>> exposure;
    for (Enemy& e : enemies_) {
        if (!e.alive) continue;
        if (trail.empty() && e.soulfireHit.intensity == 0.f) continue;
        if (!d.alive) {
            e.soulfireHit = {};
            continue;
        }
        const float hpBeforeSoulfire = e.hp;
        exposure.clear();
        const Vec2 feet{e.pos.x, std::min(e.pos.y + e.radius, bounds_.bottom())};
        for (const auto& [zone, duration] : trail) {
            if (distance(feet, zone->pos) > zone->radius + e.radius * 0.5f) continue;
            auto entry = std::find_if(exposure.begin(), exposure.end(), [&](const auto& value) {
                return value.first == zone->soulfireCastId;
            });
            if (entry == exposure.end()) {
                exposure.emplace_back(zone->soulfireCastId, duration);
                damageEnemy(e, zone->dps * duration, false);
            } else if (duration > entry->second) {
                damageEnemy(e, zone->dps * (duration - entry->second), false);
                entry->second = duration;
            }
        }
        if (e.alive) e.soulfireHit.advance(dt, e.hp < hpBeforeSoulfire);
    }
    dots_.erase(std::remove_if(dots_.begin(), dots_.end(), [](const DotZone& z) { return z.remaining <= 0.f; }), dots_.end());

    // 분열체를 낳는다(본체 둘레에 고르게)
    if (!pendingSplits_.empty()) {
        std::vector<PendingSplit> splits;
        splits.swap(pendingSplits_);
        for (const PendingSplit& sp : splits) {
            for (int c = 0; c < sp.count; ++c) {
                const float ang = 6.2831853f * (static_cast<float>(c) + 0.5f) / static_cast<float>(sp.count) + rng_.uniform(-0.4f, 0.4f);
                const Vec2 at = bounds_.clampPoint(sp.at + Vec2{std::cos(ang), std::sin(ang)} * (110.f * sizeScale_), 30.f);
                Enemy& child = spawnEnemy(sp.kind, sp.level, sp.at, false);
                child.parentId = sp.parentId;
                child.parentKind = sp.parentKind;
                child.hopFrom = sp.at;
                child.hopTo = at;
                child.hopTime = child.hopLeft = cfg::kSplitHopTime;
                child.attackCd = std::max(child.attackCd, 0.6f);  // 착지하자마자 때리지 않는다
                child.facing = at.x >= sp.at.x ? 1.f : -1.f;
            }
        }
    }
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(), [](const Enemy& e) { return !e.alive; }), enemies_.end());

    updateDragonAnim(dt);
    for (int k = 0; k < static_cast<int>(party_.size()); ++k) {
        swapAlly(k);
        updateDragonAnim(dt);
        swapAlly(k);
    }
}

void World::updateFx(float dt, bool combatEnded) {
    dragon_.overheatResidual = std::max(0.f, dragon_.overheatResidual - dt);
    if (combatEnded || !dragon_.alive) dragon_.overheatSkill = -1;
    if (combatEnded) {
        dragon_.steelAttack.reset();
        projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(),
            [](const Projectile& projectile) { return projectile.boneShot.has_value() || projectile.steelBasic.has_value() || projectile.furnace.has_value(); }), projectiles_.end());
        for (Enemy& enemy : enemies_) enemy.soulfireHit = {};
        cancelProcession();
        dots_.erase(std::remove_if(dots_.begin(), dots_.end(), [](const DotZone& z) { return z.soulfireCastId != 0; }),
                    dots_.end());
    }
    if (combatEnded && dragon_.breath) {
        advanceBreath(dt, true);
        updateDragonAnim(dt);
    }
    if (combatEnded && dragon_.skillCast) advanceSkillCast(dt, true);
    for (Fx& f : fx_) f.t += dt;
    fx_.erase(std::remove_if(fx_.begin(), fx_.end(), [](const Fx& f) { return f.t >= f.duration; }), fx_.end());
    for (FloatText& t : texts_) {
        t.t += dt;
        t.pos.y -= 32.f * dt;
    }
    texts_.erase(std::remove_if(texts_.begin(), texts_.end(), [](const FloatText& t) { return t.t >= t.duration; }), texts_.end());
}

// ---------------- 탑 100층 T7: 파티 ----------------

void World::swapAlly(int k) {
    // 같은 k 로 두 번 부르면 제자리로 돌아온다.
    PartyMember& m = party_[static_cast<size_t>(k)];
    std::swap(dragon_, m.actor);
    std::swap(slots_, m.slots);
    swappedAlly_ = swappedAlly_ == k ? -1 : k;
}

void World::setParty(std::vector<PartyMember> allies) {
    party_ = std::move(allies);
    swappedAlly_ = -1;
}

void World::addAlly(Species s, int level, float hp, Vec2 pos, const std::array<int, kSkillSlots>& slots, int id) {
    PartyMember m;
    m.id = id;
    party_.push_back(m);
    const int k = static_cast<int>(party_.size()) - 1;
    swapAlly(k);
    setupDragon(s, level, hp, pos);
    bool any = false;
    for (int v : slots) any = any || v >= 0;
    if (any) slots_ = slots;
    swapAlly(k);
}

void World::setAllyLevel(int index, int level) {
    if (index < 0 || index >= static_cast<int>(party_.size())) return;
    swapAlly(index);
    setDragonLevel(level);
    swapAlly(index);
}

int World::aliveAllyCount() const {
    int n = 0;
    for (const PartyMember& m : party_) n += m.actor.alive ? 1 : 0;
    return n;
}

bool World::promoteAlly(int index) {
    if (index < 0 || index >= static_cast<int>(party_.size())) return false;
    if (!party_[static_cast<size_t>(index)].actor.alive) return false;
    swapAlly(index);   // 쓰러진 조작 드래곤은 동료 자리로, 동료는 조작 자리로
    swappedAlly_ = -1;
    return true;
}

int World::nearestDragonFor(Vec2 from) const {
    int best = dragon_.alive ? -1 : -2;
    float bestD = dragon_.alive ? distance(from, dragon_.pos) : 1e30f;
    for (int k = 0; k < static_cast<int>(party_.size()); ++k) {
        const DragonActor& a = party_[static_cast<size_t>(k)].actor;
        if (!a.alive) continue;
        const float d = distance(from, a.pos);
        if (d < bestD) { bestD = d; best = k; }
    }
    return best;
}

int World::aliveChildren(int parentId) const {
    int n = 0;
    for (const Enemy& e : enemies_) n += (e.alive && e.parentId == parentId) ? 1 : 0;
    return n;
}

}  // namespace dt
