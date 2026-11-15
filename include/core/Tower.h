// 탑 층 구성. 드래곤(Species.h)·적(Enemy.h)과 같은 생각이다 —
// 코드에 내장된 기본값이 있고 assets/tower/floors.txt 로 덮어쓰거나 층을 더할 수 있다.
// 읽는 규칙은 TowerData.h 에 있다.
#pragma once

#include <string>
#include <vector>

namespace dt {

// 한 층에 세우는 적 한 묶음.
struct TowerSpawn {
    std::string key;    // 적 폴더 이름. 비어 있거나 없는 키면 그 층의 기본 적으로 대신한다
    int count = 1;      // 마리 수
    int level = 1;      // 적의 레벨
    float inset = 0.f;  // 오른쪽 끝에서 이만큼 더 앞으로(왼쪽으로) 당긴다
};

// 탑의 한 층.
struct TowerFloorDef {
    std::string title;      // 화면에 보일 이름. 비어 있으면 "N층"으로 보인다
    float spacing = 110.f;  // 적 사이 세로 간격
    bool boss = false;      // 보스 층인가. 없는 키를 대신할 적을 고를 때만 쓴다
    std::vector<TowerSpawn> spawns;
};

// floors.txt 가 없을 때 쓰는 기본 구성. v1.5까지의 3층과 똑같다.
// 데이터 파일을 지워도 게임은 전과 같이 동작한다.
std::vector<TowerFloorDef> builtinTowerFloors();

// ---- 탑 100층 구조 (2026-11-15 사용자 결정) ----
// floors.txt(층마다 나오는 적)와 별개인 구조 규칙이다. 수치는 Config.h 의 kTowerTopFloor 등.
// 설계: docs/tower-100-design.md. 아직 게임 흐름에는 연결하지 않았다(설계안의 T3·T4·T7).
enum class FloorType { Normal, MidBoss, FieldBoss, Invalid };

// 1~kTowerTopFloor. 10의 배수는 필드 보스, 그 외 5의 배수는 중간 보스. 범위 밖은 Invalid.
FloorType floorType(int floor);

// 자동사냥에 쓸 층: 머무는 층의 직전 일반 층. 보스층은 건너뛰고, 없으면 1층.
// 예) 23→22, 21→19, 16→14, 1→1. 100층을 넘은 경우(정복 뒤)는 100층에 있는 것으로 본다.
int huntFloorFor(int currentFloor);

// 필드 보스층의 입장 레벨 상한(층 × kFieldBossLevelPerFloor, 그 레벨 포함). 필드 보스층이 아니면 0.
int fieldBossLevelCap(int floor);

// 출전 드래곤이 정확히 kFieldBossPartySize 마리이고 각각 상한 이하인가.
bool partyMeetsLevelCap(int floor, const std::vector<int>& levels);

}  // namespace dt
