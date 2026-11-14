// 레벨·경험치와 자동 사냥 경험치 상한
#pragma once

#include "core/Species.h"

namespace dt {

// 저장되는 드래곤 개체 정보
struct DragonRecord {
    int id = 0;
    Species species = Species::Fire;
    bool alive = true;
    int level = 1;
    int exp = 0;       // 현재 레벨에서 쌓은 경험치
    int autoExp = 0;   // 자동 사냥으로 얻은 누적 경험치
    float hp = -1.f;   // 음수면 최대 체력으로 시작
    // v1.4 장착한 액티브 스킬. 값은 그 종의 스킬 목록 번호, -1은 빈 칸이다.
    // 개체마다 레벨이 달라 해제된 스킬도 다르므로 종이 아니라 개체에 둔다.
    // 패시브는 여기 들어가지 않는다(해제되면 항상 적용).
    std::array<int, kSkillSlots> slots{-1, -1, -1, -1};
};

// 빈 칸에 새로 해제된 액티브를 목록 순서대로 채운다. 사람이 이미 넣어 둔 칸은 건드리지 않는다.
// 레벨업·드래곤 교체·옛 저장 파일 불러오기에서 부른다.
void fillEmptySkillSlots(DragonRecord& d);
// 저장 파일에서 읽은 값이 그 종·레벨에 맞는지 확인하고 틀린 것은 비운다(중복·미해제·패시브).
void sanitizeSkillSlots(DragonRecord& d);

int expToNext(int level);         // level -> level+1에 필요한 경험치
int totalExpToReach(int level);   // 레벨 1, 경험치 0에서 해당 레벨까지 필요한 총 경험치
// 상한이 꺼져 있을 때 autoExpCap()이 돌려주는 값. 사실상 무한이다.
constexpr int kNoAutoExpCap = 1000000000;
int autoExpCap();                 // 개체별 자동 사냥 누적 경험치 상한
// 테스트 전용. 상한을 켠 상태의 계산을 검증하기 위해 값을 덮어쓴다.
// -1을 주면 다시 설정값(cfg::kAutoExpCapEnabled)을 따른다. 게임 코드에서는 쓰지 않는다.
void setAutoExpCapOverride(int cap);

struct ExpResult {
    int granted = 0;       // 실제로 지급된 양(자동 상한으로 잘릴 수 있음)
    int levelsGained = 0;
    bool capReached = false;  // 자동 지급 후 상한에 도달했는가
};

// 경험치를 지급한다. fromAuto이면 자동 사냥 상한을 넘지 않도록 정확히 자른다.
ExpResult grantExp(DragonRecord& d, int amount, bool fromAuto);

inline int autoExpRemaining(const DragonRecord& d) {
    int r = autoExpCap() - d.autoExp;
    return r > 0 ? r : 0;
}

}  // namespace dt
