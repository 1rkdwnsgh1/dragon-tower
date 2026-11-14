// 로컬 저장 데이터. 사람이 읽을 수 있는 key=value 텍스트로 저장한다.
#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "core/Enemy.h"
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
    // v0.9 싸울 때 유지할 간격. 0 근접 / 1 중간 / 2 바깥. 스킬 수동·자동에서 쓰인다.
    int keepRange = 0;
    // v0.6 바탕화면 UI 설정. 모르는 키는 읽을 때 무시되므로 이전 저장 파일과 호환된다.
    float panelX = -1.f;          // 상태창 왼쪽 위 좌표(논리 픽셀). -1이면 기본 위치
    float panelY = -1.f;
    bool panelCollapsed = false;  // 상태창을 접어 두었는가
    bool confineToBand = false;   // 드래곤을 화면 맨 아래 띠에만 두는가
    bool hintShown = false;       // 첫 실행 안내(단축키·트레이)를 보여 줬는가
    // v0.8 메뉴 창과 보기 설정. 역시 모르는 키는 무시되므로 이전 저장 파일과 호환된다.
    float menuX = -1.f;           // 메뉴 창 왼쪽 위 좌표. -1이면 기본 위치
    float menuY = -1.f;
    float menuScale = 1.f;        // 메뉴 창 크기 배수
    float charScale = 1.f;        // 화면에 보이는 드래곤·적 크기 배수(보기 전용, 판정은 그대로)
    bool powerSave = false;       // 조용할 때 다시 그리는 횟수를 줄인다(노트북 절전)
    bool menuOpen = false;        // v1.0 메뉴 창을 펼쳐 두었는가(키보드 조작과 무관하게 열린다)
    int bestFloor = 0;        // 탑 최고 도달 층(기록, 개체가 죽어도 유지)
    int towerClears = 0;
    // v0.2: 종이 폴더로 추가될 수 있어 고정 배열 대신 키 집합으로 저장한다.
    // 폴더가 사라진 종의 키도 그대로 남아 도감 기록이 지워지지 않는다.
    std::set<std::string> speciesSeen;
    // v0.4: 적도 폴더로 늘어날 수 있어 키 집합으로 저장한다.
    std::set<std::string> enemiesSeen;
    std::vector<GraveEntry> graves;

    bool sawEnemy(EnemyKind k) const { return enemiesSeen.count(enemyDef(k).key) > 0; }
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
