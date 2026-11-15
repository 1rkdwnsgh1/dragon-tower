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

}  // namespace dt
