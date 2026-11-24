// 탑 100층 T6(2026-11-24): 크레딧 계산과 상점 가격. 플랫폼과 무관한 순수 함수다.
// 수치는 cfg::kCredit* / kEgg* 등(Config.h)에 있다. 설계: docs/tower-100-design.md 4.4
#pragma once

#include <string>

#include "core/Tower.h"

namespace dt {

// 적 키가 정예(elite-*)인가
bool isEliteEnemyKey(const std::string& key);
// 바탕화면 사냥에서 적 하나를 쓰러뜨렸을 때 받는 크레딧
int creditsForKill(int enemyLevel, bool elite, bool autoMode);
// 탑 층을 돌파했을 때 받는 크레딧.
// dragonAlreadyCleared: 이 드래곤이 전에 깬 층이면 0. accountFirst: 계정 첫 돌파면 전액, 아니면 20%.
// 필드 보스층은 파티로 깼을 때만(asParty) 필드 보스 보상을 받고, 단독이면 일반층으로 친다(T7 전).
long long creditsForFloorClear(int floor, bool accountFirst, bool dragonAlreadyCleared, bool asParty);
// 상점 가격
long long eggPrice(int livingDragons);

}  // namespace dt
