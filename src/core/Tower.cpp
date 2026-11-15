// 탑 층의 내장 기본값.
// v1.5까지 Game::spawnTowerFloor 에 하드코딩돼 있던 3층을 그대로 옮긴 것이다.
// 값을 바꾸지 말 것 — assets/tower/floors.txt 가 없을 때 "전과 똑같이" 동작하는 기준이다.
#include "core/Tower.h"

#include <algorithm>

#include "core/Config.h"

namespace dt {

std::vector<TowerFloorDef> builtinTowerFloors() {
    std::vector<TowerFloorDef> out;

    // 1층: 슬라임 둘. 가운데에서 위아래로 110씩 벌어져 선다(사이 간격 220).
    TowerFloorDef f1;
    f1.title = "1층";
    f1.spacing = 220.f;
    f1.spawns.push_back({"slime", 2, 4, 0.f});
    out.push_back(f1);

    // 2층: 고블린 · 오우거 · 고블린. 오우거만 60px 앞으로 나온다.
    TowerFloorDef f2;
    f2.title = "2층";
    f2.spacing = 160.f;
    f2.spawns.push_back({"goblin", 1, 6, 0.f});
    f2.spawns.push_back({"ogre", 1, 6, 60.f});
    f2.spawns.push_back({"goblin", 1, 6, 0.f});
    out.push_back(f2);

    // 3층: 수호자(보스). 키를 적지 않아 defaultBoss() 가 쓰인다.
    TowerFloorDef f3;
    f3.title = "3층: 수호자";
    f3.boss = true;
    f3.spawns.push_back({"", 1, 8, 40.f});
    out.push_back(f3);

    return out;
}

// ---- 탑 100층 구조 ----

FloorType floorType(int floor) {
    if (floor < 1 || floor > cfg::kTowerTopFloor) return FloorType::Invalid;
    if (floor % cfg::kFieldBossEvery == 0) return FloorType::FieldBoss;
    if (floor % cfg::kMidBossEvery == 0) return FloorType::MidBoss;
    return FloorType::Normal;
}

int huntFloorFor(int currentFloor) {
    int f = std::min(currentFloor, cfg::kTowerTopFloor + 1) - 1;
    while (f >= 1 && floorType(f) != FloorType::Normal) --f;
    return f >= 1 ? f : 1;
}

int fieldBossLevelCap(int floor) {
    return floorType(floor) == FloorType::FieldBoss ? floor * cfg::kFieldBossLevelPerFloor : 0;
}

bool partyMeetsLevelCap(int floor, const std::vector<int>& levels) {
    const int cap = fieldBossLevelCap(floor);
    if (cap <= 0) return false;
    if (static_cast<int>(levels.size()) != cfg::kFieldBossPartySize) return false;
    for (int lv : levels) {
        if (lv < 1 || lv > cap) return false;
    }
    return true;
}

}  // namespace dt
