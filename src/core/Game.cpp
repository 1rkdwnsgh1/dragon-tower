#include "core/Game.h"

#include <algorithm>

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
    desktop_ = desktop;
    world_.setBounds(desktop_);
    if (save_.hasDragon) {
        const DragonRecord& d = save_.dragon;
        world_.setupDragon(d.species, d.level, d.hp, {desktop_.center().x, desktop_.bottom() - 120.f});
        screen_ = Screen::Desktop;
    } else {
        screen_ = Screen::Select;
    }
}

void Game::setDesktopBounds(const RectF& desktop) {
    desktop_ = desktop;
    if (!inTowerFlow()) {
        world_.setBounds(desktop_);
        world_.dragon().pos = desktop_.clampPoint(world_.dragon().pos, cfg::kDragonRadius);
    }
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
    if (save_.hasDragon && world_.dragon().alive) save_.dragon.hp = world_.dragon().hp;
    return save_;
}

void Game::placeDragonForDesktop() {
    world_.setBounds(desktop_);
    world_.dragon().pos = {desktop_.center().x, desktop_.bottom() - 120.f};
    world_.dragon().wanderTarget = world_.dragon().pos;
}

// ---------------- 선택 ----------------

void Game::chooseSpecies(Species s) {
    if (screen_ != Screen::Select) return;
    DragonRecord d;
    d.id = save_.nextDragonId++;
    d.species = s;
    d.alive = true;
    d.level = 1;
    d.exp = 0;
    d.autoExp = 0;
    d.hp = -1.f;
    save_.dragon = d;
    save_.hasDragon = true;
    save_.markSeen(s);
    world_.clearEnemies(false);
    world_.setBounds(desktop_);
    world_.setupDragon(s, 1, -1.f, {desktop_.center().x, desktop_.bottom() - 120.f});
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
    hunting_ = true;
    waveActive_ = false;
    breakTimer_ = 1.5f;
    notify(std::string(combatModeName(save_.huntMode)) + " 모드로 사냥을 시작합니다.", kGood, 2.5f);
}

void Game::confirmAutoWarning() {
    if (screen_ != Screen::AutoWarning) return;
    screen_ = Screen::Desktop;
    save_.huntMode = CombatMode::Auto;
    saveRequested_ = true;
    if (pendingAutoStart_ && !hunting_) {
        hunting_ = true;
        waveActive_ = false;
        breakTimer_ = 1.5f;
    }
    pendingAutoStart_ = false;
    notify("자동 사냥 중입니다. 자동 사냥에서도 드래곤이 죽을 수 있습니다.", kWarn, 4.f);
}

void Game::stopHunt(const std::string& reason) {
    if (!hunting_) return;
    hunting_ = false;
    waveActive_ = false;
    world_.clearEnemies(true);
    if (!reason.empty()) notify(reason, kWarn, 5.f);
    saveRequested_ = true;
}

void Game::spawnWave() {
    int level = save_.dragon.level;
    int maxCount = std::clamp(1 + level / 2, cfg::kWaveMin, cfg::kWaveMax);
    int count = rng_.range(cfg::kWaveMin, maxCount);
    for (int i = 0; i < count; ++i) {
        int lv = std::max(1, level - rng_.range(0, 1));
        world_.spawnEnemy(EnemyKind::Imp, lv, world_.randomEdgePoint(40.f));
    }
    waveActive_ = true;
}

// ---------------- 탑 ----------------

void Game::openTowerWarning() {
    if (!save_.hasDragon || screen_ != Screen::Desktop) return;
    screen_ = Screen::TowerWarning;
}

void Game::spawnTowerFloor() {
    RectF a = arena();
    float rx = a.right() - 120.f;
    float cy = a.center().y;
    if (towerFloor_ == 1) {
        world_.spawnEnemy(EnemyKind::Imp, 4, {rx, cy - 110.f});
        world_.spawnEnemy(EnemyKind::Imp, 4, {rx, cy + 110.f});
    } else if (towerFloor_ == 2) {
        world_.spawnEnemy(EnemyKind::Imp, 6, {rx, cy - 160.f});
        world_.spawnEnemy(EnemyKind::Imp, 6, {rx - 60.f, cy});
        world_.spawnEnemy(EnemyKind::Imp, 6, {rx, cy + 160.f});
    } else {
        world_.spawnEnemy(EnemyKind::Guardian, 8, {rx - 40.f, cy});
    }
}

void Game::enterTower(CombatMode m) {
    if (screen_ != Screen::TowerWarning || !save_.hasDragon) return;
    if (m == CombatMode::Auto) return;  // 탑에서는 자동 사냥을 쓸 수 없다
    if (hunting_) {
        hunting_ = false;
        waveActive_ = false;
    }
    world_.clearEnemies(false);
    towerMode_ = m;
    towerFloor_ = 1;
    save_.bestFloor = std::max(save_.bestFloor, towerFloor_);
    RectF a = arena();
    world_.setBounds(a);
    world_.healPercent(1.f);
    world_.dragon().pos = {a.x + 120.f, a.center().y};
    spawnTowerFloor();
    screen_ = Screen::Tower;
    saveRequested_ = true;
    notify("탑 1층", 0xFFFFFFFF, 2.f);
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
    notify(towerFloor_ == kTowerFloors ? "탑 3층: 수호자" : "탑 " + std::to_string(towerFloor_) + "층", 0xFFFFFFFF, 2.f);
}

void Game::leaveTower() {
    if (screen_ != Screen::FloorClear && screen_ != Screen::TowerVictory) return;
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

void Game::closeModal() {
    if (screen_ == Screen::AutoWarning || screen_ == Screen::TowerWarning) {
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
    screen_ = Screen::Select;
}

void Game::onDragonDied() {
    std::string place;
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
    save_.hasDragon = false;
    hunting_ = false;
    waveActive_ = false;
    screen_ = Screen::Death;
    saveRequested_ = true;
}

void Game::processEvents(bool autoExp) {
    for (const WorldEvent& ev : world_.takeEvents()) {
        if (ev.type == WorldEventType::EnemySpawned) {
            if (ev.enemyKind == EnemyKind::Imp && !save_.impSeen) { save_.impSeen = true; saveRequested_ = true; }
            if (ev.enemyKind == EnemyKind::Guardian && !save_.guardianSeen) { save_.guardianSeen = true; saveRequested_ = true; }
        } else if (ev.type == WorldEventType::EnemyKilled) {
            if (!save_.hasDragon) continue;
            int oldLevel = save_.dragon.level;
            ExpResult r = grantExp(save_.dragon, ev.exp, autoExp);
            if (r.levelsGained > 0) {
                world_.setDragonLevel(save_.dragon.level);
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

    switch (screen_) {
        case Screen::Select:
        case Screen::Death:
        case Screen::FloorClear:
        case Screen::TowerVictory:
            world_.updateFx(dt);
            break;

        case Screen::Desktop:
        case Screen::AutoWarning:
        case Screen::TowerWarning:
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
                if (hidden) { paused_ = true; pauseReason_ = "다른 앱이 전체화면이라 사냥을 잠시 멈췄습니다."; }
                else if (!inputEnabled) { paused_ = true; pauseReason_ = "게임 입력이 꺼져 사냥을 멈췄습니다. 설정 버튼으로 켜세요."; }
                else if (screen_ != Screen::Desktop) { paused_ = true; pauseReason_ = "창이 열려 있어 사냥을 멈췄습니다."; }
                if (paused_) break;
            }
            // 자동 사냥은 숨겨진 동안에도 진행된다(숨김은 일시정지가 아니다).
            const PlayerInput& use = (!isAuto && inputEnabled) ? in : none;
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
            world_.updateCombat(dt, towerMode_, in);
            world_.updateFx(dt);
            processEvents(false);
            if (screen_ != Screen::Tower) break;
            if (world_.aliveEnemyCount() == 0) {
                if (towerFloor_ >= kTowerFloors) {
                    save_.towerClears++;
                    ExpResult r = grantExp(save_.dragon, cfg::kTowerClearExp, false);
                    if (r.levelsGained > 0) world_.setDragonLevel(save_.dragon.level);
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

    autosaveTimer_ += dt;
    if (autosaveTimer_ >= cfg::kAutosaveInterval) {
        autosaveTimer_ = 0.f;
        if (save_.hasDragon) saveRequested_ = true;
    }
}

}  // namespace dt
