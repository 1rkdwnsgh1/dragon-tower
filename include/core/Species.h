// 드래곤 종류, 능력치, 스킬 정의
#pragma once

#include <cstdint>
#include <string>

namespace dt {

enum class Species { Fire = 0, Earth = 1, Wind = 2, Water = 3 };
constexpr int kSpeciesCount = 4;

enum class SkillKind {
    Projectile,  // 가장 가까운 적에게 유도탄
    Burst,       // 자기 주변 원형 피해(extra = 기절 시간)
    Line,        // 대상 방향 직선 피해
    Dash,        // 대상 방향 돌진 피해 + 무적
    Heal,        // 최대 체력 비율 회복(power)
    Shield,      // 받는 피해 감소(power = 감소 비율)
    Haste,       // 이동 속도(power)·회피 확률(extra) 증가
    Zone,        // 대상 위치에 지속 피해 장판(power = 초당 공격력 배율)
};

struct SkillDef {
    const char* name;
    const char* desc;
    int unlockLevel;
    float cooldown;
    SkillKind kind;
    float power;
    float radius;    // Burst/Zone 반경, Line 반폭
    float range;     // 사거리, Line/Dash 길이
    float duration;  // Shield/Haste/Zone 지속 시간
    float extra;
};

struct SpeciesDef {
    Species id;
    const char* key;   // 저장 파일·이미지 파일 이름에 쓰는 영문 키
    const char* name;
    const char* role;
    const char* desc;
    uint32_t color;    // 0xAARRGGBB
    float baseHp, baseAtk, baseDef, baseSpeed, baseDodge, baseRegen;
    float hpGrowth, atkGrowth, defGrowth, dodgeGrowth, regenGrowth;
    SkillDef skills[3];
};

struct DragonStats {
    float maxHp;
    float atk;
    float def;
    float speed;
    float dodge;   // 스킬 수동·자동 모드에서 공격마다 판정하는 회피 확률
    float regen;   // 전투 중에도 적용되는 초당 회복량
};

const SpeciesDef& speciesDef(Species s);
bool speciesFromKey(const std::string& key, Species& out);
DragonStats statsFor(Species s, int level);

}  // namespace dt
