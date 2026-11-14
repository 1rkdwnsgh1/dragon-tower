// assets/tower/floors.txt 파서와 로더.
// 형식: 키=값 + [구역] 텍스트. dragon.txt·enemy.txt 와 같은 규칙이다
// (';' 뒤는 주석, 모르는 키·범위 밖 값은 조용히 무시하지 않고 경고로 돌려준다).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/Tower.h"

namespace dt {

struct TowerParseResult {
    std::vector<TowerFloorDef> floors;
    std::vector<std::string> warnings;
};

// floors.txt 내용을 해석한다. 층은 [floor.1] 부터 차례로 적는다.
// 적 키가 실제로 등록돼 있는지는 여기서 보지 않는다(파일만 보고 해석한다).
TowerParseResult parseTowerText(const std::string& text);

struct TowerScanResult {
    bool found = false;  // floors.txt 가 있었는가. 없으면 floors는 비어 있다
    std::vector<TowerFloorDef> floors;
    std::vector<std::string> warnings;
};

// towerDir(= assets/tower) 의 floors.txt 를 읽는다.
// 파일이 없으면 found=false 로 돌아오고, 부르는 쪽이 builtinTowerFloors()를 쓴다.
// 층이 하나도 해석되지 않으면 경고를 남기고 found=false 로 돌린다.
TowerScanResult loadTowerFloors(const std::filesystem::path& towerDir);

}  // namespace dt
