#include "core/Species.h"

#include <algorithm>

namespace dt {

namespace {

// 이름과 수치는 확정이 아니라 플레이 감각 차이를 시험하기 위한 임시안이다.
const SpeciesDef kSpecies[kSpeciesCount] = {
    {Species::Fire, "fire", "화염 드래곤", "공격", "강한 공격 스킬로 적을 빠르게 쓰러뜨린다.",
     0xFFE8552B,
     100.f, 14.f, 3.f, 170.f, 0.30f, 0.0f,
     14.f, 3.0f, 0.6f, 0.0f, 0.0f,
     {
         {"화염구", "가장 가까운 적에게 큰 불덩이를 날린다.", 1, 4.f, SkillKind::Projectile, 2.2f, 0.f, 380.f, 0.f, 0.f},
         {"화염 폭발", "주변의 적에게 불길을 터뜨린다.", 3, 7.f, SkillKind::Burst, 1.8f, 110.f, 0.f, 0.f, 0.f},
         {"불꽃 숨결", "앞쪽 일직선에 강한 불꽃을 내뿜는다.", 6, 10.f, SkillKind::Line, 3.0f, 36.f, 330.f, 0.f, 0.f},
     }},
    {Species::Earth, "earth", "대지 드래곤", "방어", "단단한 몸과 방어 스킬로 오래 버틴다.",
     0xFF9A7B4F,
     140.f, 10.f, 7.f, 140.f, 0.20f, 0.0f,
     20.f, 2.0f, 1.2f, 0.0f, 0.0f,
     {
         {"바위 던지기", "가장 가까운 적에게 바위를 던진다.", 1, 4.f, SkillKind::Projectile, 1.8f, 0.f, 360.f, 0.f, 0.f},
         {"대지의 방패", "잠시 받는 피해를 절반으로 줄인다.", 3, 10.f, SkillKind::Shield, 0.5f, 0.f, 0.f, 4.f, 0.f},
         {"지진", "주변 적에게 피해를 주고 잠시 기절시킨다.", 6, 9.f, SkillKind::Burst, 1.5f, 140.f, 0.f, 0.f, 1.5f},
     }},
    {Species::Wind, "wind", "바람 드래곤", "기동", "빠른 이동과 돌진으로 공격을 흘려낸다.",
     0xFF4FC3A1,
     95.f, 11.f, 3.f, 230.f, 0.45f, 0.0f,
     12.f, 2.4f, 0.6f, 0.005f, 0.0f,
     {
         {"질풍 돌진", "적을 향해 돌진하며 지나간 적에게 피해를 준다. 돌진 중에는 무적이다.", 1, 5.f, SkillKind::Dash, 1.6f, 40.f, 220.f, 0.f, 0.f},
         {"회오리", "적 위치에 회오리를 만들어 계속 피해를 준다.", 3, 8.f, SkillKind::Zone, 0.9f, 90.f, 360.f, 3.f, 0.f},
         {"순풍", "잠시 이동 속도와 회피 확률이 오른다.", 6, 12.f, SkillKind::Haste, 0.4f, 0.f, 0.f, 5.f, 0.25f},
     }},
    {Species::Water, "water", "물 드래곤", "회복·지속", "스스로 회복하며 지속 피해로 버틴다.",
     0xFF3D8BE0,
     110.f, 10.f, 4.f, 165.f, 0.30f, 1.5f,
     15.f, 2.2f, 0.8f, 0.0f, 0.15f,
     {
         {"물대포", "가장 가까운 적에게 물줄기를 쏜다.", 1, 4.f, SkillKind::Projectile, 1.8f, 0.f, 380.f, 0.f, 0.f},
         {"치유의 물", "최대 체력의 30%를 회복한다.", 3, 10.f, SkillKind::Heal, 0.30f, 0.f, 0.f, 0.f, 0.f},
         {"소용돌이", "적 위치에 소용돌이를 만들어 계속 피해를 준다.", 6, 9.f, SkillKind::Zone, 1.0f, 100.f, 360.f, 4.f, 0.f},
     }},
};

}  // namespace

const SpeciesDef& speciesDef(Species s) {
    int i = std::clamp(static_cast<int>(s), 0, kSpeciesCount - 1);
    return kSpecies[i];
}

bool speciesFromKey(const std::string& key, Species& out) {
    for (const auto& def : kSpecies) {
        if (key == def.key) {
            out = def.id;
            return true;
        }
    }
    return false;
}

DragonStats statsFor(Species s, int level) {
    const SpeciesDef& d = speciesDef(s);
    float n = static_cast<float>(std::max(1, level) - 1);
    DragonStats st{};
    st.maxHp = d.baseHp + d.hpGrowth * n;
    st.atk = d.baseAtk + d.atkGrowth * n;
    st.def = d.baseDef + d.defGrowth * n;
    st.speed = d.baseSpeed;
    st.dodge = std::min(0.75f, d.baseDodge + d.dodgeGrowth * n);
    st.regen = d.baseRegen + d.regenGrowth * n;
    return st;
}

}  // namespace dt
