// 로컬 저장 데이터. 사람이 읽을 수 있는 key=value 텍스트로 저장한다.
#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "core/Progression.h"

namespace dt {

enum class CombatMode { Direct = 0, SkillManual = 1, Auto = 2 };

const char* combatModeName(CombatMode m);

struct GraveEntry {
    int id = 0;
    Species species = Species::Fire;
    int level = 1;
    std::string place;  // "hunt", "auto", "tower1" 등
};

struct SaveData {
    int version = 1;
    int nextDragonId = 1;
    bool hasDragon = false;   // 살아 있는 현재 개체가 있는가
    DragonRecord dragon;
    CombatMode huntMode = CombatMode::SkillManual;
    int bestFloor = 0;        // 탑 최고 도달 층(기록, 개체가 죽어도 유지)
    int towerClears = 0;
    // v0.2: 종이 폴더로 추가될 수 있어 고정 배열 대신 키 집합으로 저장한다.
    // 폴더가 사라진 종의 키도 그대로 남아 도감 기록이 지워지지 않는다.
    std::set<std::string> speciesSeen;
    bool impSeen = false;
    bool guardianSeen = false;
    std::vector<GraveEntry> graves;

    bool sawSpecies(Species s) const { return speciesSeen.count(speciesDef(s).key) > 0; }
    void markSeen(Species s) { speciesSeen.insert(speciesDef(s).key); }
};

std::string serializeSave(const SaveData& s);
// 알 수 없는 줄은 무시한다. 형식이 완전히 잘못되면 false.
bool deserializeSave(const std::string& text, SaveData& out);

// 임시 파일에 쓴 뒤 바꿔치기해서 쓰는 도중 꺼져도 이전 저장이 남게 한다.
bool saveToFile(const SaveData& s, const std::filesystem::path& path);
bool loadFromFile(const std::filesystem::path& path, SaveData& out);

std::string placeLabel(const std::string& place);

}  // namespace dt
