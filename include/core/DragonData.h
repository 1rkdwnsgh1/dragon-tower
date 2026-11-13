// assets/dragons/<키>/dragon.txt 파서와 폴더 로더.
// 형식: 키=값 + [구역] 텍스트 (docs/character-pipeline.md 3장).
// 모르는 키·잘못된 값은 무시하지 않고 경고 목록으로 돌려준다.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/Species.h"

namespace dt {

struct DragonParseResult {
    SpeciesDef def;
    std::vector<std::string> warnings;
};

// dragon.txt 내용을 해석한다. folderKey는 폴더 이름(종의 키로 쓴다).
// base가 있으면 그 종의 값에서 시작해 적힌 값만 바꾼다(내장 종 덮어쓰기).
// 없으면 기본 템플릿에서 시작한다(새 종).
DragonParseResult parseDragonText(const std::string& text, const std::string& folderKey, const SpeciesDef* base);

struct DragonScanResult {
    int folderCount = 0;    // 읽은 드래곤 폴더 수
    int newSpecies = 0;     // 새로 추가된 종 수
    std::vector<std::string> warnings;
};

// dragonsDir의 하위 폴더를 모두 읽어 종 레지스트리에 반영한다.
// '_'나 '.'으로 시작하는 폴더는 건너뛴다(예: _example).
// dragon.txt가 없는 폴더는 키가 이미 등록된 종이면 이미지 전용 폴더로 보고 넘어가고,
// 모르는 키면 경고를 남긴다.
DragonScanResult loadDragonFolders(const std::filesystem::path& dragonsDir);

}  // namespace dt
