#include "core/Species.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <set>

namespace dt {

namespace {

// 이름과 수치는 확정이 아니라 플레이 감각 차이를 시험하기 위한 임시안이다.
SpeciesDef builtinSpecies(int i) {
    SpeciesDef d;
    switch (i) {
        case 0:
            d = {Species::Fire, "fire", "화염 드래곤", "공격", "강한 공격 스킬로 적을 빠르게 쓰러뜨린다.",
                 0xFFE8552B,
                 100.f, 14.f, 3.f, 170.f, 0.30f, 0.0f,
                 14.f, 3.0f, 0.6f, 0.0f, 0.0f,
                 {
                     {"화염구", "가장 가까운 적에게 큰 불덩이를 날린다.", 1, 4.f, SkillKind::Projectile, 2.2f, 0.f, 380.f, 0.f, 0.f},
                     {"화염 폭발", "주변의 적에게 불길을 터뜨린다.", 3, 7.f, SkillKind::Burst, 1.8f, 110.f, 0.f, 0.f, 0.f},
                     {"불꽃 숨결", "앞쪽 일직선에 강한 불꽃을 내뿜는다.", 6, 10.f, SkillKind::Line, 3.0f, 36.f, 330.f, 0.f, 0.f},
                 }};
            break;
        case 1:
            d = {Species::Earth, "earth", "대지 드래곤", "방어", "단단한 몸과 방어 스킬로 오래 버틴다.",
                 0xFF9A7B4F,
                 140.f, 10.f, 7.f, 140.f, 0.20f, 0.0f,
                 20.f, 2.0f, 1.2f, 0.0f, 0.0f,
                 {
                     {"바위 던지기", "가장 가까운 적에게 바위를 던진다.", 1, 4.f, SkillKind::Projectile, 1.8f, 0.f, 360.f, 0.f, 0.f},
                     {"대지의 방패", "잠시 받는 피해를 절반으로 줄인다.", 3, 10.f, SkillKind::Shield, 0.5f, 0.f, 0.f, 4.f, 0.f},
                     {"지진", "주변 적에게 피해를 주고 잠시 기절시킨다.", 6, 9.f, SkillKind::Burst, 1.5f, 140.f, 0.f, 0.f, 1.5f},
                 }};
            break;
        case 2:
            d = {Species::Wind, "wind", "바람 드래곤", "기동", "빠른 이동과 돌진으로 공격을 흘려낸다.",
                 0xFF4FC3A1,
                 95.f, 11.f, 3.f, 230.f, 0.45f, 0.0f,
                 12.f, 2.4f, 0.6f, 0.005f, 0.0f,
                 {
                     {"질풍 돌진", "적을 향해 돌진하며 지나간 적에게 피해를 준다. 돌진 중에는 무적이다.", 1, 5.f, SkillKind::Dash, 1.6f, 40.f, 220.f, 0.f, 0.f},
                     {"회오리", "적 위치에 회오리를 만들어 계속 피해를 준다.", 3, 8.f, SkillKind::Zone, 0.9f, 90.f, 360.f, 3.f, 0.f},
                     {"순풍", "잠시 이동 속도와 회피 확률이 오른다.", 6, 12.f, SkillKind::Haste, 0.4f, 0.f, 0.f, 5.f, 0.25f},
                 }};
            break;
        default:
            d = {Species::Water, "water", "물 드래곤", "회복·지속", "스스로 회복하며 지속 피해로 버틴다.",
                 0xFF3D8BE0,
                 110.f, 10.f, 4.f, 165.f, 0.30f, 1.5f,
                 15.f, 2.2f, 0.8f, 0.0f, 0.15f,
                 {
                     {"물대포", "가장 가까운 적에게 물줄기를 쏜다.", 1, 4.f, SkillKind::Projectile, 1.8f, 0.f, 380.f, 0.f, 0.f},
                     {"치유의 물", "최대 체력의 30%를 회복한다.", 3, 10.f, SkillKind::Heal, 0.30f, 0.f, 0.f, 0.f, 0.f},
                     {"소용돌이", "적 위치에 소용돌이를 만들어 계속 피해를 준다.", 6, 9.f, SkillKind::Zone, 1.0f, 100.f, 360.f, 4.f, 0.f},
                 }};
            break;
    }
    // 상태별 기본 재생 정보. dragon.txt의 [anim.*]이 덮어쓸 수 있다.
    d.anims[static_cast<int>(AnimState::Idle)] = {8.f, true, 0};
    d.anims[static_cast<int>(AnimState::Move)] = {10.f, true, 0};
    d.anims[static_cast<int>(AnimState::Attack)] = {12.f, false, 0};
    d.anims[static_cast<int>(AnimState::Hit)] = {12.f, false, 0};
    d.anims[static_cast<int>(AnimState::Death)] = {10.f, false, 0};
    d.anims[static_cast<int>(AnimState::Fly)] = {10.f, true, 0};
    d.anims[static_cast<int>(AnimState::Rest)] = {6.f, true, 0};  // 쉬는 모션은 느리게
    return d;
}

// deque라서 추가해도 기존 원소 참조가 유지된다(speciesDef가 참조를 돌려주므로 중요).
std::deque<SpeciesDef>& registry() {
    static std::deque<SpeciesDef> r = [] {
        std::deque<SpeciesDef> v;
        for (int i = 0; i < kBuiltinSpeciesCount; ++i) v.push_back(builtinSpecies(i));
        return v;
    }();
    return r;
}

}  // namespace

int speciesCount() { return static_cast<int>(registry().size()); }

const char* animStateName(AnimState s) {
    switch (s) {
        case AnimState::Idle: return "idle";
        case AnimState::Move: return "move";
        case AnimState::Attack: return "attack";
        case AnimState::Hit: return "hit";
        case AnimState::Death: return "death";
        case AnimState::Fly: return "fly";
        case AnimState::Rest: return "rest";
    }
    return "idle";
}

bool skillIsPassive(SkillKind k) {
    return k == SkillKind::PassiveStat || k == SkillKind::PassiveOnKill;
}

namespace {
std::string lowerCopy(const std::string& s) {
    std::string n;
    n.reserve(s.size());
    for (char c : s) n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return n;
}
}  // namespace

bool statKindFromName(const std::string& name, StatKind& out) {
    std::string n = lowerCopy(name);
    if (n == "hp") out = StatKind::Hp;
    else if (n == "atk") out = StatKind::Atk;
    else if (n == "def") out = StatKind::Def;
    else if (n == "speed") out = StatKind::Speed;
    else if (n == "dodge") out = StatKind::Dodge;
    else if (n == "regen") out = StatKind::Regen;
    else return false;
    return true;
}

const char* statKindName(StatKind k) {
    switch (k) {
        case StatKind::Hp: return "hp";
        case StatKind::Atk: return "atk";
        case StatKind::Def: return "def";
        case StatKind::Speed: return "speed";
        case StatKind::Dodge: return "dodge";
        case StatKind::Regen: return "regen";
    }
    return "atk";
}

const char* statKindLabel(StatKind k) {
    switch (k) {
        case StatKind::Hp: return "최대 체력";
        case StatKind::Atk: return "공격력";
        case StatKind::Def: return "방어력";
        case StatKind::Speed: return "이동 속도";
        case StatKind::Dodge: return "회피 확률";
        case StatKind::Regen: return "초당 회복";
    }
    return "공격력";
}

void assignUnlockLevels(SpeciesDef& def) {
    // 사람이 적어 둔 레벨을 먼저 잡아 두고, 빈 스킬은 남은 레벨을 순서대로 가져간다.
    std::set<int> taken;
    for (SkillDef& s : def.skills) {
        if (s.unlockLevel > 0) taken.insert(s.unlockLevel);
    }
    int next = 1;
    for (SkillDef& s : def.skills) {
        if (s.unlockLevel > 0) continue;
        while (taken.count(next) != 0) ++next;
        s.unlockLevel = next;
        taken.insert(next);
        ++next;
    }
}

bool skillUnlockedAt(const SpeciesDef& def, int skillIndex, int level) {
    if (skillIndex < 0 || skillIndex >= static_cast<int>(def.skills.size())) return false;
    return level >= def.skills[static_cast<size_t>(skillIndex)].unlockLevel;
}

std::vector<int> unlockedActiveSkills(const SpeciesDef& def, int level) {
    std::vector<int> out;
    for (size_t i = 0; i < def.skills.size(); ++i) {
        const SkillDef& s = def.skills[i];
        if (skillIsPassive(s.kind)) continue;
        if (level < s.unlockLevel) continue;
        out.push_back(static_cast<int>(i));
    }
    return out;
}

bool skillKindFromName(const std::string& name, SkillKind& out) {
    std::string n = lowerCopy(name);
    if (n == "projectile") out = SkillKind::Projectile;
    else if (n == "burst") out = SkillKind::Burst;
    else if (n == "line") out = SkillKind::Line;
    else if (n == "dash") out = SkillKind::Dash;
    else if (n == "heal") out = SkillKind::Heal;
    else if (n == "shield") out = SkillKind::Shield;
    else if (n == "haste") out = SkillKind::Haste;
    else if (n == "zone") out = SkillKind::Zone;
    else if (n == "passivestat") out = SkillKind::PassiveStat;
    else if (n == "passiveonkill") out = SkillKind::PassiveOnKill;
    else return false;
    return true;
}

const char* skillKindName(SkillKind k) {
    switch (k) {
        case SkillKind::Projectile: return "Projectile";
        case SkillKind::Burst: return "Burst";
        case SkillKind::Line: return "Line";
        case SkillKind::Dash: return "Dash";
        case SkillKind::Heal: return "Heal";
        case SkillKind::Shield: return "Shield";
        case SkillKind::Haste: return "Haste";
        case SkillKind::Zone: return "Zone";
        case SkillKind::PassiveStat: return "PassiveStat";
        case SkillKind::PassiveOnKill: return "PassiveOnKill";
    }
    return "Projectile";
}

const SpeciesDef& speciesDef(Species s) {
    auto& r = registry();
    int i = std::clamp(static_cast<int>(s), 0, static_cast<int>(r.size()) - 1);
    return r[static_cast<size_t>(i)];
}

bool speciesFromKey(const std::string& key, Species& out) {
    for (const auto& def : registry()) {
        if (key == def.key) {
            out = def.id;
            return true;
        }
    }
    return false;
}

Species registerOrUpdateSpecies(const SpeciesDef& def) {
    SpeciesDef d = def;
    assignUnlockLevels(d);  // 여기서만 채운다. 등록된 종의 unlockLevel은 항상 1 이상이다.
    auto& r = registry();
    for (auto& existing : r) {
        if (existing.key == d.key) {
            Species id = existing.id;
            existing = d;
            existing.id = id;  // id와 키는 바뀌지 않는다
            return id;
        }
    }
    d.id = static_cast<Species>(r.size());
    r.push_back(d);
    return d.id;
}

Species ensureSpecies(const std::string& key) {
    Species s;
    if (speciesFromKey(key, s)) return s;
    SpeciesDef d;  // 기본 수치의 자리 표시 종
    d.key = key;
    d.name = key;
    d.role = "?";
    d.desc = "데이터 파일이 없는 종입니다. assets/dragons/" + key + "/dragon.txt를 확인하세요.";
    d.skills = {
        {"스킬 1", "가장 가까운 적을 공격한다.", 1, 4.f, SkillKind::Projectile, 1.9f, 0.f, 370.f, 0.f, 0.f},
        {"스킬 2", "주변에 피해를 준다.", 3, 8.f, SkillKind::Burst, 1.6f, 110.f, 0.f, 0.f, 0.f},
        {"스킬 3", "일직선으로 피해를 준다.", 6, 10.f, SkillKind::Line, 2.6f, 36.f, 320.f, 0.f, 0.f},
    };
    return registerOrUpdateSpecies(d);
}

DragonStats statsFor(Species s, int level) {
    const SpeciesDef& d = speciesDef(s);
    float n = static_cast<float>(std::max(1, level) - 1);
    DragonStats st{};
    st.maxHp = d.baseHp + d.hpGrowth * n;
    st.atk = d.baseAtk + d.atkGrowth * n;
    st.def = d.baseDef + d.defGrowth * n;
    st.speed = d.baseSpeed;
    st.dodge = d.baseDodge + d.dodgeGrowth * n;
    st.regen = d.baseRegen + d.regenGrowth * n;

    // v1.4 패시브. 해제된 PassiveStat 스킬이 능력치를 올린다. 장착 칸과 무관하게 항상 적용된다.
    // hp/atk/def/speed 는 비율(power=0.1 -> +10%), dodge/regen 은 더하기(절대값)다.
    for (const SkillDef& sk : d.skills) {
        if (sk.kind != SkillKind::PassiveStat) continue;
        if (std::max(1, level) < sk.unlockLevel) continue;
        switch (sk.stat) {
            case StatKind::Hp: st.maxHp *= (1.f + sk.power); break;
            case StatKind::Atk: st.atk *= (1.f + sk.power); break;
            case StatKind::Def: st.def *= (1.f + sk.power); break;
            case StatKind::Speed: st.speed *= (1.f + sk.power); break;
            case StatKind::Dodge: st.dodge += sk.power; break;
            case StatKind::Regen: st.regen += sk.power; break;
        }
    }
    st.maxHp = std::max(1.f, st.maxHp);
    st.dodge = std::clamp(st.dodge, 0.f, 0.75f);  // 회피 상한은 패시브로도 넘지 못한다
    st.regen = std::max(0.f, st.regen);
    return st;
}

int animFrame(float t, float fps, int frames, bool loop) {
    if (frames <= 1 || fps <= 0.f) return 0;
    int f = static_cast<int>(std::floor(std::max(0.f, t) * fps));
    if (loop) return f % frames;
    return std::min(f, frames - 1);
}

}  // namespace dt
