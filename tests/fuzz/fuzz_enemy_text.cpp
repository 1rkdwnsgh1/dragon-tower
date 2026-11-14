// enemy.txt 파서 퍼저. 이유는 fuzz_dragon_text.cpp 와 같다.
//
// 이쪽은 사거리·거리 유지·2페이즈·소환까지 데이터로 받으므로 값의 조합이 더 넓다.
// 특히 summonCount·summonLevel 처럼 나중에 루프 횟수가 되는 값이 들어 있다.
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/Enemy.h"
#include "core/EnemyData.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 1) return 0;
    const bool withBase = (data[0] & 1) != 0;
    const std::string text(reinterpret_cast<const char*>(data + 1), size - 1);

    dt::EnemyDef base;
    base.key = "fuzzbase";
    base.name = "퍼즈용";

    dt::EnemyParseResult r = dt::parseEnemyText(text, "fuzzkey", withBase ? &base : nullptr);

    volatile size_t sink = r.def.key.size() + r.def.name.size() + r.warnings.size();
    // 루프 횟수로 쓰이는 값이 터무니없이 크면 여기서 드러난다.
    sink += static_cast<size_t>(r.def.summonCount < 0 ? 0 : r.def.summonCount);
    for (const std::string& w : r.warnings) sink += w.size();
    (void)sink;
    return 0;
}
