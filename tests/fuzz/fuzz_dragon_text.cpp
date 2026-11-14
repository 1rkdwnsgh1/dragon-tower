// dragon.txt 파서 퍼저.
//
// 왜: assets/dragons/<키>/dragon.txt 는 사람이 손으로 쓰는 파일이다. 오타·반쪽 줄·깨진 UTF-8·
// 말도 안 되는 숫자가 실제로 들어온다. v0.4 에서 "범위 밖 값이 조용히 잘렸다"로 고생한 자리가
// 바로 이 계열이다. 파서는 어떤 입력에도 **죽지 않고** 경고를 남겨야 한다.
//
// 돌리는 법은 tests/fuzz/README.md 참고.
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/DragonData.h"
#include "core/Species.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // 입력 1바이트를 "내장 종을 덮어쓰는가"로 쓴다. 나머지가 파일 내용이다.
    if (size < 1) return 0;
    const bool withBase = (data[0] & 1) != 0;
    const std::string text(reinterpret_cast<const char*>(data + 1), size - 1);

    dt::SpeciesDef base;
    base.key = "fuzzbase";
    base.name = "퍼즈용";

    dt::DragonParseResult r = dt::parseDragonText(text, "fuzzkey", withBase ? &base : nullptr);

    // 결과를 실제로 읽어 본다 — 읽지 않으면 파서가 만든 쓰레기 값을 놓친다.
    volatile size_t sink = r.def.key.size() + r.def.name.size() + r.def.role.size() + r.def.uiSkin.size();
    for (const dt::SkillDef& s : r.def.skills) sink += s.name.size() + s.desc.size() + s.motion.size();
    for (const std::string& w : r.warnings) sink += w.size();
    (void)sink;
    return 0;
}
