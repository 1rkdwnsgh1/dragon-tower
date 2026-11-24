#include "core/Economy.h"

#include <algorithm>
#include <cmath>

#include "core/Config.h"
#include "core/Progression.h"

namespace dt {

bool isEliteEnemyKey(const std::string& key) { return key.rfind("elite-", 0) == 0; }

int creditsForKill(int enemyLevel, bool elite, bool autoMode) {
    int c = cfg::kCreditKillBase + std::max(0, enemyLevel) / cfg::kCreditKillLevelDiv;
    if (elite) c *= cfg::kCreditEliteMul;
    if (autoMode) c = std::max(1, static_cast<int>(std::floor(static_cast<float>(c) * cfg::kCreditAutoMul)));
    return c;
}

long long creditsForFloorClear(int floor, bool accountFirst, bool dragonAlreadyCleared, bool asParty) {
    if (dragonAlreadyCleared || floor < 1) return 0;
    long long per = cfg::kCreditFloorFirst;
    switch (floorType(floor)) {
        case FloorType::MidBoss: per = cfg::kCreditMidBossFirst; break;
        case FloorType::FieldBoss: per = asParty ? cfg::kCreditFieldBossFirst : cfg::kCreditFloorFirst; break;
        case FloorType::Invalid: return 0;
        default: break;
    }
    const long long full = static_cast<long long>(floor) * per;
    return accountFirst ? full : static_cast<long long>(std::floor(static_cast<double>(full) * cfg::kCreditRepeatRatio));
}

long long eggPrice(int livingDragons) {
    return cfg::kEggBasePrice + cfg::kEggStepPrice * std::max(0, livingDragons - 1);
}

long long trainingBookPrice(int level) { return cfg::kTrainingBookPerLevel * std::max(1, level); }

int trainingBookExp(int level) {
    return std::max(1, static_cast<int>(std::floor(static_cast<float>(expToNext(level)) * cfg::kTrainingBookExpRatio)));
}

}  // namespace dt
