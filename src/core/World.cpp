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
    if (species.key != "revenant" || !dragon_.alive) return {};
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
        if (sp.skills[static_cast<size_t>(idx)].kind == SkillKind::Procession) continue;
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
        d.anim = s;
        d.animTime = 0.f;
    }
    d.animHold = std::max(d.animHold, hold);
}

void World::updateDragonAnim(float dt) {
    DragonActor& d = dragon_;
    d.animTime += dt;
    d.animHold = std::max(0.f, d.animHold - dt);
    // v1.5: 사람이 고른 모션이 있으면 그대로 둔다. 죽음만 예외다.
    if (d.animForced && d.alive && !d.procession) {
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

Enemy& World::spawnEnemy(EnemyKind kind, int level, Vec2 pos) {
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
    cancelProcession();
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
        float speed = d.stats.speed * speedMul * (d.hasteTime > 0.f ? 1.f + d.hasteSpeed : 1.f);
        d.pos += dir.normalized() * (speed * dt);
        d.moving = true;
    }
    if (std::fabs(dir.x) > 0.01f) d.facing = Vec2{dir.x > 0.f ? 1.f : -1.f, 0.f};
    d.pos = bounds_.clampPoint(d.pos, cfg::kDragonRadius);
}

void World::basicAttack() {
    DragonActor& d = dragon_;
    if (d.basicCd > 0.f || (d.breath && d.breath->elapsed < d.breath->inputLock)) return;
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

const char* World::processionBlockedReason(int i, bool inCombat, bool automatic) const {
    const DragonActor& d = dragon_;
    const SpeciesDef& species = speciesDef(d.species);
    if (species.key != "revenant" || !skillUnlocked(i) || i >= static_cast<int>(d.skillCd.size()) ||
        species.skills[static_cast<size_t>(i)].kind != SkillKind::Procession)
        return "궁극기 미해제";
    if (std::find(slots_.begin(), slots_.end(), i) == slots_.end()) return "궁극기 장착 필요";
    if (automatic) return "궁극기는 수동 모드 전용";
    if (d.procession || d.skillCast || (d.breath && d.breath->elapsed < d.breath->inputLock) || d.held)
        return "시전 중 잠금";
    if (d.skillCd[static_cast<size_t>(i)] > 0.f) return "궁극기 재사용 대기";
    if (!ultimateCharge().full()) return "영혼 10개 필요";
    if (!inCombat) return "전투 중에 사용 가능";
    const float range = species.skills[static_cast<size_t>(i)].range;
    if (std::none_of(enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
            return enemy.alive && distance(enemy.pos, d.pos) - enemy.radius <= range;
        })) return "대상 없음";
    return nullptr;
}

bool World::castSkill(int i, bool aiChoice) {
    DragonActor& d = dragon_;
    if (d.breath && d.breath->elapsed < d.breath->inputLock) return false;
    if (d.skillCast || d.procession) return false;  // 2026-11-18: 타격 프레임 스킬이 끝날 때까지 다른 스킬을 막는다
    const SpeciesDef& sp = speciesDef(d.species);
    if (i < 0 || i >= static_cast<int>(sp.skills.size())) return false;
    const SkillDef& s = sp.skills[static_cast<size_t>(i)];
    if (s.kind == SkillKind::Procession) {
        if (aiChoice || currentMode_ == CombatMode::Auto) return false;
        const char* blocked = processionBlockedReason(i, true, false);
        const Enemy* target = blocked ? nullptr : nearestEnemy(d.pos, s.range);
        if (!blocked && !target) blocked = "대상 없음";
        if (blocked) {
            addText(blocked, d.pos - Vec2{0.f, 60.f}, 0xFFCCCCCC, 14.f, 0.8f);
            return false;
        }
        const auto payment = beginUltimatePayment({true, true, true});
        if (!payment) return false;
        const float dx = target->pos.x - d.pos.x;
        const bool left = std::fabs(dx) > 0.01f ? dx < 0.f : d.facing.x < 0.f;
        d.procession = ProcessionCast{i, *payment, d.pos, target->pos, left};
        d.facing = {left ? -1.f : 1.f, 0.f};
        d.dashTime = 0.f;
        d.moving = false;
        d.skillMotion = i;
        d.animTime = 0.f;
        setDragonAnim(AnimState::Attack, ProcessionCast::duration);
        return true;
    }
    if (!skillUnlocked(i)) {
        if (!aiChoice) addText("Lv." + std::to_string(s.unlockLevel) + " 필요", d.pos - Vec2{0.f, 60.f}, 0xFFCCCCCC, 14.f, 0.8f);
        return false;
    }
    if (i >= static_cast<int>(d.skillCd.size())) return false;
    if (d.skillCd[static_cast<size_t>(i)] > 0.f) return false;

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
            for (Enemy& e : enemies_) {
                if (!inSkillCircle(e, d.pos, skillRadius(s), sizeScale_)) continue;
                damageEnemy(e, d.stats.atk * s.power);
                if (s.extra > 0.f) e.stun = std::max(e.stun, s.extra);
            }
            addSkillFx(s, FxType::Ring, d.pos, d.pos, skillRadius(s), 0.45f, sp.color);
            break;
        }
        case SkillKind::Line: {
            Enemy* t = nearestEnemy(d.pos, s.range + 80.f);
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
                                      d.stats.atk * s.power, s.breathInputLock};
                d.breath->facingLeft = std::fabs(dir.x) > 0.01f ? dir.x < 0.f : d.facing.x < 0.f;
                if (!s.breathDeferHit) {
                    d.breath->hit = true;
                    hitLine(d.pos, d.pos + dir * s.range, d.breath->radius, d.breath->damage);
                }
                if (!s.usesUmbralBreath()) {
                    if (std::fabs(dir.x) > 0.01f) d.facing = {dir.x < 0.f ? -1.f : 1.f, 0.f};
                    d.dashTime = 0.f;
                    d.moving = false;
                }
                break;
            }
            Vec2 end = d.pos + dir * s.range;
            hitLine(d.pos, end, skillRadius(s), d.stats.atk * s.power);
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
                if (distancePointSegment(e.pos, d.pos, end) <= skillRadius(s) + scaledBody(e.radius, sizeScale_)) damageEnemy(e, d.stats.atk * s.power);
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
            Enemy* t = nearestEnemy(d.pos, s.range);
            if (!t) {
                if (!aiChoice) addText("대상 없음", d.pos - Vec2{0.f, 60.f}, 0xFFCCCCCC, 14.f, 0.6f);
                return false;
            }
            DotZone z;
            z.pos = t->pos;
            z.radius = skillRadius(s);
            z.dps = d.stats.atk * s.power;
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
        case SkillKind::Procession:  // 위에서 수동 시전만 받는다.
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
    if (d.breath && d.breath->skill == i && !s.usesUmbralBreath()) hold = std::max(hold, BreathCast::duration);
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

void World::advanceProcession(float dt) {
    DragonActor& d = dragon_;
    if (!d.procession) return;
    if (!d.alive || d.held || currentMode_ == CombatMode::Auto) {
        cancelProcession();
        return;
    }
    ProcessionCast& cast = *d.procession;
    cast.elapsed += dt;
    if (!cast.hit && cast.elapsed >= ProcessionCast::impactTime) {
        const SkillDef& skill = speciesDef(d.species).skills[static_cast<size_t>(cast.skill)];
        const float radius = skillRadius(skill);
        const auto inImpact = [&](const Enemy& e) {
            return inSkillCircle(e, cast.target, radius, sizeScale_);
        };
        if (std::none_of(enemies_.begin(), enemies_.end(), inImpact) || !commitUltimatePayment(cast.payment)) {
            cancelProcession();
            return;
        }
        cast.hit = true;
        d.skillCd[static_cast<size_t>(cast.skill)] = skill.cooldown;
        for (Enemy& enemy : enemies_)
            if (inImpact(enemy)) damageEnemy(enemy, d.stats.atk * skill.power);
    }
    if (cast.elapsed >= ProcessionCast::duration) cancelProcession();
}

void World::hitLine(Vec2 start, Vec2 end, float radius, float damage) {
    for (Enemy& e : enemies_) {
        if (e.alive && distancePointSegment(e.pos, start, end) <= radius + scaledBody(e.radius, sizeScale_))
            damageEnemy(e, damage);
    }
}

void World::advanceBreath(float dt, bool combatEnded) {
    DragonActor& d = dragon_;
    if (!d.breath) return;
    if (!d.alive || d.held || (combatEnded && !d.breath->hit)) {
        if (d.breath->inputLock > 0.f) d.animHold = 0.f;
        d.breath.reset();
        return;
    }
    BreathCast& cast = *d.breath;
    cast.elapsed += dt;
    constexpr float epsilon = 1e-6f;
    if (!cast.hit && cast.elapsed + epsilon >= BreathCast::impactTime) {
        cast.hit = true;
        hitLine(cast.origin, cast.origin + cast.aim * cast.range, cast.radius, cast.damage);
    }
    if (cast.elapsed + epsilon >= BreathCast::duration) {
        const bool umbral = speciesDef(d.species).skills[static_cast<size_t>(cast.skill)].usesUmbralBreath();
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
    if (!d.alive || d.held) {
        d.skillCast.reset();  // 죽거나 붙잡히면 효과도 피해도 없이 끝난다
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
    c.elapsed += dt;
    constexpr float epsilon = 1e-6f;
    if (!c.fxDone && c.elapsed + epsilon >= c.fxTime) {
        c.fxDone = true;
        spawnAnchoredFx(s, c.facingLeft);
    }
    if (!c.hitDone && c.elapsed + epsilon >= c.hitTime) {
        c.hitDone = true;
        hitLine(d.pos, d.pos + c.aim * s.range, skillRadius(s), d.stats.atk * s.power);
    }
    if (c.fxDone && c.hitDone && c.elapsed + epsilon >= c.lockTime) d.skillCast.reset();
}

// 타격 전에 끊는다. 남는 쿨타임 = 원래 쿨타임 × cooldownRatio (0이면 바로 다시 쓸 수 있다).
void World::cancelSkillCast(float cooldownRatio) {
    DragonActor& d = dragon_;
    if (!d.skillCast) return;
    const int i = d.skillCast->skill;
    const bool hit = d.skillCast->hitDone;
    d.skillCast.reset();
    if (hit) return;  // 타격 뒤에는 쿨타임을 돌려주지 않는다
    const SpeciesDef& sp = speciesDef(d.species);
    if (i >= 0 && i < static_cast<int>(sp.skills.size()) && i < static_cast<int>(d.skillCd.size()))
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
                if (!skillUnlocked(i) || sp.skills[static_cast<size_t>(i)].kind == SkillKind::Procession) continue;
                best = std::max(best, sp.skills[static_cast<size_t>(i)].range);
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
        // v1.4: 자동 전투도 장착한 칸만 쓴다. 칸 순서가 곧 우선순위다.
        for (int slot = 0; slot < kSkillSlots; ++slot) {
            int i = slotSkill(slot);
            if (slotReady(slot) && castSkill(i, true)) break;
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

// 서 있을 수 있는 가장 아래 줄. 영역이 얇아도 발이 바닥 밖으로 나가지 않는다.
float World::groundLine() const { return bounds_.bottom() - cfg::kDragonRadius; }

Vec2 World::randomRoamTarget(bool fly) {
    const float m = 80.f;
    // 걷기는 아래쪽 바닥을 따라, 날기는 화면 위쪽까지 올라간다.
    float topRatio = fly ? cfg::kIdleFlyTopRatio : cfg::kIdleWalkTopRatio;
    float top = bounds_.y + bounds_.h * topRatio;
    float bottom = groundLine();
    if (top > bottom) top = bottom;  // 영역이 드래곤보다 얇을 때(얇은 전투 띠)를 대비
    float lx = bounds_.x + m, rx = bounds_.right() - m;
    if (lx > rx) { lx = rx = (bounds_.x + bounds_.right()) * 0.5f; }
    return {rng_.uniform(lx, rx), rng_.uniform(top, bottom)};
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

    d.behaviorLeft -= dt;
    if (d.behaviorLeft <= 0.f) pickBehavior();

    if (d.behavior == IdleBehavior::Rest) {
        d.flying = false;
        d.moving = false;
        d.restTime += dt;  // 오래 쉬면 엎드린다
        return;
    }
    d.restTime = 0.f;

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
                return;  // 벡터가 재할당되었을 수 있으므로 이번 프레임은 여기서 멈춘다
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
            continue;
        }
        if (dist > keepDist) e.pos += to.normalized() * (e.speed * dt);
        if (e.entering) {
            // 경계 안으로 완전히 들어오면 그때부터 가둔다
            if (bounds_.contains(e.pos.x, e.pos.y)) e.entering = false;
        } else {
            e.pos = bounds_.clampPoint(e.pos, e.radius);
        }
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
                if (enemyDef(a.kind).role != EnemyRole::Boss) a.pos -= push;
                if (enemyDef(b.kind).role != EnemyRole::Boss) b.pos += push;
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
        e.curseStacks = 0;
        e.curseRemaining = 0.0;
        e.curseZoneCooldown = 0.0;
        events_.push_back({WorldEventType::EnemyKilled, e.kind, e.expReward});
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
    d.hp -= dmg;
    d.hitFlash = 0.2f;
    if (!d.procession && (!d.breath || d.breath->inputLock <= 0.f)) setDragonAnim(AnimState::Hit, cfg::kAnimHitHold);
    statHitsTaken++;
    addText(intText(dmg), d.pos - Vec2{0.f, 52.f}, kColorDamageToDragon, 20.f, 0.9f);
    addFx(FxType::Hit, d.pos, d.pos, 46.f, 0.25f, kColorDamageToDragon);
    if (d.hp <= 0.f) {
        d.hp = 0.f;
        d.alive = false;
        resetSouls();
        d.breath.reset();
        events_.push_back({WorldEventType::DragonDied, EnemyKind{}, 0});
        addFx(FxType::Death, d.pos, d.pos, 90.f, 0.9f, speciesDef(d.species).color);
    }
}

void World::resolveZone(const AttackZone& z, CombatMode mode) {
    DragonActor& d = dragon_;
    if (z.shape == ZoneShape::Circle) addFx(FxType::Ring, z.a, z.a, z.radius, 0.25f, 0xFFFF6040);
    else addFx(FxType::LineBlast, z.a, z.b, z.radius, 0.3f, 0xFFFF6040);
    if (!d.alive) return;

    float reach = cfg::kDragonRadius * 0.4f * sizeScale_;
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

    for (Enemy& enemy : enemies_) {
        enemy.curseRemaining = std::max(0.0, enemy.curseRemaining - dt);
        enemy.curseZoneCooldown = std::max(0.0, enemy.curseZoneCooldown - dt);
        if (enemy.curseRemaining == 0.0) enemy.curseStacks = 0;
    }

    if (mode == CombatMode::Direct && in.dodge && d.dodgeCd <= 0.f && d.procession && !d.procession->hit)
        cancelProcession();
    advanceProcession(dt);
    advanceBreath(dt);
    advanceSkillCast(dt);
    if (d.alive && d.stats.regen > 0.f) d.hp = std::min(d.stats.maxHp, d.hp + d.stats.regen * dt);
    // 2026-11-18: 타격 전 직접 조작 회피는 시전을 끊고 회피한다(쿨타임 일부 반환).
    if (mode == CombatMode::Direct && in.dodge && d.dodgeCd <= 0.f && d.skillCast && !d.skillCast->hitDone)
        cancelSkillCast(cfg::kSkillCastCancelCooldown);
    if (d.alive && !d.procession && (!d.breath || d.breath->elapsed >= d.breath->inputLock) && !skillCastLocked()) {
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
            // 스킬 수동에서는 방향키로 직접 밀 수 있다. 자동은 손대지 않는다.
            dragonAi(dt, mode == CombatMode::Auto, in, mode == CombatMode::SkillManual);
        }
        if (d.dashTime > 0.f) d.dashTime -= dt;

        if (mode != CombatMode::Auto) {
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
        if (!d.procession) basicAttack();
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
            if (p.appliesCurse) applyCurse(*t);
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
            p.t += dt;  // 따라다니는 효과 그림의 재생 시간
        }
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(), [](const Projectile& p) { return !p.alive; }),
                       projectiles_.end());

    // 지속 피해 장판
    for (DotZone& z : dots_) {
        z.remaining -= dt;
        for (Enemy& e : enemies_) {
            if (e.alive && distance(e.pos, z.pos) <= z.radius + e.radius * 0.5f) {
                if (z.appliesCurse && dt > 0.f && z.remaining >= 0.f && e.curseZoneCooldown == 0.0) {
                    applyCurse(e);
                    e.curseZoneCooldown = kCurseZoneInterval;
                }
                damageEnemy(e, z.dps * dt, false);
            }
        }
    }
    dots_.erase(std::remove_if(dots_.begin(), dots_.end(), [](const DotZone& z) { return z.remaining <= 0.f; }), dots_.end());

    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(), [](const Enemy& e) { return !e.alive; }), enemies_.end());

    updateDragonAnim(dt);
}

void World::updateFx(float dt, bool combatEnded) {
    if (combatEnded) cancelProcession();
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

}  // namespace dt
