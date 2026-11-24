#include "core/Game.h"

#include "core/Economy.h"

#include <algorithm>
#include <utility>  // std::move (전이 포함을 믿지 않는다 — v1.4의 교훈)

#include "core/Config.h"

namespace dt {

namespace {
constexpr uint32_t kGood = 0xFF8CFFA0;
constexpr uint32_t kWarn = 0xFFFFD166;
constexpr uint32_t kBad = 0xFFFF7070;
}  // namespace

Game::Game() = default;

void Game::seed(uint32_t s) {
    rng_.seed(s);
    world_.seed(s * 7u + 3u);
}

void Game::init(const SaveData& save, const RectF& desktop) {
    save_ = save;
    world_.resetSouls();
    desktop_ = desktop;
    world_.setProjectileViewport(desktop_);
    world_.setBounds(desktop_);
    world_.setKeepRange(save_.keepRange);  // 저장해 둔 간격을 이어서 쓴다
    world_.setSizeScale(save_.charScale);  // 저장해 둔 보기 크기를 판정에도 쓴다
    addingDragon_ = false;
    if (save_.hasDragon) {
        const DragonRecord& d = save_.dragon;
        world_.setupDragon(d.species, d.level, d.hp, {desktop_.center().x, desktop_.bottom() - 120.f});
        world_.dragon().awakened = d.awakened;
        syncSkillSlots();  // v1.4 옛 저장 파일이면 여기서 빈 칸이 채워진다
        screen_ = Screen::Desktop;
    } else {
        // 보유 목록은 있는데 띄운 개체가 없으면(사망 직후 등) 관리창에서 고르게 한다
        screen_ = save_.stable.empty() ? Screen::Select : Screen::Stable;
    }
}

void Game::setDesktopBounds(const RectF& desktop) {
    desktop_ = desktop;
    world_.setProjectileViewport(desktop_);
    if (!inTowerFlow()) {
        // #47: 화면 크기가 바뀌어도 "아래쪽에만" 설정을 지킨다(전에는 사냥 중에도 화면 전체로 풀렸다).
        RectF b = save_.confineToBand ? battleBand() : desktop_;
        world_.setBounds(b);
        world_.dragon().pos = b.clampPoint(world_.dragon().pos, cfg::kDragonRadius);
    }
}

RectF Game::battleBand() const {
    // 작업 표시줄은 작업 영역(desktop_)에서 이미 빠져 있으므로, 그 아래쪽 띠를 쓴다.
    float h = std::min(cfg::kBandHeight, desktop_.h);
    float y = desktop_.bottom() - h - cfg::kBandBottomMargin;
    if (y < desktop_.y) y = desktop_.y;
    return {desktop_.x, y, desktop_.w, h};
}

void Game::setConfineToBand(bool on) {
    if (save_.confineToBand == on) return;
    save_.confineToBand = on;
    saveRequested_ = true;
    // #47(2026-11-16): 사냥 중에도 바로 적용한다. 켜면 띠로 날아 내려가고, 끄면 화면 전체에서 싸운다.
    if (!inTowerFlow() && save_.hasDragon) {
        if (on) {
            // v1.0: 순간이동시키지 않고 날아서 내려간다.
            world_.beginConfine(battleBand());
        } else {
            world_.cancelConfine();
            world_.setBounds(desktop_);
            world_.dragon().groundY = world_.dragon().pos.y;
            world_.dragon().behaviorLeft = 0.f;
        }
    }
    notify(on ? "드래곤이 화면 아래쪽에만 머뭅니다." : "드래곤이 바탕화면 전체를 돌아다닙니다.", kGood, 2.5f);
}

void Game::placeDragon(Vec2 pos) {
    if (!save_.hasDragon) return;
    DragonActor& d = world_.dragon();
    d.pos = world_.bounds().clampPoint(pos, cfg::kDragonRadius);
    d.groundY = d.pos.y;
    d.wanderTarget = d.pos;
    d.behaviorLeft = 0.f;
    d.flying = false;
    d.landing = false;
}

void Game::setPanelPos(Vec2 p) {
    save_.panelX = p.x;
    save_.panelY = p.y;
    saveRequested_ = true;
}

void Game::setPanelCollapsed(bool on) {
    if (save_.panelCollapsed == on) return;
    save_.panelCollapsed = on;
    saveRequested_ = true;
}

void Game::setMenuPos(Vec2 p) {
    save_.menuX = p.x;
    save_.menuY = p.y;
    saveRequested_ = true;
}

void Game::setMenuScale(float s) {
    float v = std::clamp(s, cfg::kMenuScaleMin, cfg::kMenuScaleMax);
    if (save_.menuScale == v) return;
    save_.menuScale = v;
    saveRequested_ = true;
}

void Game::setCharScale(float s) {
    float v = std::clamp(s, cfg::kCharScaleMin, cfg::kCharScaleMax);
    if (save_.charScale == v) return;
    save_.charScale = v;
    world_.setSizeScale(v);  // 판정 크기도 같이 바뀐다(2026-11-17)
    saveRequested_ = true;
}

void Game::setMenuOpen(bool on) {
    if (save_.menuOpen == on) return;
    save_.menuOpen = on;
    saveRequested_ = true;
}

void Game::setMenuTab(int tab) {
    int v = std::clamp(tab, 0, 1);
    if (save_.menuTab == v) return;
    save_.menuTab = v;
    saveRequested_ = true;
}

void Game::setPowerSave(bool on) {
    if (save_.powerSave == on) return;
    save_.powerSave = on;
    saveRequested_ = true;
}

void Game::setTaskbarSkin(bool on) {
    if (save_.taskbarSkin == on) return;
    save_.taskbarSkin = on;
    saveRequested_ = true;
}

RectF Game::arena() const {
    float w = std::min(cfg::kArenaWidth, desktop_.w - 40.f);
    float h = std::min(cfg::kArenaHeight, desktop_.h - 140.f);
    Vec2 c = desktop_.center();
    return {c.x - w * 0.5f, c.y - h * 0.5f, w, h};
}

bool Game::inTowerFlow() const {
    return screen_ == Screen::Tower || screen_ == Screen::FloorClear || screen_ == Screen::TowerVictory ||
           (screen_ == Screen::Death && deathPlace_.rfind("tower", 0) == 0);
}

bool Game::needsInput() const {
    return screen_ != Screen::Desktop;
}

bool Game::wantsKeyboard() const {
    if (screen_ == Screen::Tower) return towerMode_ != CombatMode::Auto;
    if (screen_ != Screen::Desktop) return true;  // 보조 화면에서 Esc를 받는다.
    if (motionMenuOpen_ || (menuOpen() && hasDragon())) return true;
    return hunting_ && save_.huntMode != CombatMode::Auto;
}

void Game::alert(const std::string& title, const std::string& body, bool warning) {
    Alert a;
    a.title = title;
    a.body = body;
    a.warning = warning;
    alerts_.push_back(a);
    if (alerts_.size() > 4) alerts_.erase(alerts_.begin());
}

Game::Alert Game::takeAlert() {
    if (alerts_.empty()) return Alert{};
    Alert a = alerts_.front();
    alerts_.erase(alerts_.begin());
    return a;
}

void Game::notify(const std::string& text, uint32_t color, float duration) {
    Notice n;
    n.text = text;
    n.color = color;
    n.duration = duration;
    notices_.push_back(n);
    if (notices_.size() > 5) notices_.erase(notices_.begin());
}

bool Game::consumeSaveRequest() {
    bool r = saveRequested_;
    saveRequested_ = false;
    return r;
}

SaveData Game::snapshotForSave() {
    if (save_.hasDragon && world_.dragon().alive) {
        save_.dragon.hp = world_.dragon().hp;
        save_.dragon.awakened = world_.dragon().awakened;
    }
    // 불러올 때 dragon 은 stable[activeIndex] 에서 복원되므로(SaveData.cpp 참고),
    // 목록 쪽에도 지금 상태를 적어 두어야 한다. v1.3에서 빠져 있어 다친 드래곤이
    // 다시 켜면 체력이 가득 찬 채로 돌아왔다.
    if (save_.hasDragon && save_.activeIndex >= 0 &&
        save_.activeIndex < static_cast<int>(save_.stable.size())) {
        save_.stable[static_cast<size_t>(save_.activeIndex)] = save_.dragon;
    }
    // T6: 탑 안에서 꺼지면 탑 진행은 남지 않는다. 가지고 들어간 물약은 가방에 있는 것으로 저장한다.
    SaveData out = save_;
    if (carryTaken_) {
        out.potionSmall = std::min(cfg::kMaxInventory, out.potionSmall + carrySmall_);
        out.potionLarge = std::min(cfg::kMaxInventory, out.potionLarge + carryLarge_);
    }
    return out;
}

float Game::maxHpOf(const DragonRecord& d) const { return statsFor(d.species, d.level).maxHp; }

bool Game::canSwitchDragon() const {
    return save_.hasDragon ? (!hunting_ && !inTowerFlow() && !waveActive_) : !inTowerFlow();
}

const char* Game::switchBlockedReason() const {
    if (inTowerFlow()) return "탑 안에서는 바꿀 수 없습니다.";
    if (hunting_ || waveActive_) return "전투 중에는 바꿀 수 없습니다. 사냥을 멈추고 바꾸세요.";
    return nullptr;
}

void Game::openMotionMenu(Vec2 at) {
    if (!save_.hasDragon || screen_ != Screen::Desktop) return;
    motionMenuAt_ = at;
    motionMenuOpen_ = true;
}

void Game::closeMotionMenu() { motionMenuOpen_ = false; }

void Game::setForcedMotion(AnimState st) {
    DragonActor& d = world_.dragon();
    d.animForced = true;
    d.forcedAnim = st;
    motionMenuOpen_ = false;
}

void Game::clearForcedMotion() {
    DragonActor& d = world_.dragon();
    if (!d.animForced) return;
    d.animForced = false;
}

void Game::openStable() {
    if (screen_ != Screen::Desktop && screen_ != Screen::Select) return;
    // 선택 화면에서 돌아올 때는 '추가 중' 상태를 푼다. 보유 목록이 비어 있으면 열 것이 없다.
    addingDragon_ = false;
    if (save_.stable.empty() && !save_.hasDragon) return;
    screen_ = Screen::Stable;
}

void Game::closeStable() {
    if (screen_ != Screen::Stable) return;
    // 띄운 개체가 없으면 선택 화면으로 돌아간다(드래곤 없이 바탕화면만 남지 않게)
    screen_ = save_.hasDragon ? Screen::Desktop : Screen::Select;
}

// ---------------- v1.4 스킬 장착 ----------------

// 기준은 언제나 저장 기록(save_.dragon.slots)이다. World와 보유 목록은 여기서 맞춘다.
void Game::syncSkillSlots() {
    if (!save_.hasDragon) {
        world_.setSlots({-1, -1, -1, -1});
        return;
    }
    fillEmptySkillSlots(save_.dragon);  // 빈 칸은 새로 해제된 스킬로 채운다
    if (save_.activeIndex >= 0 && save_.activeIndex < static_cast<int>(save_.stable.size())) {
        save_.stable[static_cast<size_t>(save_.activeIndex)].slots = save_.dragon.slots;
        save_.stable[static_cast<size_t>(save_.activeIndex)].level = save_.dragon.level;
    }
    world_.setSlots(save_.dragon.slots);
}

void Game::selectSlot(int slot) {
    if (slot < 0 || slot >= kSkillSlots) return;
    selectedSlot_ = slot;
}

bool Game::equipSkill(int slot, int skillIndex) {
    if (!save_.hasDragon) return false;
    if (slot < 0 || slot >= kSkillSlots) return false;
    // 드래곤 교체와 같은 규칙을 쓴다 — 불리할 때 바꿔 끼워 위험을 피하지 못하게 한다.
    if (!canSwitchDragon()) {
        const char* why = switchBlockedReason();
        if (why) notify(why, kWarn, 3.f);
        return false;
    }
    DragonRecord& d = save_.dragon;
    const SpeciesDef& sp = speciesDef(d.species);
    if (skillIndex < 0 || skillIndex >= static_cast<int>(sp.skills.size())) return false;
    const SkillDef& s = sp.skills[static_cast<size_t>(skillIndex)];
    if (skillIsPassive(s.kind)) {
        notify("패시브는 칸에 넣지 않습니다. 해제되면 항상 적용됩니다.", kWarn, 2.5f);
        return false;
    }
    if (s.ultimate) {
        notify("궁극기는 칸에 넣지 않습니다. 키 5나 상태창 아이콘으로 씁니다.", kWarn, 2.5f);
        return false;
    }
    if (d.level < s.unlockLevel) {
        notify(s.name + "은(는) Lv." + std::to_string(s.unlockLevel) + "에 해제됩니다.", kWarn, 2.5f);
        return false;
    }
    // 이미 다른 칸에 있으면 두 칸을 맞바꾼다(같은 스킬이 두 칸을 차지하지 않게)
    for (int i = 0; i < kSkillSlots; ++i) {
        if (i != slot && d.slots[static_cast<size_t>(i)] == skillIndex) {
            d.slots[static_cast<size_t>(i)] = d.slots[static_cast<size_t>(slot)];
            break;
        }
    }
    d.slots[static_cast<size_t>(slot)] = skillIndex;
    // 여기서는 fillEmptySkillSlots를 부르지 않는다. 사람이 일부러 비운 칸을
    // 다시 채워 버리면 "빼 두기"가 불가능해진다.
    if (save_.activeIndex >= 0 && save_.activeIndex < static_cast<int>(save_.stable.size()))
        save_.stable[static_cast<size_t>(save_.activeIndex)].slots = d.slots;
    world_.setSlots(d.slots);
    saveRequested_ = true;
    return true;
}

void Game::clearSlot(int slot) {
    if (!save_.hasDragon || slot < 0 || slot >= kSkillSlots) return;
    if (!canSwitchDragon()) {
        const char* why = switchBlockedReason();
        if (why) notify(why, kWarn, 3.f);
        return;
    }
    if (save_.dragon.slots[static_cast<size_t>(slot)] < 0) return;
    save_.dragon.slots[static_cast<size_t>(slot)] = -1;
    if (save_.activeIndex >= 0 && save_.activeIndex < static_cast<int>(save_.stable.size()))
        save_.stable[static_cast<size_t>(save_.activeIndex)].slots = save_.dragon.slots;
    world_.setSlots(save_.dragon.slots);
    saveRequested_ = true;
}

void Game::useUltimate() {
    // 전투 밖에서 받아 두면 다음 전투가 시작될 때 갑자기 나간다. 그래서 지금 싸우는 중일 때만 받는다.
    const bool inCombat = screen() == Screen::Tower ||
                          (screen() == Screen::Desktop && hunting() && waveActive() && !combatPaused());
    if (!inCombat) {
        notify("궁극기는 전투 중에 쓸 수 있습니다.", kWarn, 2.f);
        return;
    }
    world_.requestUltimate();
}

bool Game::hasUnequippedSkill() const {
    if (!save_.hasDragon) return false;
    const SpeciesDef& sp = speciesDef(save_.dragon.species);
    for (int idx : unlockedActiveSkills(sp, save_.dragon.level)) {
        if (sp.skills[static_cast<size_t>(idx)].ultimate) continue;  // 궁극기는 칸에 넣지 않는다
        bool found = false;
        for (int v : save_.dragon.slots) if (v == idx) { found = true; break; }
        if (!found) return true;
    }
    return false;
}

void Game::openSkills() {
    if (!save_.hasDragon) return;
    skillPage_ = 0;
    // 탑에서는 열 수 없다. 창이 떠 있는 동안 전투가 멈추므로, 위험할 때 창을 열어
    // 시간을 버는 것이 가능해진다. 드래곤 교체를 탑에서 막는 것과 같은 이유다.
    if (screen_ != Screen::Desktop && screen_ != Screen::Stable) return;
    skillsReturn_ = Screen::Desktop;
    screen_ = Screen::Skills;
}

void Game::closeSkills() {
    if (screen_ != Screen::Skills) return;
    screen_ = save_.hasDragon ? skillsReturn_ : Screen::Select;
}

void Game::beginAddDragon() {
    if (screen_ != Screen::Stable || stableFull()) return;
    if (!freeDragonAvailable()) {
        notify("공짜로 데려올 수 있는 드래곤을 다 골랐습니다. 상점에서 알을 사세요.", kWarn, 3.5f);
        return;
    }
    addingDragon_ = true;
    screen_ = Screen::Select;
}

bool Game::switchDragon(int index) {
    if (index < 0 || index >= static_cast<int>(save_.stable.size())) return false;
    if (!save_.stable[static_cast<size_t>(index)].alive) return false;
    if (!canSwitchDragon()) {
        const char* why = switchBlockedReason();
        if (why) notify(why, kWarn, 3.f);
        return false;
    }
    if (index == save_.activeIndex && save_.hasDragon) return true;  // 이미 띄워 둔 개체

    // 지금 띄운 개체의 상태를 목록에 적어 둔다(체력·경험치가 저장되어야 한다)
    if (save_.hasDragon && save_.activeIndex >= 0 &&
        save_.activeIndex < static_cast<int>(save_.stable.size())) {
        save_.dragon.hp = world_.dragon().hp;
        save_.dragon.awakened = world_.dragon().awakened;
        save_.stable[static_cast<size_t>(save_.activeIndex)] = save_.dragon;
    }

    save_.activeIndex = index;
    save_.dragon = save_.stable[static_cast<size_t>(index)];
    save_.hasDragon = true;

    // 바탕화면 설정(아래쪽에만)을 그대로 지키며 새 개체를 세운다
    world_.clearEnemies(false);
    world_.cancelConfine();
    RectF b = save_.confineToBand ? battleBand() : desktop_;
    world_.setBounds(b);
    Vec2 at = b.clampPoint({desktop_.center().x, b.bottom() - cfg::kDragonRadius}, cfg::kDragonRadius);
    world_.setupDragon(save_.dragon.species, save_.dragon.level, save_.dragon.hp, at);
    world_.dragon().awakened = save_.dragon.awakened;
    syncSkillSlots();
    world_.dragon().groundY = world_.dragon().pos.y;
    world_.setKeepRange(save_.keepRange);
    deathPlace_.clear();
    screen_ = Screen::Desktop;
    saveRequested_ = true;
    notify(speciesDef(save_.dragon.species).name + " Lv." + std::to_string(save_.dragon.level) +
               "(으)로 바꿨습니다.", kGood, 2.5f);
    return true;
}

void Game::setKeepRange(int v) {
    int n = std::clamp(v, 0, cfg::kKeepRangeCount - 1);
    if (save_.keepRange == n) return;
    save_.keepRange = n;
    world_.setKeepRange(n);
    saveRequested_ = true;
    notify(std::string("간격: ") + keepRangeName(n), kGood, 2.0f);
}

const char* Game::keepRangeName(int v) const {
    switch (v) {
        case 1: return "중간";
        case 2: return "바깥";
        default: return "근접";
    }
}

void Game::setDragonHeld(bool on) {
    if (!save_.hasDragon) return;
    DragonActor& d = world_.dragon();
    if (d.held == on) return;
    d.held = on;
    if (!on) {
        // 놓은 자리를 새 기준으로 삼는다. 공중에 놓았으면 날아서 내려온다.
        d.groundY = d.pos.y;
        d.wanderTarget = d.pos;
        d.restTime = 0.f;
        d.behaviorLeft = 0.f;
    }
}

void Game::placeDragonForDesktop() {
    world_.setBounds(desktop_);
    world_.dragon().pos = {desktop_.center().x, desktop_.bottom() - 120.f};
    world_.dragon().wanderTarget = world_.dragon().pos;
}

// ---------------- 선택 ----------------

void Game::chooseSpecies(Species s) {
    if (screen_ != Screen::Select) return;
    if (stableFull()) {
        notify("더 가질 수 없습니다. 최대 " + std::to_string(save_.stableCapacity()) + "마리입니다.", kWarn, 3.f);
        screen_ = save_.hasDragon ? Screen::Stable : Screen::Select;
        return;
    }
    if (!freeDragonAvailable()) {
        notify("공짜로 고를 수 있는 드래곤이 없습니다. 상점에서 알을 사세요.", kWarn, 3.5f);
        screen_ = save_.hasDragon ? Screen::Stable : Screen::Select;
        addingDragon_ = false;
        return;
    }
    // 처음 4마리 안이면 공짜 횟수를 쓴다. 다 쓴 뒤라면 '모두 죽었을 때의 구조' 한 마리다.
    if (save_.freeDragonsUsed < cfg::kFreeDragonPicks) ++save_.freeDragonsUsed;
    DragonRecord d = makeDragonRecord(s);
    // v1.3: 목록에 더한다. 지금 플레이 중인 개체가 있으면 그대로 두고 목록에만 넣는다.
    bool wasAdding = addingDragon_ && save_.hasDragon;
    if (wasAdding) {
        // 띄워 둔 개체의 현재 상태를 목록에 먼저 반영한다
        if (save_.activeIndex >= 0 && save_.activeIndex < static_cast<int>(save_.stable.size()))
            save_.stable[static_cast<size_t>(save_.activeIndex)] = save_.dragon;
        save_.stable.push_back(d);
        save_.markSeen(s);
        addingDragon_ = false;
        notify(speciesDef(s).name + "을(를) 데려왔습니다. 관리창에서 바탕화면에 띄울 수 있습니다.", kGood, 3.5f);
        screen_ = Screen::Stable;
        saveRequested_ = true;
        return;
    }
    addingDragon_ = false;
    save_.stable.push_back(d);
    save_.activeIndex = static_cast<int>(save_.stable.size()) - 1;
    save_.dragon = d;
    save_.hasDragon = true;
    save_.markSeen(s);
    world_.clearEnemies(false);
    world_.setBounds(desktop_);
    world_.setupDragon(s, 1, -1.f, {desktop_.center().x, desktop_.bottom() - 120.f});
    syncSkillSlots();
    deathPlace_.clear();
    screen_ = Screen::Desktop;
    saveRequested_ = true;
    notify(std::string(speciesDef(s).name) + " #" + std::to_string(d.id) + "이(가) 태어났습니다.", kGood);
}

// ---------------- 바탕화면 사냥 ----------------

bool Game::canChangeMode() const {
    return screen_ == Screen::Desktop && (!hunting_ || !waveActive_);
}

bool Game::setHuntMode(CombatMode m) {
    if (!save_.hasDragon) return false;
    if (m == save_.huntMode) return true;
    if (!canChangeMode()) {
        notify("전투 중에는 모드를 바꿀 수 없습니다. 이번 무리를 끝낸 뒤 바꿔 주세요.", kWarn);
        return false;
    }
    if (m == CombatMode::Auto) {
        if (autoExpRemaining(save_.dragon) <= 0) {
            notify("이 드래곤은 자동 사냥 경험치 상한에 도달했습니다.", kWarn);
            return false;
        }
        if (hunting_) {
            pendingAutoStart_ = false;
            screen_ = Screen::AutoWarning;  // 확인 후 모드 변경
            return false;
        }
    }
    save_.huntMode = m;
    saveRequested_ = true;
    return true;
}

void Game::startHunt() {
    if (!save_.hasDragon || hunting_ || screen_ != Screen::Desktop) return;
    if (save_.huntMode == CombatMode::Auto) {
        if (autoExpRemaining(save_.dragon) <= 0) {
            notify("자동 사냥 경험치 상한에 도달했습니다. 직접 조작이나 스킬 수동으로 사냥하세요.", kWarn);
            return;
        }
        pendingAutoStart_ = true;
        screen_ = Screen::AutoWarning;
        return;
    }
    world_.resetSouls();
    hunting_ = true;
    waveActive_ = false;
    waveNumber_ = 0;
    enterBattleArea();
    breakTimer_ = 1.5f;
    notify(std::string(combatModeName(save_.huntMode)) + " 모드로 사냥을 시작합니다.", kGood, 2.5f);
}

void Game::confirmAutoWarning() {
    if (screen_ != Screen::AutoWarning) return;
    screen_ = Screen::Desktop;
    save_.huntMode = CombatMode::Auto;
    saveRequested_ = true;
    if (pendingAutoStart_ && !hunting_) {
        world_.resetSouls();
        hunting_ = true;
        waveActive_ = false;
        enterBattleArea();
        breakTimer_ = 1.5f;
    }
    pendingAutoStart_ = false;
    notify("자동 사냥 중입니다. 자동 사냥에서도 드래곤이 죽을 수 있습니다.", kWarn, 4.f);
}

// #47(2026-11-16 사용자 결정): 싸우는 자리는 "아래쪽에만" 설정을 따른다.
// 켜면 맨 아래 띠로 내려 세우고, 끄면 드래곤이 있던 자리 그대로 화면 전체에서 싸운다.
void Game::enterBattleArea() {
    world_.cancelConfine();
    if (save_.confineToBand) {
        world_.setBounds(battleBand());
        world_.dragon().pos = battleBand().clampPoint(world_.dragon().pos, cfg::kDragonRadius);
    } else {
        world_.setBounds(desktop_);
    }
    world_.dragon().groundY = world_.dragon().pos.y;
}

void Game::stopHunt(const std::string& reason) {
    if (!hunting_) return;
    world_.resetSouls();
    hunting_ = false;
    waveActive_ = false;
    waveNumber_ = 0;
    // 전투가 끝나면 다시 바탕화면 전체를 돌아다닌다.
    if (!inTowerFlow()) {
        // 설정에 따라 띠에 머물거나 바탕화면 전체로 돌아간다
        world_.setBounds(save_.confineToBand ? battleBand() : desktop_);
        world_.cancelConfine();
        world_.dragon().behaviorLeft = 0.f;  // 다음 프레임에 새 행동을 고른다
    }
    world_.clearEnemies(true);
    if (!reason.empty()) notify(reason, kWarn, 5.f);
    saveRequested_ = true;
}

EnemyKind Game::randomMelee() {
    // 바탕화면 사냥에는 일반 적이 섞여 나온다.
    std::vector<EnemyKind> pool;
    for (int i = 0; i < enemyCount(); ++i) {
        EnemyKind k = static_cast<EnemyKind>(i);
        if (enemyDef(k).role == EnemyRole::Melee) pool.push_back(k);
    }
    if (pool.empty()) return defaultMelee();
    return pool[static_cast<size_t>(rng_.range(0, static_cast<int>(pool.size()) - 1))];
}

int Game::huntFloor() const {
    int f = huntFloorFor(save_.dragon.floor);
    // floors.txt 가 아직 100층이 아니면(T4 전) 있는 층까지만 본다.
    if (towerFloorCount() > 0) f = std::min(f, towerFloorCount());
    return std::max(1, f);
}

std::vector<EnemyKind> Game::huntPool() const {
    std::vector<EnemyKind> pool;
    if (floors_.empty()) return pool;
    const TowerFloorDef& f = towerFloorDef(huntFloor());
    for (const TowerSpawn& s : f.spawns) {
        EnemyKind k;
        if (s.key.empty() || !enemyFromKey(s.key, k)) continue;
        if (enemyDef(k).role != EnemyRole::Melee) continue;  // 보스는 바탕화면에 나오지 않는다
        for (int i = 0; i < std::max(1, s.count); ++i) pool.push_back(k);
    }
    return pool;
}

bool Game::recordFloorCleared(int floor) {
    if (!save_.hasDragon || floorType(floor) == FloorType::Invalid) return false;
    DragonRecord& d = save_.dragon;
    d.clearedFloors.insert(floor);
    d.cleared = std::clamp(std::max(d.cleared, floor), 0, cfg::kTowerTopFloor);
    // 머무는 층은 돌파한 다음 층. 탑의 끝(floors.txt 마지막 층)을 넘지 않는다.
    const int top = std::max(1, std::min(cfg::kTowerTopFloor, towerFloorCount()));
    d.floor = std::clamp(std::max(d.floor, floor + 1), 1, top);
    save_.firstClear = std::clamp(std::max(save_.firstClear, floor), 0, cfg::kTowerTopFloor);
    if (save_.activeIndex >= 0 && save_.activeIndex < static_cast<int>(save_.stable.size())) {
        DragonRecord& s = save_.stable[static_cast<size_t>(save_.activeIndex)];
        s.floor = d.floor;
        s.cleared = d.cleared;
        s.clearedFloors = d.clearedFloors;
    }
    return save_.firstClearFloors.insert(floor).second;
}

void Game::spawnWave() {
    // 디펜스형: 적은 화면 왼쪽 밖에서 줄지어 들어와 드래곤에게 걸어온다.
    ++waveNumber_;
    RectF band = battleBand();
    int level = save_.dragon.level;
    int byLevel = 1 + level / 2;
    int byWave = 1 + static_cast<int>(waveNumber_ / cfg::kWaveLevelStep);
    int maxCount = std::clamp(std::max(byLevel, byWave), cfg::kWaveMin, cfg::kWaveMax);
    int count = rng_.range(cfg::kWaveMin, maxCount);
    const std::vector<EnemyKind> pool = huntPool();  // T3: 드래곤 층 기준 구성
    for (int i = 0; i < count; ++i) {
        int lv = std::max(1, level - rng_.range(0, 1));
        // 한 줄로 겹치지 않게 왼쪽 밖에서 간격을 두고 세운다
        float x = band.x - cfg::kSpawnOffsetX - static_cast<float>(i) * 70.f;
        // 띠가 얇으면 40/30 여백이 뒤집힌다. 여백을 띠 높이에 맞춰 줄인다.
        float margin = std::min(30.f, band.h * 0.25f);
        float lo = band.y + margin, hi = band.bottom() - margin;
        if (lo > hi) lo = hi = band.y + band.h * 0.5f;
        if (!save_.confineToBand) {
            // #47: 화면 전체에서 싸울 때는 드래곤이 있는 높이 근처로 들어온다.
            float dy = world_.dragon().pos.y;
            lo = std::max(desktop_.y + 30.f, dy - 80.f);
            hi = std::min(desktop_.bottom() - 30.f, dy + 80.f);
            if (lo > hi) lo = hi = std::clamp(dy, desktop_.y, desktop_.bottom());
        }
        float y = rng_.uniform(lo, hi);
        EnemyKind kind = pool.empty() ? randomMelee()
                                      : pool[static_cast<size_t>(rng_.range(0, static_cast<int>(pool.size()) - 1))];
        world_.spawnEnemy(kind, lv, {x, y});
    }
    waveActive_ = true;
}

// ---------------- 탑 ----------------

void Game::openTowerSelection() {
    if (!save_.hasDragon || screen_ != Screen::Desktop) return;
    screen_ = Screen::TowerSelect;
    selectTowerFloor(save_.dragon.cleared + 1);
    scrollTowerFloors(selectedTowerFloor_ - visibleTowerFloors / 2);
}

void Game::selectTowerFloor(int floor) {
    if (screen_ != Screen::TowerSelect) return;
    selectedTowerFloor_ = std::clamp(floor, 1, towerFloorCount());
    if (selectedTowerFloor_ < firstVisibleTowerFloor_) scrollTowerFloors(selectedTowerFloor_);
    if (selectedTowerFloor_ >= firstVisibleTowerFloor_ + visibleTowerFloors)
        scrollTowerFloors(selectedTowerFloor_ - visibleTowerFloors + 1);
}

int Game::topVisibleTowerFloor() const {
    return std::min(towerFloorCount(), firstVisibleTowerFloor_ + visibleTowerFloors - 1);
}

void Game::scrollTowerFloors(int first) {
    if (screen_ == Screen::TowerSelect)
        firstVisibleTowerFloor_ = std::clamp(first, 1, std::max(1, towerFloorCount() - visibleTowerFloors + 1));
}

int Game::towerRecommendedLevel(int floor) const {
    int level = 1;
    for (const auto& spawn : towerFloorDef(floor).spawns) level = std::max(level, spawn.level);
    return level;
}

void Game::openTowerWarning() {
    if (!save_.hasDragon || (screen_ != Screen::Desktop && screen_ != Screen::TowerSelect)) return;
    warningFromSelection_ = screen_ == Screen::TowerSelect;
    // T6: 물약은 가진 만큼 기본으로 챙긴다(소 먼저). 경고창에서 −/+ 로 바꾼다.
    if (!carryTaken_) setTowerPotionCarry(cfg::kTowerPotionCarry, cfg::kTowerPotionCarry);
    if (!warningFromSelection_) selectedTowerFloor_ = 1;
    screen_ = Screen::TowerWarning;
}

const TowerFloorDef& Game::towerFloorDef(int floor) const {
    // 층은 1부터 센다. 목록이 비는 일은 없지만(setTowerFloors가 막는다) 만일을 대비해 둔다.
    if (floors_.empty()) {
        static const TowerFloorDef kEmpty{};
        return kEmpty;
    }
    int n = static_cast<int>(floors_.size());
    int i = std::clamp(floor, 1, n) - 1;
    return floors_[static_cast<size_t>(i)];
}

void Game::setTowerFloors(std::vector<TowerFloorDef> floors) {
    if (floors.empty()) {
        floors_ = builtinTowerFloors();
        return;
    }
    floors_ = std::move(floors);
}

std::string Game::towerFloorLabel(int floor) const {
    const TowerFloorDef& f = towerFloorDef(floor);
    if (!f.title.empty()) return "탑 " + f.title;
    return "탑 " + std::to_string(floor) + "층";
}

void Game::spawnTowerFloor() {
    RectF a = arena();
    float rx = a.right() - 120.f;
    float cy = a.center().y;
    const TowerFloorDef& f = towerFloorDef(towerFloor_);

    // 한 층의 적을 모두 펼쳐 놓고 가운데를 기준으로 위아래로 고르게 세운다.
    // spacing 은 "옆 적과의 간격"이다 — 2마리면 ±spacing/2, 3마리면 -spacing, 0, +spacing.
    std::vector<const TowerSpawn*> list;
    for (const TowerSpawn& s : f.spawns) {
        for (int i = 0; i < s.count; ++i) list.push_back(&s);
    }
    int n = static_cast<int>(list.size());
    for (int i = 0; i < n; ++i) {
        const TowerSpawn& s = *list[static_cast<size_t>(i)];
        EnemyKind k;
        // 키를 비웠거나(= '-') 없는 키면 그 층의 기본 적으로 대신한다.
        // v1.5까지는 일반 적만 놓을 수 있었는데, 이제 키로 보스도 아래 층에 놓을 수 있다.
        if (s.key.empty() || !enemyFromKey(s.key, k)) k = f.boss ? defaultBoss() : defaultMelee();
        float offset = (static_cast<float>(i) - static_cast<float>(n - 1) * 0.5f) * f.spacing;
        world_.spawnEnemy(k, s.level, {rx - s.inset, cy + offset});
    }
}

void Game::enterTower(CombatMode m) {
    if (screen_ != Screen::TowerWarning || !save_.hasDragon) return;
    if (m == CombatMode::Auto) return;  // 탑에서는 자동 사냥을 쓸 수 없다
    world_.resetSouls();
    if (hunting_) {
        hunting_ = false;
        waveActive_ = false;
    }
    world_.clearEnemies(false);
    towerMode_ = m;
    // T6: 고른 물약을 가방에서 꺼내 가지고 들어간다
    setTowerPotionCarry(carrySmall_, carryLarge_);
    save_.potionSmall -= carrySmall_;
    save_.potionLarge -= carryLarge_;
    carryTaken_ = true;
    drinkTimer_ = 0.f;
    towerFloor_ = std::clamp(selectedTowerFloor_, 1, towerFloorCount());
    towerFinalRewardGranted_ = false;
    save_.bestFloor = std::max(save_.bestFloor, towerFloor_);
    RectF a = arena();
    world_.setBounds(a);
    world_.healPercent(1.f);
    world_.dragon().pos = {a.x + 120.f, a.center().y};
    spawnTowerFloor();
    screen_ = Screen::Tower;
    saveRequested_ = true;
    notify(towerFloorLabel(towerFloor_), 0xFFFFFFFF, 2.f);
}

void Game::nextFloor() {
    if (screen_ != Screen::FloorClear) return;
    towerFloor_++;
    save_.bestFloor = std::max(save_.bestFloor, towerFloor_);
    RectF a = arena();
    world_.dragon().pos = {a.x + 120.f, a.center().y};
    spawnTowerFloor();
    screen_ = Screen::Tower;
    saveRequested_ = true;
    notify(towerFloorLabel(towerFloor_), 0xFFFFFFFF, 2.f);
}

void Game::leaveTower() {
    if (screen_ != Screen::FloorClear && screen_ != Screen::TowerVictory) return;
    returnTowerPotions();
    world_.resetSouls();
    world_.clearEnemies(false);
    towerFloor_ = 0;
    placeDragonForDesktop();
    screen_ = Screen::Desktop;
    saveRequested_ = true;
}

// ---------------- 기타 화면 ----------------

void Game::openCodex() {
    if (screen_ == Screen::Desktop || screen_ == Screen::Select) {
        codexReturn_ = screen_;
        screen_ = Screen::Codex;
    }
}

void Game::dismissWindow() {
    switch (screen_) {
        case Screen::Select:
            if (addingDragon_) openStable();
            break;
        case Screen::Stable: closeStable(); break;
        case Screen::Skills: closeSkills(); break;
        case Screen::Shop: closeShop(); break;
        case Screen::Death: acknowledgeDeath(); break;
        case Screen::FloorClear:
        case Screen::TowerVictory: leaveTower(); break;
        case Screen::Desktop:
            if (motionMenuOpen_) closeMotionMenu();
            else if (menuOpen()) setMenuOpen(false);
            break;
        default: closeModal(); break;
    }
}

void Game::closeModal() {
    if (screen_ == Screen::TowerWarning && warningFromSelection_) {
        screen_ = Screen::TowerSelect;
    } else if (screen_ == Screen::AutoWarning || screen_ == Screen::TowerWarning || screen_ == Screen::TowerSelect) {
        pendingAutoStart_ = false;
        screen_ = Screen::Desktop;
    } else if (screen_ == Screen::Codex) {
        screen_ = codexReturn_;
    }
}

void Game::acknowledgeDeath() {
    if (screen_ != Screen::Death) return;
    world_.clearEnemies(false);
    towerFloor_ = 0;
    deathPlace_.clear();
    world_.setBounds(desktop_);
    // 남은 드래곤이 있으면 관리창에서 고르게 한다. 자동으로 다음 개체를 띄우지는 않는다
    // (죽음이 그냥 넘어가지 않도록 한 번은 사람이 고르게 한다).
    screen_ = save_.stable.empty() ? Screen::Select : Screen::Stable;
}

void Game::onDragonDied() {
    std::string place;
    if (screen_ == Screen::Tower) returnTowerPotions();  // T6: 쓰지 않은 물약은 가방으로(부활은 없다)
    drinkTimer_ = 0.f;
    if (screen_ == Screen::Tower) place = "tower" + std::to_string(towerFloor_);
    else place = (save_.huntMode == CombatMode::Auto) ? "auto" : "hunt";
    deathPlace_ = place;

    // 사망 확정: 즉시 저장해 재실행으로 부활하지 않게 한다.
    save_.dragon.alive = false;
    save_.dragon.hp = 0.f;
    GraveEntry g;
    g.id = save_.dragon.id;
    g.species = save_.dragon.species;
    g.level = save_.dragon.level;
    g.place = place;
    save_.graves.push_back(g);
    // v1.3: 보유 목록에서도 지운다. 남겨 두면 다시 켤 때 되살아난다(영구 사망 위반).
    // 죽은 개체는 묘비(graves)로만 남는다.
    if (save_.activeIndex >= 0 && save_.activeIndex < static_cast<int>(save_.stable.size()))
        save_.stable.erase(save_.stable.begin() + save_.activeIndex);
    save_.activeIndex = -1;
    save_.hasDragon = false;
    hunting_ = false;
    waveActive_ = false;
    alert(speciesDef(save_.dragon.species).name + " 사망",
          "Lv." + std::to_string(save_.dragon.level) + " · " +
          (deathPlace_.rfind("tower", 0) == 0 ? "탑에서" : "바탕화면 사냥 중에") +
          " 쓰러졌습니다. 되살릴 수 없습니다.", true);
    screen_ = Screen::Death;
    saveRequested_ = true;
}

void Game::processEvents(bool autoExp) {
    for (const WorldEvent& ev : world_.takeEvents()) {
        if (ev.type == WorldEventType::EnemySpawned) {
            const std::string& ek = enemyDef(ev.enemyKind).key;
            if (!ek.empty() && save_.enemiesSeen.insert(ek).second) saveRequested_ = true;
        } else if (ev.type == WorldEventType::EnemyKilled) {
            if (!save_.hasDragon) continue;
            // T6: 바탕화면 사냥 처치는 크레딧을 준다(탑은 층 돌파로 준다)
            if (screen_ != Screen::Tower)
                addCredits(creditsForKill(ev.level, isEliteEnemyKey(enemyDef(ev.enemyKind).key), autoExp), nullptr);
            int oldLevel = save_.dragon.level;
            ExpResult r = grantExp(save_.dragon, ev.exp, autoExp);
            if (r.levelsGained > 0) {
                world_.setDragonLevel(save_.dragon.level);
                syncSkillSlots();  // 새로 해제된 스킬을 빈 칸에 넣는다
                world_.addFx(FxType::LevelUp, world_.dragon().pos, world_.dragon().pos, 70.f, 1.0f, 0xFFFFE066);
                notify("레벨 업! Lv." + std::to_string(save_.dragon.level), kGood);
                const SpeciesDef& sp = speciesDef(save_.dragon.species);
                for (const SkillDef& s : sp.skills) {
                    if (s.unlockLevel > oldLevel && s.unlockLevel <= save_.dragon.level)
                        notify(std::string("새 스킬: ") + s.name, kGood, 5.f);
                }
                saveRequested_ = true;
            }
            if (autoExp && r.capReached) {
                stopHunt("자동 사냥 경험치 상한에 도달해 자동 사냥을 멈췄습니다. 이후에는 직접 조작이나 스킬 수동으로 성장시키세요.");
                saveRequested_ = true;
                return;  // 남은 처치 경험치는 상한 때문에 지급되지 않는다
            }
        } else if (ev.type == WorldEventType::DragonDied) {
            onDragonDied();
            return;
        }
    }
}

// ---------------- 갱신 ----------------

void Game::update(float dt, const PlayerInput& in, bool hidden, bool inputEnabled) {
    for (Notice& n : notices_) n.t += dt;
    notices_.erase(std::remove_if(notices_.begin(), notices_.end(), [](const Notice& n) { return n.t >= n.duration; }),
                   notices_.end());

    paused_ = false;
    pauseReason_.clear();
    PlayerInput none;

    const bool wasAwakened = world_.dragon().awakened;

    // v1.3: 보관 중인(띄우지 않은) 개체는 쉬면서 체력만 회복한다. 경험치는 오르지 않는다.
    // 한 번에 한 마리만 키우게 해서 영구 사망의 무게를 지킨다.
    for (size_t i = 0; i < save_.stable.size(); ++i) {
        if (static_cast<int>(i) == save_.activeIndex) continue;
        DragonRecord& d = save_.stable[i];
        if (!d.alive) continue;
        float maxHp = maxHpOf(d);
        if (d.hp < 0.f) d.hp = maxHp;  // 음수면 아직 꽉 찬 상태다
        if (d.hp >= maxHp) continue;
        d.hp = std::min(maxHp, d.hp + maxHp * cfg::kStableRegenRatio * dt);
    }

    switch (screen_) {
        case Screen::Death:
        case Screen::FloorClear:
        case Screen::TowerVictory:
            world_.updateFx(dt, true);
            break;

        // v1.5: 드래곤 관리창·스킬창·선택창이 떠 있어도 바탕화면 전투는 멈추지 않는다.
        // 창을 여는 동안 적이 멈춰 서 있으면 전투가 끊겨 보인다는 이야기가 있었다.
        case Screen::Select:
        case Screen::Stable:
        case Screen::Skills:
        case Screen::Shop:
        case Screen::Desktop:
        case Screen::AutoWarning:
        case Screen::TowerWarning:
        case Screen::TowerSelect:
        case Screen::Codex: {
            if (!save_.hasDragon) break;
            if (!hunting_) {
                world_.updateWander(dt);
                world_.regen(cfg::kRegenIdle, dt);
                world_.updateFx(dt);
                break;
            }
            bool isAuto = save_.huntMode == CombatMode::Auto;
            if (!isAuto) {
                // 직접 조작·스킬 수동은 플레이어가 볼 수 있고 입력할 수 있을 때만 진행한다.
                // v1.5: **창이 열려 있다고 멈추지는 않는다**(관리창·스킬창·선택창). 창이 떠 있어도
                // 키보드는 그대로 들어오므로 직접 조작도 이어진다.
                // 보이지 않거나 키보드를 받지 못하는 경우에는 전처럼 멈춘다 — 손을 쓸 수 없는데
                // 맞기만 하는 상황을 막기 위한 것이다.
                if (hidden) { paused_ = true; pauseReason_ = "다른 앱이 전체화면이라 사냥을 잠시 멈췄습니다."; }
                else if (!inputEnabled) { paused_ = true; pauseReason_ = "창이 활성화되지 않아 사냥을 멈췄습니다. 드래곤을 한 번 누르세요."; }
                if (paused_) break;
            }
            // 자동 사냥은 숨겨진 동안에도 진행된다(숨김은 일시정지가 아니다).
            const PlayerInput& use = (!isAuto && inputEnabled && screen_ != Screen::TowerSelect) ? in : none;
            world_.updateCombat(dt, save_.huntMode, use);
            world_.updateFx(dt);
            if (!waveActive_) world_.regen(cfg::kRegenBetweenWaves, dt);
            processEvents(isAuto);
            if (!hunting_ || screen_ == Screen::Death) break;
            if (waveActive_) {
                if (world_.aliveEnemyCount() == 0) {
                    waveActive_ = false;
                    breakTimer_ = cfg::kWaveBreak;
                }
            } else {
                breakTimer_ -= dt;
                if (breakTimer_ <= 0.f) spawnWave();
            }
            break;
        }

        case Screen::Tower: {
            if (hidden) { paused_ = true; pauseReason_ = "다른 앱이 전체화면이라 탑 전투를 잠시 멈췄습니다."; break; }
            if (!inputEnabled) { paused_ = true; pauseReason_ = "게임 입력이 꺼져 탑 전투를 멈췄습니다. 설정 버튼으로 켜세요."; break; }
            if (drinkTimer_ > 0.f) {
                drinkTimer_ -= dt;
                world_.dragon().moveScale = cfg::kPotionDrinkMove;
                if (drinkTimer_ <= 0.f) {
                    drinkTimer_ = 0.f;
                    world_.dragon().moveScale = 1.f;
                    world_.healPercent(drinkLarge_ ? cfg::kPotionLargeHeal : cfg::kPotionSmallHeal);
                    world_.addFx(FxType::LevelUp, world_.dragon().pos, world_.dragon().pos, 50.f, 0.6f, 0xFF7CFFB0);
                }
            }
            world_.updateCombat(dt, towerMode_, in);
            world_.updateFx(dt);
            processEvents(false);
            if (screen_ != Screen::Tower) break;
            if (world_.aliveEnemyCount() == 0) {
                // T6: 기록을 올리기 전에 보상 판정(이 드래곤이 깬 적 있는 층인가, 계정 첫 돌파인가)
                const bool dragonAlready = save_.dragon.clearedFloors.count(towerFloor_) > 0;
                const bool accountFirst = save_.firstClearFloors.count(towerFloor_) == 0;
                drinkTimer_ = 0.f;
                world_.dragon().moveScale = 1.f;
                const bool firstClear = recordFloorCleared(towerFloor_);
                addCredits(creditsForFloorClear(towerFloor_, accountFirst, dragonAlready, false), "층 돌파");
                if (towerFloor_ >= towerFloorCount()) {
                    world_.resetSouls();
                    save_.towerClears++;
                    towerFinalRewardGranted_ = firstClear;
                    if (firstClear) {
                        ExpResult r = grantExp(save_.dragon, cfg::kTowerClearExp, false);
                        if (r.levelsGained > 0) { world_.setDragonLevel(save_.dragon.level); syncSkillSlots(); }
                    }
                    alert("탑 정복", speciesDef(save_.dragon.species).name + "이(가) 탑의 마지막 층을 돌파했습니다.");
                    screen_ = Screen::TowerVictory;
                } else {
                    world_.healPercent(cfg::kTowerFloorHeal);
                    screen_ = Screen::FloorClear;
                }
                saveRequested_ = true;
            }
            break;
        }
    }

    if (save_.hasDragon && world_.dragon().awakened != wasAwakened) saveRequested_ = true;

    autosaveTimer_ += dt;
    if (autosaveTimer_ >= cfg::kAutosaveInterval) {
        autosaveTimer_ = 0.f;
        if (save_.hasDragon) saveRequested_ = true;
    }
}

}  // namespace dt

namespace dt {

// ---------------- 탑 100층 T6: 크레딧·상점·탑 물약 ----------------

void Game::addCredits(long long amount, const char* why) {
    if (amount <= 0) return;
    save_.credits = std::min(cfg::kMaxCredits, save_.credits + amount);
    if (why) notify(std::string(why) + " +" + std::to_string(amount) + " 크레딧", 0xFFFFD45A, 2.5f);
    saveRequested_ = true;
}

DragonRecord Game::makeDragonRecord(Species s) {
    DragonRecord d;
    d.id = save_.nextDragonId++;
    d.species = s;
    d.alive = true;
    d.level = 1;
    d.exp = 0;
    d.autoExp = 0;
    d.hp = -1.f;
    return d;
}

void Game::addDragonToStable(const DragonRecord& d) {
    // 띄워 둔 개체의 현재 상태를 목록에 먼저 반영한다(chooseSpecies 와 같은 순서)
    if (save_.hasDragon && save_.activeIndex >= 0 && save_.activeIndex < static_cast<int>(save_.stable.size())) {
        save_.dragon.hp = world_.dragon().hp;
        save_.stable[static_cast<size_t>(save_.activeIndex)] = save_.dragon;
    }
    save_.stable.push_back(d);
    save_.markSeen(d.species);
    saveRequested_ = true;
}

long long Game::shopPrice(ShopItem item) const {
    switch (item) {
        case ShopItem::Egg: return eggPrice(static_cast<int>(save_.stable.size()));
        case ShopItem::PotionSmall: return cfg::kPotionSmallPrice;
        case ShopItem::PotionLarge: return cfg::kPotionLargePrice;
        case ShopItem::TrainingBook: return trainingBookPrice(save_.hasDragon ? save_.dragon.level : 1);
    }
    return 0;
}

const char* Game::shopBlockedReason(ShopItem item) const {
    if (inTowerFlow() || screen_ == Screen::Tower) return "탑에 있는 동안에는 상점을 쓸 수 없습니다.";
    if (save_.credits < shopPrice(item)) return "크레딧이 모자랍니다.";
    switch (item) {
        case ShopItem::Egg:
            if (stableFull()) return "빈 칸이 없습니다. 필드 보스를 깨면 칸이 늘어납니다.";
            if (speciesCount() <= 0) return "부화할 드래곤 종이 없습니다.";
            break;
        case ShopItem::PotionSmall:
            if (save_.potionSmall >= cfg::kMaxInventory) return "더 가질 수 없습니다.";
            break;
        case ShopItem::PotionLarge:
            if (save_.potionLarge >= cfg::kMaxInventory) return "더 가질 수 없습니다.";
            break;
        case ShopItem::TrainingBook:
            if (!save_.hasDragon) return "플레이 중인 드래곤이 없습니다.";
            if (save_.dragon.level >= cfg::kMaxLevel) return "이미 최고 레벨입니다.";
            break;
    }
    return nullptr;
}

bool Game::buy(ShopItem item) {
    if (const char* why = shopBlockedReason(item)) {
        notify(why, kWarn, 3.f);
        return false;
    }
    const long long price = shopPrice(item);
    switch (item) {
        case ShopItem::Egg: {
            save_.credits -= price;
            const Species s = static_cast<Species>(rng_.range(0, speciesCount() - 1));
            addDragonToStable(makeDragonRecord(s));
            notify(speciesDef(s).name + "이(가) 알에서 태어났습니다. 관리창에서 띄울 수 있습니다.", kGood, 3.5f);
            break;
        }
        case ShopItem::PotionSmall: save_.credits -= price; ++save_.potionSmall; break;
        case ShopItem::PotionLarge: save_.credits -= price; ++save_.potionLarge; break;
        case ShopItem::TrainingBook: {
            save_.credits -= price;
            const int oldLevel = save_.dragon.level;
            const int gain = trainingBookExp(save_.dragon.level);
            ExpResult r = grantExp(save_.dragon, gain, false);
            if (r.levelsGained > 0) {
                world_.setDragonLevel(save_.dragon.level);
                syncSkillSlots();
                notify("레벨 업! Lv." + std::to_string(save_.dragon.level), kGood);
            } else {
                notify("경험치 +" + std::to_string(gain), kGood, 2.5f);
            }
            (void)oldLevel;
            break;
        }
    }
    saveRequested_ = true;
    return true;
}

bool Game::hatchEgg() {
    if (save_.eggs <= 0 || stableFull() || speciesCount() <= 0) return false;
    --save_.eggs;
    const Species s = static_cast<Species>(rng_.range(0, speciesCount() - 1));
    addDragonToStable(makeDragonRecord(s));
    notify(speciesDef(s).name + "이(가) 알에서 태어났습니다.", kGood, 3.5f);
    return true;
}

void Game::setTowerPotionCarry(int smallCount, int largeCount) {
    if (carryTaken_) return;  // 들어간 뒤에는 바꿀 수 없다
    smallCount = std::clamp(smallCount, 0, std::min(cfg::kTowerPotionCarry, save_.potionSmall));
    largeCount = std::clamp(largeCount, 0, std::min(cfg::kTowerPotionCarry - smallCount, save_.potionLarge));
    carrySmall_ = smallCount;
    carryLarge_ = largeCount;
}

bool Game::drinkPotion(bool useLarge) {
    if (screen_ != Screen::Tower || drinkTimer_ > 0.f || !world_.dragon().alive) return false;
    int& have = useLarge ? carryLarge_ : carrySmall_;
    if (have <= 0) return false;
    --have;
    drinkLarge_ = useLarge;
    drinkTimer_ = cfg::kPotionDrinkTime;
    world_.dragon().moveScale = cfg::kPotionDrinkMove;
    return true;
}

void Game::returnTowerPotions() {
    if (!carryTaken_) return;
    save_.potionSmall = std::min(cfg::kMaxInventory, save_.potionSmall + carrySmall_);
    save_.potionLarge = std::min(cfg::kMaxInventory, save_.potionLarge + carryLarge_);
    carrySmall_ = carryLarge_ = 0;
    carryTaken_ = false;
    drinkTimer_ = 0.f;
    world_.dragon().moveScale = 1.f;
    saveRequested_ = true;
}

void Game::openShop() {
    if (screen_ != Screen::Stable && screen_ != Screen::Desktop) return;
    shopReturn_ = screen_;
    screen_ = Screen::Shop;
}

void Game::closeShop() {
    if (screen_ != Screen::Shop) return;
    screen_ = shopReturn_ == Screen::Desktop && save_.hasDragon ? Screen::Desktop : Screen::Stable;
    if (screen_ == Screen::Stable && save_.stable.empty() && !save_.hasDragon) screen_ = Screen::Select;
}

bool Game::freeDragonAvailable() const {
    if (stableFull()) return false;
    if (save_.freeDragonsUsed < cfg::kFreeDragonPicks) return true;
    return save_.stable.empty();  // 모두 죽었으면 한 마리는 공짜(2026-11-24 사용자 결정)
}

void Game::adjustTowerPotion(int code) {
    if (screen_ != Screen::TowerWarning) return;
    int sm = carrySmall_, lg = carryLarge_;
    if (code == 0) --sm;
    else if (code == 1) ++sm;
    else if (code == 2) --lg;
    else if (code == 3) ++lg;
    if (sm + lg > cfg::kTowerPotionCarry) {  // 합계를 넘으면 반대쪽을 하나 줄인다
        if (code == 1 && lg > 0) --lg;
        else if (code == 3 && sm > 0) --sm;
        else return;
    }
    setTowerPotionCarry(sm, lg);
    // setTowerPotionCarry 는 소를 먼저 채우므로 대를 늘렸을 때 소가 줄지 않았으면 다시 맞춘다
    if (code == 3 && carryLarge_ < lg && carrySmall_ > 0) setTowerPotionCarry(carrySmall_ - 1, lg);
}

}  // namespace dt
