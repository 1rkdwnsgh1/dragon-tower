// 저장 파일 파서 퍼저 + 왕복 불변식.
//
// 왜 이것이 가장 중요한가: 저장 파일이 깨지면 **영구 사망 게임에서 기른 개체가 사라진다.**
// v0.7 에서 "저장 파일이 깨지면 백업해 두고 새로 시작"하도록 이미 막아 두었지만,
// 그건 "읽다가 죽지 않는다"를 보장하지 않는다. 사용자가 손으로 고친 파일, 쓰다 꺼져서
// 반쪽만 남은 파일, 다른 버전이 쓴 파일이 전부 이 파서로 들어온다.
//
// 두 가지를 본다.
//   1. 어떤 입력에도 죽지 않는다 (ASan/UBSan 가 잡는다).
//   2. **왕복이 안정적이다.** 한 번 읽어 다시 쓴 글을 또 읽어 쓰면 같아야 한다.
//      이게 깨지면 "켤 때마다 저장 내용이 조금씩 달라지는" 종류의 버그다.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/SaveData.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::string text(reinterpret_cast<const char*>(data), size);

    dt::SaveData a{};
    if (!dt::deserializeSave(text, a)) return 0;  // 거부된 입력은 왕복을 따질 대상이 아니다

    const std::string once = dt::serializeSave(a);

    dt::SaveData b{};
    // 자기가 쓴 글은 반드시 자기가 읽을 수 있어야 한다.
    if (!dt::deserializeSave(once, b)) {
        __builtin_trap();  // serialize 가 자기 파서로 못 읽는 글을 썼다
    }
    const std::string twice = dt::serializeSave(b);

    if (once != twice) {
        __builtin_trap();  // 왕복이 안정적이지 않다 — 저장 내용이 조용히 달라진다
    }
    return 0;
}
