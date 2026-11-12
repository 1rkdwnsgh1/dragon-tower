// 전투 시뮬레이션. 바탕화면 사냥과 탑이 같은 규칙(체력·피해·스킬)을 쓴다.
#pragma once

#include <string>
#include <vector>

#include "core/MathUtil.h"
#include "core/SaveData.h"
#include "core/Species.h"

namespace dt {

// 한 프레임의 플레이어 입력. dodge/skill은 이번 프레임에 눌렸을 때만 true.
struct PlayerInput {
    float moveX = 0.f;
    float moveY = 0.f;
    bool dodge = false;
    bool skill[3] = {false, false, false};
};

enum class EnemyKind { Imp, Guardian };

struct Enemy {
    int id = 0;
    EnemyKind kind = EnemyKind::Imp;
    int level = 1;
    Vec2 pos;
    float radius = 24.f;
    float hp = 1.f, maxHp = 1.f;
    float atk = 1.f, def = 0.f, speed = 80.f;
    int expReward = 1;
    float attackCd = 1.f;
    float castTimer = 0.f;   // 공격 예고 중에는 멈춰 선다
    float stun = 0.f;
    float hitFlash = 0.f;
    float facing = 1.f;      // 1 오른쪽, -1 왼쪽
    bool alive = true;
    // 보스 전용
    bool phase2 = false;
    bool spawnedAdds = false;
    int patternIndex = 0;
};

enum class ZoneShape { Circle, Line };

// 적 공격 예고. windup이 끝나는 순간 판정한다.
struct AttackZone {
    ZoneShape shape = ZoneShape::Circle;
    Vec2 a, b;        // Circle: a = 중심, Line: a → b
    float radius = 50.f;  // Circle 반경, Line 반폭
    float windup = 1.f;
    float t = 0.f;
    float damage = 1.f;
    int ownerId = 0;
};

struct Projectile {
    Vec2 pos;
    int targetId = 0;
    float speed = 500.f;
    float damage = 1.f;
    bool big = false;
    bool alive = true;
};

struct DotZone {
    Vec2 pos;
    float radius = 80.f;
    float dps = 1.f;
    float remaining = 1.f;
    float duration = 1.f;
};

enum class FxType { Ring, Heal, Dodge, Hit, Poof, LineBlast, Dash, LevelUp, Death, Shield };

struct Fx {
    FxType type = FxType::Ring;
    Vec2 a, b;
    float radius = 40.f;
    float t = 0.f;
    float duration = 0.4f;
    uint32_t color = 0xFFFFFFFF;
};

struct FloatText {
    std::string text;
    Vec2 pos;
    float t = 0.f;
    float duration = 1.f;
    uint32_t color = 0xFFFFFFFF;
    float size = 16.f;
};

struct DragonActor {
    Species species = Species::Fire;
    int level = 1;
    DragonStats stats{};
    Vec2 pos;
    Vec2 facing{1.f, 0.f};
    float hp = 1.f;
    float basicCd = 0.f;
    float skillCd[3] = {0.f, 0.f, 0.f};
    float dodgeCd = 0.f;
    float dashTime = 0.f;
    Vec2 dashVel;
    float invuln = 0.f;
    float shieldTime = 0.f, shieldPct = 0.f;
    float hasteTime = 0.f, hasteSpeed = 0.f, hasteDodge = 0.f;
    float hitFlash = 0.f;
    bool alive = true;
    bool moving = false;
    Vec2 wanderTarget;
    float wanderWait = 0.f;
};

enum class WorldEventType { EnemyKilled, DragonDied, EnemySpawned };

struct WorldEvent {
    WorldEventType type;
    EnemyKind enemyKind = EnemyKind::Imp;
    int exp = 0;
};

class World {
public:
    World();

    void setBounds(const RectF& r) { bounds_ = r; }
    const RectF& bounds() const { return bounds_; }
    void seed(uint32_t s) { rng_.seed(s); }

    void setupDragon(Species s, int level, float hp, Vec2 pos);
    void setDragonLevel(int level);  // 레벨업 시 능력치 갱신(늘어난 최대 체력만큼 회복)
    DragonActor& dragon() { return dragon_; }
    const DragonActor& dragon() const { return dragon_; }

    Enemy& spawnEnemy(EnemyKind kind, int level, Vec2 pos);
    Vec2 randomEdgePoint(float margin);
    void clearEnemies(bool withPoof);
    int aliveEnemyCount() const;

    // 전투 진행. mode에 따라 이동·스킬·회피 판정 방식이 달라진다.
    void updateCombat(float dt, CombatMode mode, const PlayerInput& in);
    // 적이 없을 때 바탕화면을 천천히 돌아다닌다.
    void updateWander(float dt);
    // 효과와 글자만 진행(일시정지 중에는 부르지 않는다)
    void updateFx(float dt);

    void healPercent(float pct);
    void regen(float pctPerSec, float dt);

    bool skillUnlocked(int i) const;
    bool skillReady(int i) const { return skillUnlocked(i) && dragon_.skillCd[i] <= 0.f; }

    std::vector<WorldEvent> takeEvents();

    const std::vector<Enemy>& enemies() const { return enemies_; }
    const std::vector<AttackZone>& zones() const { return zones_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    const std::vector<DotZone>& dots() const { return dots_; }
    const std::vector<Fx>& fx() const { return fx_; }
    const std::vector<FloatText>& texts() const { return texts_; }

    void addText(const std::string& text, Vec2 pos, uint32_t color, float size = 16.f, float dur = 1.0f);
    void addFx(FxType type, Vec2 a, Vec2 b, float radius, float dur, uint32_t color);

    // 통계(밸런스 시뮬레이션용)
    int statDodges = 0;
    int statHitsTaken = 0;

private:
    Enemy* nearestEnemy(Vec2 from, float maxDist);
    Enemy* findEnemy(int id);
    void moveDragon(float dt, Vec2 dir, float speedMul);
    void dragonAi(float dt, bool castSkills);
    bool castSkill(int i, bool aiChoice);
    void basicAttack();
    void updateEnemies(float dt);
    void enemyAttack(Enemy& e);
    void resolveZone(const AttackZone& z, CombatMode mode);
    void damageEnemy(Enemy& e, float raw, bool showNumber = true);
    void damageDragon(float raw);
    float dragonDodgeChance() const;

    RectF bounds_{0.f, 0.f, 1280.f, 720.f};
    Rng rng_;
    DragonActor dragon_;
    std::vector<Enemy> enemies_;
    std::vector<AttackZone> zones_;
    std::vector<Projectile> projectiles_;
    std::vector<DotZone> dots_;
    std::vector<Fx> fx_;
    std::vector<FloatText> texts_;
    std::vector<WorldEvent> events_;
    int nextEnemyId_ = 1;
    CombatMode currentMode_ = CombatMode::SkillManual;
};

float mitigate(float raw, float def);

}  // namespace dt
