// 게임 진행 상태(화면 흐름, 사냥, 탑, 성장, 사망, 저장 요청). 플랫폼과 무관하다.
#pragma once

#include <string>
#include <vector>

#include "core/SaveData.h"
#include "core/World.h"

namespace dt {

enum class Screen {
    Select,        // 드래곤 선택
    Desktop,       // 바탕화면(사냥 또는 대기)
    AutoWarning,   // 자동 사냥 시작 전 경고
    TowerWarning,  // 탑 진입 전 경고와 모드 선택
    Tower,         // 탑 전투
    FloorClear,    // 층 돌파 후 선택
    TowerVictory,  // 탑 정복
    Death,         // 영구 사망 안내
    Codex,         // 기록·도감
};

struct Notice {
    std::string text;
    float t = 0.f;
    float duration = 4.f;
    uint32_t color = 0xFFFFFFFF;
};

class Game {
public:
    Game();

    void init(const SaveData& save, const RectF& desktop);
    void setDesktopBounds(const RectF& desktop);
    void seed(uint32_t s);

    // hidden: 다른 앱이 전체화면이라 숨겨진 상태, inputEnabled: 게임 입력이 켜져 있는가
    void update(float dt, const PlayerInput& in, bool hidden, bool inputEnabled);

    // ---- UI에서 부르는 동작 ----
    void chooseSpecies(Species s);
    void startHunt();
    void stopHunt(const std::string& reason = "");
    bool canChangeMode() const;
    bool setHuntMode(CombatMode m);
    void confirmAutoWarning();
    void openTowerWarning();
    void enterTower(CombatMode m);
    void nextFloor();
    void leaveTower();
    void openCodex();
    void closeModal();          // 경고·도감 닫기
    void acknowledgeDeath();    // 사망 화면 → 새 드래곤 선택

    // ---- 조회 ----
    Screen screen() const { return screen_; }
    bool hasDragon() const { return save_.hasDragon; }
    const DragonRecord& dragonRecord() const { return save_.dragon; }
    bool hunting() const { return hunting_; }
    bool waveActive() const { return waveActive_; }
    float waveBreakLeft() const { return breakTimer_; }
    CombatMode huntMode() const { return save_.huntMode; }
    CombatMode towerMode() const { return towerMode_; }
    int towerFloor() const { return towerFloor_; }
    static constexpr int kTowerFloors = 3;
    bool inTowerFlow() const;
    bool needsInput() const;       // 입력이 필요한 화면인가
    bool combatPaused() const { return paused_; }
    const std::string& pauseReason() const { return pauseReason_; }
    const World& world() const { return world_; }
    World& world() { return world_; }
    const SaveData& save() const { return save_; }
    const std::vector<Notice>& notices() const { return notices_; }
    RectF arena() const;
    const RectF& desktopBounds() const { return desktop_; }
    const GraveEntry* lastGrave() const { return save_.graves.empty() ? nullptr : &save_.graves.back(); }

    // 저장이 필요하면 true를 한 번 돌려준다. 사망은 즉시 저장을 요청한다.
    bool consumeSaveRequest();
    // 저장 직전에 현재 체력 등 실행 중 상태를 기록에 반영한다.
    SaveData snapshotForSave();

    void notify(const std::string& text, uint32_t color = 0xFFFFFFFF, float duration = 4.f);

private:
    EnemyKind meleeByKey(const std::string& key) const;
    EnemyKind randomMelee();
    void spawnWave();
    void spawnTowerFloor();
    void processEvents(bool autoExp);
    void onDragonDied();
    void placeDragonForDesktop();

    SaveData save_;
    World world_;
    Rng rng_;
    RectF desktop_{0.f, 0.f, 1280.f, 720.f};
    Screen screen_ = Screen::Select;
    Screen codexReturn_ = Screen::Desktop;
    bool hunting_ = false;
    bool waveActive_ = false;
    float breakTimer_ = 0.f;
    bool pendingAutoStart_ = false;
    CombatMode towerMode_ = CombatMode::Direct;
    int towerFloor_ = 0;
    std::string deathPlace_;
    bool paused_ = false;
    std::string pauseReason_;
    bool saveRequested_ = false;
    float autosaveTimer_ = 0.f;
    std::vector<Notice> notices_;
};

}  // namespace dt
