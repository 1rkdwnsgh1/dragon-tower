// floors.txt 파서 퍼저.
//
// 왜: v1.6 에서 탑 층이 데이터로 빠졌다(PR #24). dragon.txt·enemy.txt 와 같은 이유로
// 사람이 손으로 쓰는 파일이 하나 더 늘었고, 이쪽은 특히 **나중에 루프 횟수·배열 크기가 되는
// 값**을 담는다(층 수, 한 층의 enemy= 줄 수, 한 줄의 마리 수, 적 레벨).
// Config.h 에 울타리 상수가 네 개 있는데(kMaxTowerFloors 등), 퍼저는 그 울타리가
// **실제로 모든 경로에서 걸리는지**를 본다.
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/Config.h"
#include "core/TowerData.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::string text(reinterpret_cast<const char*>(data), size);

    dt::TowerParseResult r = dt::parseTowerText(text);

    // 울타리가 걸렸는지 확인한다. 넘으면 파서가 상한을 놓친 것이다.
    if (r.floors.size() > static_cast<size_t>(dt::cfg::kMaxTowerFloors)) __builtin_trap();

    volatile size_t sink = r.warnings.size();
    for (const dt::TowerFloorDef& f : r.floors) {
        if (f.spawns.size() > static_cast<size_t>(dt::cfg::kMaxSpawnLinesPerFloor)) __builtin_trap();
        sink += f.spawns.size();
        for (const auto& s : f.spawns) {
            if (s.count < 1 || s.count > dt::cfg::kMaxEnemiesPerFloor) __builtin_trap();
            if (s.level < 1 || s.level > dt::cfg::kMaxEnemyLevel) __builtin_trap();
            sink += s.key.size();
        }
    }
    for (const std::string& w : r.warnings) sink += w.size();
    (void)sink;
    return 0;
}
