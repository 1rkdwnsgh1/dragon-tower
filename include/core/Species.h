// 드래곤 종류, 능력치, 스킬, 애니메이션 정의.
// v0.2: 종 목록이 고정 배열에서 레지스트리로 바뀌었다. 내장 4종으로 시작하고,
// assets/dragons/<키>/dragon.txt(DragonData.h)가 종을 덮어쓰거나 추가할 수 있다.
#pragma once

#include <cstdint>
#include <string>

namespace dt {

// 내장 4종. 레지스트리에 종이 추가되면 4 이상의 값도 유효한 Species가 된다.
enum class Species { Fire = 0, Earth = 1, Wind = 2, Water = 3 };
constexpr int kBuiltinSpeciesCount = 4;

// 현재 레지스트리에 등록된 종의 수(내장 + 폴더에서 추가된 종)
int speciesCount();

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

// dragon.txt의 kind= 값과 SkillKind를 서로 바꾼다. 모르는 이름이면 false.
bool skillKindFromName(const std::string& name, SkillKind& out);
const char* skillKindName(SkillKind k);

struct SkillDef {
    std::string name;
    std::string desc;
    int unlockLevel = 1;
    float cooldown = 4.f;
    SkillKind kind = SkillKind::Projectile;
    float power = 1.5f;
    float radius = 0.f;    // Burst/Zone 반경, Line 반폭
    float range = 350.f;   // 사거리, Line/Dash 길이
    float duration = 0.f;  // Shield/Haste/Zone 지속 시간
    float extra = 0.f;
};

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

struct SpeciesDef {
    Species id = Species::Fire;
    std::string key;   // 저장 파일·폴더·이미지 이름에 쓰는 영문 키
    std::string name;
    std::string role;
    std::string desc;
    uint32_t color = 0xFF9AA2B4;  // 0xAARRGGBB
    float baseHp = 110.f, baseAtk = 11.f, baseDef = 4.f, baseSpeed = 170.f, baseDodge = 0.30f, baseRegen = 0.f;
    float hpGrowth = 15.f, atkGrowth = 2.4f, defGrowth = 0.8f, dodgeGrowth = 0.f, regenGrowth = 0.f;
    SkillDef skills[3];
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
Species registerOrUpdateSpecies(const SpeciesDef& def);

// 키가 레지스트리에 없으면 그 키의 자리 표시 종(이름=키, 기본 수치)을 등록한다.
// 저장 파일에 폴더가 지워진 종의 개체가 남아 있어도 다른 종으로 바뀌지 않게 한다.
Species ensureSpecies(const std::string& key);

DragonStats statsFor(Species s, int level);

// 시트 애니메이션의 현재 프레임. loop면 반복, 아니면 마지막 프레임에서 멈춘다.
int animFrame(float t, float fps, int frames, bool loop);

}  // namespace dt
