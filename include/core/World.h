// 전투 시뮬레이션. 바탕화면 사냥과 탑이 같은 규칙(체력·피해·스킬)을 쓴다.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/MathUtil.h"
#include "core/SaveData.h"
#include "core/Species.h"

namespace dt {

// 한 프레임의 플레이어 입력. dodge/skill은 이번 프레임에 눌렸을 때만 true.
// v1.4: skill의 번호는 **장착 칸**(키 1~4)이지 스킬 목록 번호가 아니다.
struct PlayerInput {
    float moveX = 0.f;
    float moveY = 0.f;
    bool dodge = false;
    bool skill[kSkillSlots] = {false, false, false, false};
};

// 전투와 표시가 같은 중첩 상한과 지속 시간을 읽는다.
inline constexpr int kCurseMaxStacks = 5;
inline constexpr int kRevenantSoulCap = 10;
inline constexpr int kUltimateSoulCost = 10;

struct UltimateCharge {
    bool soulHarvestUnlocked = false;
    int souls = 0;
    int percent() const { return 100 * souls / kUltimateSoulCost; }
    bool full() const { return soulHarvestUnlocked && souls == kUltimateSoulCost; }
};

// 실제 궁극기가 생기면 호출자가 해제·전투·시전 제한을 각각 평가해 전달한다.
struct UltimateUseConditions {
    bool skillUnlocked = false;
    bool inCombat = false;
    bool castAllowed = false;
};
inline constexpr double kCurseDuration = 6.0;

struct ProcessionCast {
    static constexpr float chargeStart = 1.40f;
    static constexpr float impactTime = 1.88f;
    static constexpr float duration = impactTime + 1.04f;
    int skill;
    uint64_t payment;
    Vec2 origin;
    Vec2 target;
    bool facingLeft;
    float elapsed = 0.f;
    bool hit = false;
};

struct Enemy {
    int id = 0;
    EnemyKind kind{};
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
    // 화면 밖에서 들어오는 중이면 경계에 가두지 않는다(디펜스 전투의 등장 연출)
    bool entering = false;
    // 보스 전용
    bool phase2 = false;
    bool spawnedAdds = false;
    int patternIndex = 0;
    int curseStacks = 0;
    double curseRemaining = 0.0;
    double curseZoneCooldown = 0.0;  // 같은 적에게 닿는 모든 저주 장판이 공유한다.
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
    // 2026-11-17: 효과 그림을 쓰려고 더했다. Fx 는 시작점·끝점이 고정이라 날아가는 것을
    // 따라갈 수 없어서, 발사체가 자기 스킬 번호와 재생 시간을 들고 다닌다.
    int skill = -1;   // 스킬 목록 번호. -1 이면 전처럼 동그라미로 그린다
    float t = 0.f;    // 애니메이션 시간
    bool appliesCurse = false;
};

struct DotZone {
    Vec2 pos;
    float radius = 80.f;
    float dps = 1.f;
    float remaining = 1.f;
    float duration = 1.f;
    bool appliesCurse = false;
    int spriteSkill = -1;
};

enum class FxType { Ring, Heal, Dodge, Hit, Poof, LineBlast, Dash, LevelUp, Death, Shield };

// v1.6: 효과는 발동 → 지속 → 잔류 세 단계를 지난다.
// 도형으로 그리던 기존 효과도 같은 세 단계를 가진다(전체 길이는 그대로, 비율로 나눈다).
enum class FxPhase { Windup, Hold, Tail };

struct Fx {
    FxType type = FxType::Ring;
    Vec2 a, b;
    float radius = 40.f;
    float t = 0.f;
    float duration = 0.4f;
    uint32_t color = 0xFFFFFFFF;
    // ---- v1.6: 스프라이트 효과. key 가 비어 있으면 전처럼 도형으로만 그린다 ----
    std::string key;      // fx_<key>.png. 비어 있으면 도형만
    bool inFront = true;  // 본체 위에 그리는가
    float scale = 1.f;
    float offsetX = 0.f;  // 100% 기준 px. 바라보는 쪽이 양수 (그릴 때 charScale 을 곱한다)
    float offsetY = 0.f;  // 100% 기준 px. 위가 음수
    float windup = 0.f, hold = 0.f, tail = 0.f;  // 세 단계 길이. 합이 duration 이다
    FxPhase phase() const;
    float phaseProgress() const;  // 지금 단계 안에서 0..1
};

struct FloatText {
    std::string text;
    Vec2 pos;
    float t = 0.f;
    float duration = 1.f;
    uint32_t color = 0xFFFFFFFF;
    float size = 16.f;
};

// 바탕화면에서 사냥하지 않을 때 드래곤이 스스로 고르는 행동.
// 전투 중에는 쓰지 않는다(전투는 모드에 따라 이동·공격이 정해진다).
enum class IdleBehavior { Rest = 0, Walk = 1, Fly = 2 };
const char* idleBehaviorName(IdleBehavior b);

struct BreathCast {
    static constexpr float frameSeconds = 0.12f;
    static constexpr float impactTime = 4 * frameSeconds;
    static constexpr float endTime = 12 * frameSeconds;
    static constexpr float duration = 16 * frameSeconds;
    static constexpr int openMouthFrame = 3;  // Zero-based frame of the Revenant attack sheet.
    int skill = -1;
    Vec2 origin, aim;
    float elapsed = 0.f;
    float range = 0.f, radius = 0.f, damage = 0.f;
    float inputLock = 0.f;
    bool hit = false;
    bool facingLeft = false;

    int frame() const {
        return std::min(15, static_cast<int>((elapsed + 1e-6f) / frameSeconds));
    }
    int bodyFrame(const SkillDef& definition) const {
        if (!definition.usesUmbralBreath()) return openMouthFrame;
        constexpr int windup[] = {2, 4, 5, 6};
        const int i = frame();
        if (i < 4) return windup[i];
        return i < 12 ? 6 : i - 4;
    }
};

// 2026-11-18: hit_frame= 을 적은 스킬의 진행 상태. 시전부터 타격까지는 제자리에 서서
// 모션만 재생하고, fx_frame 에 효과를, hit_frame 에 피해를 낸다(SkillDef::usesTimedHit).
struct SkillCast {
    int skill = -1;
    Vec2 aim;                // 시전할 때 정한 방향. 준비 중 대상이 죽어도 이 방향으로 나간다
    float elapsed = 0.f;
    float fxTime = 0.f;      // 효과를 띄우는 시각
    float hitTime = 0.f;     // 피해를 주는 시각
    float lockTime = 0.f;    // 여기까지 이동·다른 스킬을 막는다
    bool fxDone = false;
    bool hitDone = false;
    bool facingLeft = false;
};

struct DragonActor {
    int souls = 0;  // Current combat session only.
    std::optional<ProcessionCast> procession;
    Species species = Species::Fire;
    int level = 1;
    DragonStats stats{};
    Vec2 pos;
    Vec2 facing{1.f, 0.f};
    float hp = 1.f;
    float basicCd = 0.f;
    // v1.4: 재사용 대기시간은 **스킬마다** 센다(칸이 아니라). 칸을 바꿔 끼워도 대기시간은 그대로다.
    // 크기는 setupDragon에서 그 종의 스킬 수에 맞춘다.
    std::vector<float> skillCd;
    // 지금 재생 중인 스킬 전용 본체 모션의 스킬 번호. -1이면 공용 attack 시트를 쓴다.
    int skillMotion = -1;
    std::optional<BreathCast> breath;
    std::optional<SkillCast> skillCast;  // 2026-11-18: 타격 프레임 스킬 진행 중
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
    // v0.2 시트 애니메이션 상태. 프레임 수는 이미지가 정하므로 여기서는 상태와
    // 경과 시간만 둔다(그리는 쪽이 fps·프레임 수로 현재 칸을 계산한다).
    AnimState anim = AnimState::Idle;
    float animTime = 0.f;  // 현재 상태로 바뀐 뒤 흐른 시간
    float animHold = 0.f;  // 1회 재생 상태를 유지할 남은 시간(0이면 상태 자유)
    // v1.5: 사람이 고른 모션. 다음 입력까지 유지한다(끌어 옮겨도 풀리지 않는다).
    bool animForced = false;
    AnimState forcedAnim = AnimState::Idle;
    // v0.3 바탕화면 자동 행동(사냥하지 않을 때만 쓴다)
    IdleBehavior behavior = IdleBehavior::Rest;
    float behaviorLeft = 0.f;  // 이 행동을 이어 갈 남은 시간
    bool flying = false;       // 날고 있는가(걷기와 그리는 상태가 다르다)
    bool landing = false;      // 날다가 지면으로 내려오는 중
    float groundY = 0.f;       // 걷는 높이. 걷기는 이 높이에서 수평으로만 움직인다
    float restTime = 0.f;      // v0.9 쉬는 행동을 이어 온 시간. 길어지면 엎드린다
    bool held = false;         // v0.9 사람이 마우스로 잡고 있는 중(자동 행동을 멈춘다)
};

enum class WorldEventType { EnemyKilled, DragonDied, EnemySpawned };

struct WorldEvent {
    WorldEventType type;
    EnemyKind enemyKind{};
    int exp = 0;
};

class World {
public:
    World();

    void setBounds(const RectF& r) { bounds_ = r; }
    // v1.0: 띠로 내려가는 중. 순간이동 대신 날아서 내려가고, 닿으면 경계를 띠로 바꾼다.
    void beginConfine(const RectF& band);
    void cancelConfine() { confining_ = false; }
    bool confining() const { return confining_; }
    const RectF& bounds() const { return bounds_; }
    void seed(uint32_t s) { rng_.seed(s); }

    void setupDragon(Species s, int level, float hp, Vec2 pos);
    void resetSouls();
    UltimateCharge ultimateCharge() const;
    bool canUseUltimate(const UltimateUseConditions& conditions) const;
    const char* processionBlockedReason(int skill, bool inCombat, bool automatic) const;
    // 준비는 영혼을 건드리지 않는다. 성공한 타격 한 번에만 같은 ID로 확정한다.
    std::optional<uint64_t> beginUltimatePayment(const UltimateUseConditions& conditions);
    bool commitUltimatePayment(uint64_t id);
    void cancelUltimatePayment(uint64_t id);
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
    void updateFx(float dt, bool combatEnded = false);

    void healPercent(float pct);
    void regen(float pctPerSec, float dt);

    // 드래곤 애니메이션 상태를 진행한다. updateCombat/updateWander가 내부에서 부른다.
    void updateDragonAnim(float dt);
    // 1회 재생 상태로 바꾼다(공격·피격·사망). hold 동안 다른 상태로 덮이지 않는다.
    void setDragonAnim(AnimState s, float hold);

    // 아래 둘은 **스킬 목록 번호**를 받는다(장착 칸이 아니다).
    bool skillUnlocked(int i) const;
    bool skillReady(int i) const;

    // v1.4 장착 칸. Game이 개체 기록(DragonRecord::slots)에서 넣어 준다.
    // 입력(키 1~4)과 자동 전투는 여기 들어 있는 스킬만 쓴다.
    void setSlots(const std::array<int, kSkillSlots>& s) { slots_ = s; }

    // 보기 크기(charScale)를 판정에도 쓴다(2026-11-17). Game이 설정이 바뀔 때마다 넣어 준다.
    // 몸 반지름은 항상, 스킬 반경은 scale_with_size 를 켠 스킬만 따라 커진다.
    void setSizeScale(float s) { sizeScale_ = s > 0.f ? s : 1.f; }
    // 스킬 판정 반경. scale_with_size 를 켠 스킬만 보기 크기를 따라 커진다.
    float skillRadius(const SkillDef& s) const { return s.scaleWithSize ? s.radius * sizeScale_ : s.radius; }
    float sizeScale() const { return sizeScale_; }
    const std::array<int, kSkillSlots>& slots() const { return slots_; }
    // 칸 번호 -> 스킬 목록 번호. 빈 칸이면 -1.
    int slotSkill(int slot) const {
        return (slot < 0 || slot >= kSkillSlots) ? -1 : slots_[static_cast<size_t>(slot)];
    }
    bool slotReady(int slot) const { return skillReady(slotSkill(slot)); }

    std::vector<WorldEvent> takeEvents();

    const std::vector<Enemy>& enemies() const { return enemies_; }
    // 화면에 아직 움직이는 효과·글자가 남아 있는가(조용할 때 그리기를 줄이는 판단에 쓴다)
    bool hasActiveFx() const { return !fx_.empty() || !texts_.empty(); }
    std::vector<Enemy>& enemiesMutable() { return enemies_; }
    const std::vector<AttackZone>& zones() const { return zones_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    const std::vector<DotZone>& dots() const { return dots_; }
    const std::vector<Fx>& fx() const { return fx_; }
    const std::vector<FloatText>& texts() const { return texts_; }

    void addText(const std::string& text, Vec2 pos, uint32_t color, float size = 16.f, float dur = 1.0f);
    void addFx(FxType type, Vec2 a, Vec2 b, float radius, float dur, uint32_t color);
    // v1.6: 스킬의 fx 설정을 붙여서 넣는다. s.fx 가 비어 있으면 addFx 와 완전히 같다
    // (shapeDur 를 그대로 쓰고 단계만 비율로 나눈다) — 기존 스킬의 보이는 모습이 바뀌지 않는다.
    void addSkillFx(const SkillDef& s, FxType fallback, Vec2 a, Vec2 b, float radius, float shapeDur,
                    uint32_t color);

    // 통계(밸런스 시뮬레이션용)
    int statDodges = 0;
    int statHitsTaken = 0;

    // v0.9 싸울 때 유지할 간격(0 근접 / 1 중간 / 2 바깥). 스킬 수동·자동에서 AI가 지킨다.
    void setKeepRange(int v) { keepRange_ = v; }
    int keepRange() const { return keepRange_; }
    // 지금 설정과 해금된 스킬로 계산한 유지 거리
    float desiredRange() const;

private:
    uint64_t nextUltimatePayment_ = 1;
    std::optional<uint64_t> pendingUltimatePayment_;

    void wander(float dt);     // 애니메이션 진행을 포함하지 않는 배회(내부용)
    void pickBehavior();       // 다음 행동을 확률로 고르고 목적지를 정한다
    Vec2 randomRoamTarget(bool fly);
    float randomRoamX();
    float randomGroundY();
    // 발이 영역 바닥에 닿는 세로 좌표. 영역이 얇으면 여기에 붙는다.
    float groundLine() const;
    Enemy* nearestEnemy(Vec2 from, float maxDist);
    Enemy* findEnemy(int id);
    void moveDragon(float dt, Vec2 dir, float speedMul);
    void dragonAi(float dt, bool castSkills, const PlayerInput& in, bool allowManual);
    int keepRange_ = 0;
    std::array<int, kSkillSlots> slots_{-1, -1, -1, -1};
    void autoFillSlots();  // 빈 칸을 해제된 액티브로 채운다(이미 든 칸은 그대로)
    bool castSkill(int i, bool aiChoice);  // i = 스킬 목록 번호
    void hitLine(Vec2 start, Vec2 end, float radius, float damage);
    void advanceBreath(float dt, bool combatEnded = false);
    // 2026-11-18: 타격 프레임 스킬. 타격 전에 끊기면 쿨타임의 일부를 돌려준다.
    void advanceProcession(float dt);
    void cancelProcession();
    void advanceSkillCast(float dt, bool combatEnded = false);
    void cancelSkillCast(float refundRatio);
    void spawnAnchoredFx(const SkillDef& s, bool facingLeft);
    bool skillCastLocked() const { return dragon_.skillCast && dragon_.skillCast->elapsed < dragon_.skillCast->lockTime; }
    // v1.4 패시브 PassiveOnKill. 적을 쓰러뜨렸을 때 damageEnemy가 부른다.
    void onEnemyKilled(Vec2 at);
    void basicAttack();
    void updateEnemies(float dt);
    void enemyAttack(Enemy& e);
    void resolveZone(const AttackZone& z, CombatMode mode);
    void damageEnemy(Enemy& e, float raw, bool showNumber = true);
    void damageDragon(float raw);
    float dragonDodgeChance() const;

    RectF bounds_{0.f, 0.f, 1280.f, 720.f};
    float sizeScale_ = 1.f;  // 보기 크기. 판정에 쓰는 몸·스킬 반경을 여기에 곱한다
    bool confining_ = false;   // 띠로 내려가는 중
    RectF confineBand_{};
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
