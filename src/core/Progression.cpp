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

void sanitizeSkillSlots(DragonRecord& d) {
    const SpeciesDef& sp = speciesDef(d.species);
    const int n = static_cast<int>(sp.skills.size());
    for (int i = 0; i < kSkillSlots; ++i) {
        int& v = d.slots[static_cast<size_t>(i)];
        if (v < 0 || v >= n) { v = -1; continue; }
        const SkillDef& s = sp.skills[static_cast<size_t>(v)];
        // 패시브는 칸을 쓰지 않는다. 아직 해제되지 않은 스킬도 비운다.
        if (skillIsPassive(s.kind) || d.level < s.unlockLevel) { v = -1; continue; }
        for (int j = 0; j < i; ++j) {  // 같은 스킬을 두 칸에 둘 수 없다
            if (d.slots[static_cast<size_t>(j)] == v) { v = -1; break; }
        }
    }
}

void fillEmptySkillSlots(DragonRecord& d) {
    sanitizeSkillSlots(d);
    const SpeciesDef& sp = speciesDef(d.species);
    for (int idx : unlockedActiveSkills(sp, d.level)) {
        if (sp.skills[static_cast<size_t>(idx)].kind == SkillKind::Procession) continue;
        bool already = false;
        for (int v : d.slots) if (v == idx) { already = true; break; }
        if (already) continue;
        for (int& v : d.slots) {
            if (v < 0) { v = idx; break; }
        }
    }
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
    // v1.4: 레벨이 올라 새 스킬이 열리면 빈 칸에 바로 넣어 준다. 관리창을 열지 않아도
    // 쓸 수 있게 하려는 것이고, 사람이 채워 둔 칸은 건드리지 않는다.
    if (r.levelsGained > 0) fillEmptySkillSlots(d);
    return r;
}

}  // namespace dt
