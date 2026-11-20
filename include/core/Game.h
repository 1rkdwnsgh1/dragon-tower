// 게임 진행 상태(화면 흐름, 사냥, 탑, 성장, 사망, 저장 요청). 플랫폼과 무관하다.
#pragma once

#include <string>
#include <vector>

#include "core/Config.h"
#include "core/SaveData.h"
#include "core/Tower.h"
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
    Stable,        // v1.3 드래곤 관리창(보유 목록·묘비)
    Skills,        // v1.4 스킬 관리창(해제된 스킬 목록과 장착 4칸)
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
    // v1.6: 층 구성이 데이터(assets/tower/floors.txt)에서 온다. 파일이 없으면 내장 3층이다.
    // 층 수가 더 이상 상수가 아니므로 화면에 쓸 때는 이 함수를 쓴다.
    int towerFloorCount() const { return static_cast<int>(floors_.size()); }
    // T3: 지금 드래곤의 바탕화면 사냥 층(테스트·화면 표시용). floors.txt 층 수를 넘지 않는다.
    int huntFloor() const;
    // floor는 1부터 센다. 범위를 벗어나면 가장 가까운 층을 돌려준다(빈 목록은 만들지 않는다).
    const TowerFloorDef& towerFloorDef(int floor) const;
    // 데이터로 읽은 층으로 갈아끼운다. 빈 목록이면 내장 구성을 쓴다.
    void setTowerFloors(std::vector<TowerFloorDef> floors);
    bool inTowerFlow() const;
    bool needsInput() const;
    // v1.4: 키보드를 받아야 하는 상태인가. 싸우는 중이고 자동 모드가 아닐 때만이다.
    // 자동 사냥은 키가 필요 없으므로 쓰던 프로그램의 포커스를 가져오지 않는다.
    bool wantsKeyboard() const;       // 입력이 필요한 화면인가
    bool combatPaused() const { return paused_; }
    const std::string& pauseReason() const { return pauseReason_; }
    const World& world() const { return world_; }
    World& world() { return world_; }
    const SaveData& save() const { return save_; }
    const std::vector<Notice>& notices() const { return notices_; }
    RectF arena() const;
    // 바탕화면 디펜스 전투가 일어나는 띠. 화면 맨 아래, 작업 표시줄 바로 위다.
    RectF battleBand() const;
    // v0.6: 드래곤을 띠에만 둘지. 켜면 전투를 꺼도 맨 아래 띠에서만 돌아다닌다.
    bool confineToBand() const { return save_.confineToBand; }
    void setConfineToBand(bool on);
    // v0.6: 드래곤을 마우스로 끌어다 놓는다(경계 안으로 맞춘다).
    void placeDragon(Vec2 pos);
    // v0.9: 잡고 있는 동안에는 자동 행동을 멈춘다. 놓으면 그 자리에서 다시 시작한다.
    void setDragonHeld(bool on);
    bool dragonHeld() const { return world_.dragon().held; }
    // v0.6: 상태창 위치·접힘. 화면 밖으로 나가지 않게 맞춘다.
    Vec2 panelPos() const { return {save_.panelX, save_.panelY}; }
    void setPanelPos(Vec2 p);
    bool hintShown() const { return save_.hintShown; }
    void markHintShown() { save_.hintShown = true; saveRequested_ = true; }
    bool panelCollapsed() const { return save_.panelCollapsed; }
    void setPanelCollapsed(bool on);
    // v0.8: 메뉴 창 위치·크기와 보기 설정.
    Vec2 menuPos() const { return {save_.menuX, save_.menuY}; }
    void setMenuPos(Vec2 p);
    float menuScale() const { return save_.menuScale; }
    // 배수는 허용 범위로 맞춰서 넣는다. 범위를 벗어난 값을 그대로 저장하지 않는다.
    void setMenuScale(float s);
    float charScale() const { return save_.charScale; }
    void setCharScale(float s);
    bool powerSave() const { return save_.powerSave; }
    void setPowerSave(bool on);
    // v1.9: 작업표시줄 꾸미기
    bool taskbarSkin() const { return save_.taskbarSkin; }
    void setTaskbarSkin(bool on);
    // v1.0: 메뉴 창은 키보드 조작과 상관없이 마우스로 열고 닫는다.
    bool menuOpen() const { return save_.menuOpen; }
    void setMenuOpen(bool on);
    // v1.4 메뉴 탭: 0 전투 설정 / 1 환경 설정
    int menuTab() const { return save_.menuTab; }
    void setMenuTab(int t);
    // v0.9: 싸울 때 유지할 간격(0 근접 / 1 중간 / 2 바깥). 스킬 수동·자동에 적용된다.
    int keepRange() const { return save_.keepRange; }
    void setKeepRange(int v);
    const char* keepRangeName(int v) const;
    // v0.9: 지금 쓸 UI 스킨 키. 드래곤이 없으면(선택 화면) default.
    std::string uiSkin() const {
        if (!save_.hasDragon) return "default";
        const std::string& s = speciesDef(save_.dragon.species).uiSkin;
        return s.empty() ? "default" : s;
    }
    // ---- v1.3 드래곤 관리 ----
    // 보유 목록. 죽은 개체는 들어 있지 않다(묘비로만 남는다).
    const std::vector<DragonRecord>& stable() const { return save_.stable; }
    int activeIndex() const { return save_.activeIndex; }
    bool stableFull() const { return save_.stable.size() >= static_cast<size_t>(cfg::kMaxStable); }
    // 전투·탑 밖에서만 바꿀 수 있다(불리할 때 바꿔 끼워 위험을 피하는 것을 막는다).
    bool canSwitchDragon() const;
    const char* switchBlockedReason() const;  // 못 바꾸는 이유. 바꿀 수 있으면 nullptr
    // 그 자리의 드래곤을 바탕화면에 띄운다(= 플레이할 드래곤으로 삼는다).
    bool switchDragon(int index);
    // ---- v1.5 모션 고르기 ----
    // 드래곤을 오른쪽 클릭하면 목록이 열리고, 고르면 다음 입력까지 그 모션을 유지한다.
    bool motionMenuOpen() const { return motionMenuOpen_; }
    Vec2 motionMenuAt() const { return motionMenuAt_; }
    void openMotionMenu(Vec2 at);
    void closeMotionMenu();
    void setForcedMotion(AnimState st);
    void clearForcedMotion();     // 모션 메뉴의 자동 선택
    bool motionForced() const { return world_.dragon().animForced; }
    AnimState forcedMotion() const { return world_.dragon().forcedAnim; }

    void openStable();
    void closeStable();

    // ---- v1.4 스킬 관리 ----
    // 지금 플레이 중인 개체의 장착 칸. 값은 그 종의 스킬 목록 번호, -1은 빈 칸.
    const std::array<int, kSkillSlots>& skillSlots() const { return save_.dragon.slots; }
    // 관리창에서 고른 칸(0~3). 스킬을 누르면 이 칸에 들어간다. 저장하지 않는다.
    int selectedSlot() const { return selectedSlot_; }
    void selectSlot(int slot);
    // 스킬 목록이 한 화면에 다 들어가지 않을 때의 쪽 번호(0부터). 저장하지 않는다.
    // 한 쪽에 몇 줄이 들어가는지는 그리는 쪽이 정하므로, 쪽 수 제한도 그리는 쪽이 알려 준다.
    int skillPage() const { return skillPage_; }
    void setSkillPage(int page) { skillPage_ = page < 0 ? 0 : page; }
    // 그 칸에 스킬을 넣는다. 다른 칸에 이미 있으면 두 칸을 맞바꾼다.
    // 해제되지 않았거나 패시브면 아무 일도 하지 않고 false.
    bool equipSkill(int slot, int skillIndex);
    void clearSlot(int slot);
    // 해제됐지만 어느 칸에도 없는 액티브가 있는가(관리창을 열어 보라고 표시할 때 쓴다)
    bool hasUnequippedSkill() const;
    void openSkills();
    void closeSkills();
    // 관리창에서 "새 드래곤"을 누르면 선택 화면을 띄운다. 고르면 목록에 더해진다.
    void beginAddDragon();
    bool addingDragon() const { return addingDragon_; }
    // 보관 중인(띄우지 않은) 개체의 최대 체력. 체력바를 그리는 데 쓴다.
    float maxHpOf(const DragonRecord& d) const;

    const RectF& desktopBounds() const { return desktop_; }
    const GraveEntry* lastGrave() const { return save_.graves.empty() ? nullptr : &save_.graves.back(); }

    // 저장이 필요하면 true를 한 번 돌려준다. 사망은 즉시 저장을 요청한다.
    bool consumeSaveRequest();
    // 저장 직전에 현재 체력 등 실행 중 상태를 기록에 반영한다.
    SaveData snapshotForSave();

    void notify(const std::string& text, uint32_t color = 0xFFFFFFFF, float duration = 4.f);
    // 화면을 보고 있지 않아도 알아야 하는 일(사망·탑 결과·저장 실패)을 따로 모은다.
    // 플랫폼 쪽에서 가져가 트레이 풍선 알림 등으로 보여 준다.
    struct Alert {
        std::string title;
        std::string body;
        bool warning = false;  // 경고(빨간 아이콘)로 보여 줄지
    };
    void alert(const std::string& title, const std::string& body, bool warning = false);
    bool hasAlert() const { return !alerts_.empty(); }
    Alert takeAlert();  // 하나 꺼내 간다(없으면 빈 것)

private:
    EnemyKind randomMelee();
    // 탑 100층 T3(2026-11-18): 바탕화면 디펜스에 나올 적 후보. 드래곤이 머무는 층의 직전 일반 층
    // (huntFloorFor) 구성에서 일반 적만, 마리 수만큼 넣는다. 비면 randomMelee 로 대신한다.
    std::vector<EnemyKind> huntPool() const;
    // 탑 층을 돌파했을 때 그 드래곤의 진행(floor·cleared)과 계정 첫 돌파 기록을 올린다.
    void recordFloorCleared(int floor);
    void enterBattleArea();  // #47: 전투를 시작할 때 "아래쪽에만" 설정에 따라 싸울 영역을 정한다
    void spawnWave();
    int waveNumber_ = 0;  // 이번 전투에서 보낸 웨이브 수
    void spawnTowerFloor();
    std::string towerFloorLabel(int floor) const;  // "탑 1층" / "탑 3층: 수호자"
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
    bool addingDragon_ = false;
    bool motionMenuOpen_ = false;
    Vec2 motionMenuAt_{};  // v1.3 선택 화면이 '추가'인지 '첫 선택'인지
    int selectedSlot_ = 0;       // v1.4 스킬 관리창에서 고른 칸
    int skillPage_ = 0;          // v1.4 스킬 목록 쪽 번호
    Screen skillsReturn_ = Screen::Desktop;  // 스킬 관리창을 닫으면 돌아갈 화면
    // 장착이 바뀌면 World에 알린다. 저장 기록(save_.dragon.slots)이 언제나 기준이다.
    void syncSkillSlots();
    bool waveActive_ = false;
    float breakTimer_ = 0.f;
    bool pendingAutoStart_ = false;
    CombatMode towerMode_ = CombatMode::Direct;
    int towerFloor_ = 0;
    // 탑 층 구성. 기본값은 내장 3층이고, App이 floors.txt를 읽었으면 갈아끼운다.
    std::vector<TowerFloorDef> floors_ = builtinTowerFloors();
    std::string deathPlace_;
    bool paused_ = false;
    std::string pauseReason_;
    bool saveRequested_ = false;
    float autosaveTimer_ = 0.f;
    std::vector<Notice> notices_;
    std::vector<Alert> alerts_;
};

}  // namespace dt
