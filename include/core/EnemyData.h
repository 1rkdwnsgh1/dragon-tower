// assets/enemies/<키>/enemy.txt 파서와 폴더 로더.
// 드래곤과 같은 형식이다(키=값 + [구역]). 구현은 DragonData.cpp에 함께 있다
// (줄 자르기·숫자 읽기 같은 보조 함수를 공유하기 위해서다).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/Enemy.h"

namespace dt {

struct EnemyParseResult {
    EnemyDef def;
    std::vector<std::string> warnings;
};

// enemy.txt 내용을 해석한다. folderKey는 폴더 이름(적의 키로 쓴다).
// base가 있으면 그 값에서 시작해 적힌 값만 바꾼다(내장 적 덮어쓰기).
EnemyParseResult parseEnemyText(const std::string& text, const std::string& folderKey, const EnemyDef* base);

struct EnemyScanResult {
    int folderCount = 0;
    int newEnemies = 0;
    std::vector<std::string> warnings;
};

// enemiesDir의 하위 폴더를 모두 읽어 적 레지스트리에 반영한다.
// '_'나 '.'으로 시작하는 폴더는 건너뛴다.
EnemyScanResult loadEnemyFolders(const std::filesystem::path& enemiesDir);

}  // namespace dt
