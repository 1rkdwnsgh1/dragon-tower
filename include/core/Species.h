// 드래곤 종류, 능력치, 스킬, 애니메이션 정의.
// v0.2: 종 목록이 고정 배열에서 레지스트리로 바뀌었다. 내장 4종으로 시작하고,
// assets/dragons/<키>/dragon.txt(DragonData.h)가 종을 덮어쓰거나 추가할 수 있다.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dt {

// v1.4 스킬 관리창에서 고를 수 있는 액티브 장착 칸 수. 조작키 1~4와 짝이다.
// 패시브는 이 칸을 쓰지 않는다(해제되면 항상 적용).
constexpr int kSkillSlots = 4;

// v1.5: 내장 4종(화염·대지·바람·물)을 지웠다. 이제 종은 전부 assets/dragons/<키>/ 폴더에서 온다.
// 아래 이름은 "레지스트리의 몇 번째"라는 뜻의 번호일 뿐이며, 특정 종을 가리키지 않는다.
// 기본값이 필요한 자리(Species{})와 테스트 외에는 쓰지 않는다.
enum class Species { Fire = 0, Earth = 1, Wind = 2, Water = 3 };
constexpr int kBuiltinSpeciesCount = 0;

// 현재 레지스트리에 등록된 종의 수(내장 + 폴더에서 추가된 종)
int speciesCount();

inline constexpr const char* kProcessionLayers[] = {
    "procession_ritual", "procession_soul_eruption", "procession_spectral_crown",
    "procession_army_emergence", "procession_spirit_charge", "procession_final_impact", "procession_aftermath"
};

enum class SkillKind {
    // ---- 액티브: 장착 칸(최대 kSkillSlots개)에 넣어야 쓸 수 있다 ----
    Projectile,  // 가장 가까운 적에게 유도탄
    Burst,       // 자기 주변 원형 피해(extra = 기절 시간)
    Line,        // 대상 방향 직선 피해
    Dash,        // 대상 방향 돌진 피해 + 무적
    Heal,        // 최대 체력 비율 회복(power)
    Shield,      // 받는 피해 감소(power = 감소 비율)
    Haste,       // 이동 속도(power)·회피 확률(extra) 증가
    Zone,        // 대상 위치에 지속 피해 장판(power = 초당 공격력 배율)
    // ---- 패시브(v1.4): 칸을 쓰지 않고 해제되면 항상 적용된다 ----
    PassiveStat,    // 능력치 상시 보정(stat = 어떤 능력치, power = 보정량)
    PassiveOnKill,  // 적을 쓰러뜨릴 때 최대 체력의 power 비율만큼 회복
    Procession,     // 망령룡 전용 수동 궁극기
};

// 액티브와 패시브를 가르는 유일한 기준. 스킬 종류를 새로 추가할 때 여기만 고치면
// 장착창·입력·자동 전투가 모두 따라온다.
bool skillIsPassive(SkillKind k);

// dragon.txt의 kind= 값과 SkillKind를 서로 바꾼다. 모르는 이름이면 false.
bool skillKindFromName(const std::string& name, SkillKind& out);
const char* skillKindName(SkillKind k);

// PassiveStat이 올려 줄 능력치. dragon.txt의 stat= 값과 짝이다.
enum class StatKind { Hp, Atk, Def, Speed, Dodge, Regen };
bool statKindFromName(const std::string& name, StatKind& out);
const char* statKindName(StatKind k);  // "hp", "atk", ...
const char* statKindLabel(StatKind k);  // "최대 체력", "공격력", ...

// 캐릭터 애니메이션 상태. assets/dragons/<키>/<상태 이름>.png 시트와 짝이다.
// Fly는 바탕화면에서 스스로 날아다닐 때만 쓴다(전투에는 쓰지 않는다).
// Rest는 오래 쉴 때만 쓰는 모션(엎드리기 등)이다. 시트가 없으면 idle로 대체된다.
enum class AnimState { Idle = 0, Move = 1, Attack = 2, Hit = 3, Death = 4, Fly = 5, Rest = 6 };
constexpr int kAnimStateCount = 7;
const char* animStateName(AnimState s);  // "idle", "move", ...

struct AnimDef {
    float fps = 8.f;
    bool loop = true;
    int frameW = 0;  // 시트의 칸 너비. 0이면 정사각(이미지 높이와 같음)으로 본다.
    // 이 상태만 그릴 때 크기를 곱한다. 날개를 크게 펼치는 비행처럼, 칸 안에 넣느라
    // 몸이 작게 들어간 시트를 다른 상태와 같은 몸 크기로 보이게 할 때 쓴다.
    // 바닥 기준선은 유지된다(위아래로 늘어나지 않고 발 위치가 고정된다).
    float renderScale = 1.f;
};

struct SkillDef {
    std::string name;
    std::string desc;
    // 0이면 "자동" — 등록할 때 비어 있는 가장 낮은 레벨이 한 레벨에 하나씩 배정된다.
    // 1 이상이면 그 레벨에서 해제된다. 등록이 끝난 뒤에는 항상 1 이상이다.
    int unlockLevel = 0;
    float cooldown = 4.f;
    SkillKind kind = SkillKind::Projectile;
    float power = 1.5f;
    float radius = 0.f;    // Burst/Zone 반경, Line 반폭
    float range = 350.f;   // 사거리, Line/Dash 길이
    float duration = 0.f;  // Shield/Haste/Zone 지속 시간
    float extra = 0.f;
    // ---- 아래는 v1.4에서 더했다. 위 순서는 기존 코드가 그대로 쓰므로 바꾸지 않는다 ----
    StatKind stat = StatKind::Atk;  // PassiveStat 전용
    // 이 스킬을 쓸 때 재생할 전용 본체 모션. assets/dragons/<키>/skill_<motion>.png
    // 비어 있으면 공용 attack 시트를 쓴다(예전과 같다).
    std::string motion;
    AnimDef motionAnim{12.f, false, 0, 1.f};
    // ---- 아래는 v1.6에서 더했다 ----
    // 스킬 효과(FX) 스프라이트. assets/dragons/<키>/fx_<fx>.png
    // 비어 있으면 지금까지처럼 도형(FxType)만 그린다 — 기존 스킬은 아무것도 달라지지 않는다.
    std::string fx;
    // 발사체가 **맞는 순간** 쓸 효과 시트(fx_<이름>.png). Projectile 전용이다(2026-11-17).
    // fx= 는 날아가는 동안 발사체를 따라다니는 그림이고, 이쪽은 터질 때 한 번 뜬다.
    std::string fxImpact;
    // 효과 그림의 투명도 배수. 1 이면 지금까지와 같고, 낮출수록 본체가 비쳐 보인다.
    // 단계별 투명도(발동/지속/잔류)에 곱해진다 — 단계 흐름은 그대로 두고 전체만 옅게 한다.
    // 도형으로 그리는 효과(시트가 없는 스킬)에는 적용되지 않는다.
    float fxAlpha = 1.f;
    // ---- 아래는 2026-11-17에 더했다 ----
    // 켜면 이 스킬의 판정 반경(radius)이 보기 크기(charScale)를 따라 커진다.
    // 사거리(range)는 켜도 커지지 않는다 — 1000%에서 사거리가 10배가 되면 균형이 깨진다.
    // 광역(Burst/Zone)과 효과 그림이 넓은 스킬에만 켠다.
    bool scaleWithSize = false;
    bool fxInFront = true;  // 본체 위에 그리는가. false면 본체 뒤
    float fxScale = 1.f;    // 효과 크기 배율
    // 보정값은 "100% 기준 px"다 — 그릴 때 charScale 을 곱한다(보기 크기 설정과 같이 커진다).
    float fxOffsetX = 0.f;  // 본체 기준 가로 보정(100% 기준 px, 바라보는 쪽이 양수)
    float fxOffsetY = 0.f;  // 본체 기준 세로 보정(100% 기준 px, 위가 음수)
    // 발동 → 지속 → 잔류 세 단계의 길이(초). 0이면 cfg::kFx... 기본값을 쓴다.
    float fxWindup = 0.f;
    float fxHold = 0.f;
    float fxTail = 0.f;
    float fxDelay = 0.f;  // Seconds from the triggering event to the visual effect.
    bool soulfire = false;
    float soulfireTakeoff = 0.4f;
    float soulfireLanding = 0.4f;
    float soulfireAngle = 55.f;
    float soulfireWidth = 1.32f;
    float soulfireMouthX = 764.f / 1024.f;
    float soulfireMouthY = 586.f / 1024.f;
    float soulfireImpactScale = 1.55f;
    float soulfireFlameScale = 0.22f;
    bool fxZoneSpriteOnly = false;  // Render the zone's sheet on the zone's own clock.
    bool grantsSoul = false;  // Opt-in for Revenant's on-kill passive.
    // 궁극기(2026-11-20): 장착 칸 4개와 따로 쓰는 전용 칸. 키 5·상태창 아이콘 클릭으로 쓰고, 자동 모드는 AI 가 쓴다.
    // ultimate_resource=souls 면 영혼 10개를 소모한다(게이지에 영혼 표시). 없으면 재사용 대기만 있다(게이지에 쿨타임 표시).
    bool ultimate = false;
    bool ultimateUsesSouls = false;
    AnimDef fxAnim{12.f, false, 0, 1.f};
    bool furnaceBurst = false;
    bool coreOverheat = false;
    bool heatBladeStorm = false;
    bool heatBladeRepeatTargets = false;
    float fxImpactFps = 0.f;  // Zero inherits the projectile sheet's FPS.
    float fxPlaybackFps(const std::string& key) const {
        return key == fxImpact && key != fx && fxImpactFps > 0.f ? fxImpactFps : fxAnim.fps;
    }
    std::string layeredBreath;  // Optional five-sheet breath package, relative to the dragon folder.
    float breathInputLock = 0.f;
    bool sustainedBreath = false;
    bool breathDeferHit = true;  // Compatibility with Bonestorm's impact at LOOP entry.
    bool usesFurnaceBreath() const { return layeredBreath == "furnace_breath_v3_1"; }
    bool usesUmbralBreath() const { return layeredBreath == "umbral_vortex_v2"; }
    // ---- 2026-11-18: 타격 프레임 (그림자룡 가르기로 처음 검증) ----
    // 피해를 시전 순간이 아니라 본체 모션의 정해진 프레임에 준다. 효과도 정해진 프레임에
    // 본체의 정해진 자리(앞발 등)에 붙여 띄운다. 지금은 Line(숨결 제외)에서만 쓴다.
    bool spectralCleave = false;
    bool boneShot = false;
    int hitFrame = 0;      // hit_frame= 몇 번째(1부터) 모션 프레임에 피해를 주는가. 0이면 예전처럼 즉시
    int fxFrame = 0;       // fx_frame= 효과를 띄우는 프레임. 0이면 hit_frame 과 같다
    int motionFrames = 0;  // motion_frames= 모션 프레임 수. 끝까지 자세를 유지하는 데 쓴다(0이면 기본 유지 시간)
    bool hasFxAnchor = false;
    float fxAnchorX = 0.f, fxAnchorY = 0.f;    // fx_anchor= fx_frame 에서 효과를 붙일 본체 칸 안 좌표(px, 오른쪽 지향)
    float fxOriginX = -1.f, fxOriginY = -1.f;  // fx_origin= 효과 칸 안에서 fx_anchor 에 맞출 점(px). 음수면 칸 가운데
    int processionShots = 3;
    float processionInterval = 0.4f;
    float processionSpeed = 620.f;
    float processionLifetime = 2.f;
    bool detonatesCurse = false;  // 직접 명중 시 최대 중첩 소비
    bool appliesCurse = false;  // Projectile 명중 또는 Zone 피해 판정에서만 사용한다.
    bool usesTimedHit() const { return hitFrame > 0 && !motion.empty() && layeredBreath.empty() && kind == SkillKind::Line; }
};

struct SpeciesDef {
    Species id = Species::Fire;
    std::string key;   // 저장 파일·폴더·이미지 이름에 쓰는 영문 키
    std::string name;
    std::string role;
    std::string desc;
    uint32_t color = 0xFF9AA2B4;  // 0xAARRGGBB
    float baseHp = 110.f, baseAtk = 11.f, baseDef = 4.f, baseSpeed = 170.f, baseDodge = 0.30f, baseRegen = 0.f;
    float hpGrowth = 15.f, atkGrowth = 2.4f, defGrowth = 0.8f, dodgeGrowth = 0.f, regenGrowth = 0.f;
    // v1.4: 개수 제한을 없앴다. dragon.txt에 [skill.1] [skill.2] ... 를 적은 만큼 늘어난다.
    // 해제 레벨은 registerOrUpdateSpecies에서 "한 레벨에 하나씩" 규칙으로 채워진다.
    std::vector<SkillDef> skills;
    AnimDef anims[kAnimStateCount];  // 상태별 재생 정보(시트가 없으면 무시된다)
    bool flipAllowed = true;         // false면 왼쪽을 볼 때도 좌우 반전하지 않는다
    // v0.9: 이 드래곤을 고르면 쓰는 UI 스킨(assets/ui/<키>/). 비어 있으면 default.
    // 스킨에 없는 조각은 default 에서 가져오므로, 바꾸고 싶은 조각만 넣으면 된다.
    std::string uiSkin;
};

struct DragonStats {
    float maxHp;
    float atk;
    float def;
    float speed;
    float dodge;   // 스킬 수동·자동 모드에서 공격마다 판정하는 회피 확률
    float regen;   // 전투 중에도 적용되는 초당 회복량
};

// 주의: 반환 참조는 종 등록(registerOrUpdateSpecies)이 더 일어나지 않는 동안에만 유효하다.
// 등록은 시작 시(폴더 로딩·저장 불러오기)에만 하고, 게임 중에는 하지 않는다.
const SpeciesDef& speciesDef(Species s);
bool speciesFromKey(const std::string& key, Species& out);

// 같은 키가 있으면 그 종을 덮어쓰고, 없으면 새 종으로 추가한다. 등록된 id를 돌려준다.
// 등록할 때 해제 레벨이 비어 있는(unlockLevel == 0) 스킬에 레벨을 배정한다.
Species registerOrUpdateSpecies(const SpeciesDef& def);

// "레벨마다 하나씩" 규칙. level= 이 적힌 스킬은 그 값을 지키고, 비어 있는 스킬은
// 아직 아무도 쓰지 않은 가장 낮은 레벨을 목록 순서대로 하나씩 가져간다.
// registerOrUpdateSpecies가 항상 부르므로 보통은 직접 부를 일이 없다(테스트용으로 공개).
void assignUnlockLevels(SpeciesDef& def);

// 그 레벨에서 쓸 수 있는가. 범위 밖 번호는 false.
bool skillUnlockedAt(const SpeciesDef& def, int skillIndex, int level);
// 그 레벨에서 해제된 액티브 스킬 번호를 목록 순서대로 돌려준다(장착창·자동 채우기용).
std::vector<int> unlockedActiveSkills(const SpeciesDef& def, int level);

// 키가 레지스트리에 없으면 그 키의 자리 표시 종(이름=키, 기본 수치)을 등록한다.
// 저장 파일에 폴더가 지워진 종의 개체가 남아 있어도 다른 종으로 바뀌지 않게 한다.
Species ensureSpecies(const std::string& key);

DragonStats statsFor(Species s, int level);

// 시트 애니메이션의 현재 프레임. loop면 반복, 아니면 마지막 프레임에서 멈춘다.
int animFrame(float t, float fps, int frames, bool loop);

}  // namespace dt
