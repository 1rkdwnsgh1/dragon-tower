#include "core/Progression.h"

#include <algorithm>

#include "core/Config.h"

namespace dt {

int expToNext(int level) {
    level = std::max(1, level);
    return 30 * level + 10 * level * level;  // 40, 100, 180, 280, 400 ...
}

int totalExpToReach(int level) {
    int total = 0;
    for (int l = 1; l < level; ++l) total += expToNext(l);
    return total;
}

namespace {
int g_capOverride = -1;
}

void setAutoExpCapOverride(int cap) { g_capOverride = cap; }

int autoExpCap() {
    if (g_capOverride >= 0) return g_capOverride;
    // 상한을 끄면 아주 큰 값을 돌려준다. 호출하는 쪽을 고치지 않아도 되고,
    // 저장 파일의 autoExp 기록도 그대로 쌓인다(나중에 다시 켤 수 있다).
    if (!cfg::kAutoExpCapEnabled) return kNoAutoExpCap;
    return totalExpToReach(cfg::kAutoExpCapLevel);
}

ExpResult grantExp(DragonRecord& d, int amount, bool fromAuto) {
    ExpResult r;
    if (!d.alive || amount <= 0) {
        r.capReached = fromAuto && d.autoExp >= autoExpCap();
        return r;
    }
    if (fromAuto) {
        int room = std::max(0, autoExpCap() - d.autoExp);
        amount = std::min(amount, room);
        d.autoExp += amount;
        r.capReached = d.autoExp >= autoExpCap();
    }
    r.granted = amount;
    d.exp += amount;
    while (d.level < cfg::kMaxLevel && d.exp >= expToNext(d.level)) {
        d.exp -= expToNext(d.level);
        ++d.level;
        ++r.levelsGained;
    }
    if (d.level >= cfg::kMaxLevel) d.exp = 0;
    return r;
}

}  // namespace dt
