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
    // hasDragon / dragon 은 **지금 플레이 중인(바탕화면에 띄운)** 개체다.
    // v1.3부터 보유 목록(stable)이 생겼고, dragon 은 stable[activeIndex] 의 사본으로 유지된다.
    // 기존 코드가 save_.dragon 을 그대로 쓰도록 남겨 두었다.
    bool hasDragon = false;   // 살아 있는 현재 개체가 있는가
    DragonRecord dragon;
    // v1.3 보유 목록. 최대 cfg::kMaxStable 마리. 죽으면 여기서 빠지고 graves 로 간다.
    std::vector<DragonRecord> stable;
    int activeIndex = -1;     // stable 에서 지금 플레이 중인 자리. 없으면 -1
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
    float charScale = 1.f;        // 드래곤·적 크기 배수. 2026-11-17부터 판정에도 쓴다(Config.h 참고)
    bool powerSave = false;       // 조용할 때 다시 그리는 횟수를 줄인다(노트북 절전)
    // v1.9: 작업표시줄 둘레를 드래곤 스킨으로 꾸민다. 스킨에 taskbar.png 가 있을 때만 쓰인다.
    // 2026-11-17(#77): 작업표시줄을 덮지 않고 바로 바깥쪽에 그린다. 저장 형식은 그대로다.
    // 모르는 키는 무시되므로 이전 저장 파일과 호환된다.
    bool taskbarSkin = true;
    bool menuOpen = false;        // v1.0 메뉴 창을 펼쳐 두었는가(키보드 조작과 무관하게 열린다)
    int menuTab = 0;              // v1.4 메뉴 탭. 0 전투 설정 / 1 환경 설정
    int bestFloor = 0;        // 탑 최고 도달 층(기록, 개체가 죽어도 유지)
    int towerClears = 0;
    // v0.2: 종이 폴더로 추가될 수 있어 고정 배열 대신 키 집합으로 저장한다.
    // 폴더가 사라진 종의 키도 그대로 남아 도감 기록이 지워지지 않는다.
    std::set<std::string> speciesSeen;
    // v0.4: 적도 폴더로 늘어날 수 있어 키 집합으로 저장한다.
    std::set<std::string> enemiesSeen;
    std::vector<GraveEntry> graves;
    // 탑 100층 T2(2026-11-18). 계정 공용 기록이다 — 드래곤이 죽어도 남는다.
    long long credits = 0;          // 크레딧(설계안 4.4). 얻고 쓰는 것은 T6
    std::set<int> fieldBossCleared; // 계정이 깬 필드 보스층(10, 20, ...)
    std::set<int> firstClearFloors;
    int firstClear = 0;             // 계정이 처음 돌파한 가장 높은 층(첫 돌파 보상 판정)
    // 탑 100층 T6(2026-11-24) 가방. 계정 공용 — 드래곤이 죽어도 남는다.
    int potionSmall = 0;            // 탑 물약(소)
    int potionLarge = 0;            // 탑 물약(대)
    int eggs = 0;                   // 아직 부화하지 않은 알(보상으로 받았는데 칸이 없을 때)
    // 지금 가질 수 있는 드래곤 수 = kMaxStable + 깬 20의 배수 필드 보스층 수.
    int stableCapacity() const;

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
