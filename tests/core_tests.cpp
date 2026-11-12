// 핵심 규칙 검증. Windows 없이도 빌드·실행된다.
#include <cstdio>
#include <filesystem>
#include <string>

#include "core/Config.h"
#include "core/Game.h"
#include "core/Progression.h"
#include "core/SaveData.h"
#include "core/World.h"

using namespace dt;

static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (cond) {                                                          \
            ++g_passed;                                                      \
        } else {                                                             \
            ++g_failed;                                                      \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static const RectF kDesk{0.f, 0.f, 1600.f, 900.f};

static void stepGame(Game& g, float seconds, bool hidden, bool input, const PlayerInput& in = {}) {
    const float dt = 1.f / 30.f;
    for (float t = 0.f; t < seconds; t += dt) g.update(dt, in, hidden, input);
}

static void testExpCurve() {
    std::printf("[exp curve]\n");
    CHECK(expToNext(1) == 40);
    CHECK(expToNext(2) == 100);
    CHECK(expToNext(4) == 280);
    CHECK(totalExpToReach(1) == 0);
    CHECK(totalExpToReach(5) == 40 + 100 + 180 + 280);
    CHECK(autoExpCap() == totalExpToReach(cfg::kAutoExpCapLevel));
}

static void testAutoCapClamp() {
    std::printf("[auto exp cap]\n");
    DragonRecord d;
    d.autoExp = autoExpCap() - 10;
    d.level = 4;
    d.exp = 270;
    ExpResult r = grantExp(d, 50, true);
    CHECK(r.granted == 10);           // 초과분은 정확히 잘린다
    CHECK(d.autoExp == autoExpCap());
    CHECK(r.capReached);
    CHECK(d.level == 5);              // 270 + 10 = 280 → 레벨업
    CHECK(d.exp == 0);
    ExpResult r2 = grantExp(d, 30, true);
    CHECK(r2.granted == 0);
    CHECK(d.autoExp == autoExpCap());
    // 수동 경험치는 자동 누적에 포함되지 않는다
    ExpResult r3 = grantExp(d, 30, false);
    CHECK(r3.granted == 30);
    CHECK(d.autoExp == autoExpCap());
    CHECK(d.exp == 30);
}

static void testMultiLevelUp() {
    std::printf("[multi level up]\n");
    DragonRecord d;
    ExpResult r = grantExp(d, 40 + 100 + 180 + 5, false);
    CHECK(r.levelsGained == 3);
    CHECK(d.level == 4);
    CHECK(d.exp == 5);
    DragonRecord dead;
    dead.alive = false;
    CHECK(grantExp(dead, 100, false).granted == 0);
}

static void testSaveRoundTrip() {
    std::printf("[save round trip]\n");
    SaveData s;
    s.nextDragonId = 4;
    s.hasDragon = true;
    s.dragon.id = 3;
    s.dragon.species = Species::Water;
    s.dragon.level = 6;
    s.dragon.exp = 12;
    s.dragon.autoExp = 250;
    s.dragon.hp = 88.5f;
    s.huntMode = CombatMode::Auto;
    s.bestFloor = 2;
    s.towerClears = 1;
    s.speciesSeen[0] = true;
    s.speciesSeen[3] = true;
    s.impSeen = true;
    s.graves.push_back({1, Species::Fire, 4, "tower2"});
    s.graves.push_back({2, Species::Wind, 3, "auto"});

    SaveData l;
    CHECK(deserializeSave(serializeSave(s), l));
    CHECK(l.hasDragon);
    CHECK(l.dragon.id == 3);
    CHECK(l.dragon.species == Species::Water);
    CHECK(l.dragon.level == 6);
    CHECK(l.dragon.exp == 12);
    CHECK(l.dragon.autoExp == 250);
    CHECK(l.dragon.hp > 88.f && l.dragon.hp < 89.f);
    CHECK(l.huntMode == CombatMode::Auto);
    CHECK(l.bestFloor == 2);
    CHECK(l.towerClears == 1);
    CHECK(l.speciesSeen[0] && !l.speciesSeen[1] && l.speciesSeen[3]);
    CHECK(l.impSeen && !l.guardianSeen);
    CHECK(l.graves.size() == 2);
    CHECK(l.graves[0].place == "tower2");
    CHECK(placeLabel("tower2") == "탑 2층");

    // 죽은 개체는 현재 개체로 불러오지 않는다
    s.dragon.alive = false;
    CHECK(deserializeSave(serializeSave(s), l));
    CHECK(!l.hasDragon);

    CHECK(!deserializeSave("garbage", l));

    auto dir = std::filesystem::temp_directory_path() / "dragon_tower_test";
    std::filesystem::remove_all(dir);
    auto path = dir / "save.txt";
    s.dragon.alive = true;
    CHECK(saveToFile(s, path));
    CHECK(saveToFile(s, path));  // 덮어쓰기
    SaveData f;
    CHECK(loadFromFile(path, f));
    CHECK(f.hasDragon && f.dragon.level == 6);
    std::filesystem::remove_all(dir);
}

static void testSelectAndDeath() {
    std::printf("[select, permadeath]\n");
    Game g;
    g.seed(1);
    g.init(SaveData{}, kDesk);
    CHECK(g.screen() == Screen::Select);
    g.chooseSpecies(Species::Fire);
    CHECK(g.screen() == Screen::Desktop);
    CHECK(g.hasDragon());
    CHECK(g.consumeSaveRequest());
    int id = g.dragonRecord().id;

    // 체력 1로 만들고 직접 조작 사냥에서 맞게 둔다
    g.setHuntMode(CombatMode::Direct);
    g.startHunt();
    CHECK(g.hunting());
    Vec2 p = g.world().dragon().pos;
    for (int i = 0; i < 3; ++i) g.world().spawnEnemy(EnemyKind::Imp, 15, p + Vec2{40.f, -40.f + 40.f * i});
    g.world().dragon().hp = 1.f;
    stepGame(g, 20.f, false, true);
    CHECK(g.screen() == Screen::Death);
    CHECK(!g.hasDragon());
    CHECK(g.consumeSaveRequest());  // 사망은 즉시 저장 요청
    SaveData s = g.snapshotForSave();
    CHECK(!s.hasDragon);
    CHECK(!s.dragon.alive);
    CHECK(!s.graves.empty() && s.graves.back().id == id);

    // 재실행: 죽은 개체로 시작할 수 없다
    SaveData reloaded;
    CHECK(deserializeSave(serializeSave(s), reloaded));
    Game g2;
    g2.init(reloaded, kDesk);
    CHECK(g2.screen() == Screen::Select);
    g2.chooseSpecies(Species::Earth);
    CHECK(g2.dragonRecord().id != id);  // 새 개체

    g.acknowledgeDeath();
    CHECK(g.screen() == Screen::Select);
}

static void testModeRules() {
    std::printf("[mode rules]\n");
    Game g;
    g.seed(2);
    g.init(SaveData{}, kDesk);
    g.chooseSpecies(Species::Earth);
    g.world().dragon().hp = 1e6f;  // 이 테스트에서는 죽지 않게
    CHECK(g.setHuntMode(CombatMode::Direct));
    g.startHunt();
    // 첫 무리가 나올 때까지 진행
    stepGame(g, 2.0f, false, true);
    CHECK(g.waveActive());
    CHECK(!g.canChangeMode());
    CHECK(!g.setHuntMode(CombatMode::SkillManual));
    CHECK(g.huntMode() == CombatMode::Direct);
    g.stopHunt();
    CHECK(g.canChangeMode());
    CHECK(g.setHuntMode(CombatMode::SkillManual));

    // 자동 사냥은 시작 전 경고를 거친다
    g.setHuntMode(CombatMode::Auto);
    CHECK(g.huntMode() == CombatMode::Auto);
    g.startHunt();
    CHECK(g.screen() == Screen::AutoWarning);
    CHECK(!g.hunting());
    g.confirmAutoWarning();
    CHECK(g.hunting());
    CHECK(g.screen() == Screen::Desktop);
}

static void testHiddenRules() {
    std::printf("[hidden / input rules]\n");
    // 수동 모드: 숨겨지면 멈춘다
    Game g;
    g.seed(3);
    g.init(SaveData{}, kDesk);
    g.chooseSpecies(Species::Fire);
    g.setHuntMode(CombatMode::SkillManual);
    g.startHunt();
    stepGame(g, 1.0f, true, true);
    CHECK(g.combatPaused());
    CHECK(!g.waveActive());  // 시간이 흐르지 않아 무리가 나오지 않음
    stepGame(g, 1.0f, false, false);
    CHECK(g.combatPaused());  // 입력이 꺼져도 멈춤

    // 자동 모드: 숨겨져도 진행된다
    Game a;
    a.seed(4);
    a.init(SaveData{}, kDesk);
    a.chooseSpecies(Species::Earth);
    a.world().dragon().hp = 1e6f;
    a.setHuntMode(CombatMode::Auto);
    a.startHunt();
    a.confirmAutoWarning();
    stepGame(a, 30.f, true, false);
    CHECK(!a.combatPaused());
    CHECK(a.dragonRecord().autoExp > 0);
}

static void testAutoStopsAtCap() {
    std::printf("[auto stops at cap]\n");
    SaveData s;
    s.hasDragon = true;
    s.nextDragonId = 2;
    s.dragon.id = 1;
    s.dragon.species = Species::Earth;
    s.dragon.level = 4;
    s.dragon.exp = 0;
    s.dragon.autoExp = autoExpCap() - 5;
    s.huntMode = CombatMode::Auto;
    Game g;
    g.seed(5);
    g.init(s, kDesk);
    g.world().dragon().hp = 1e6f;
    g.startHunt();
    g.confirmAutoWarning();
    CHECK(g.hunting());
    stepGame(g, 120.f, false, false);
    CHECK(!g.hunting());
    CHECK(g.dragonRecord().autoExp == autoExpCap());
    CHECK(g.dragonRecord().exp + totalExpToReach(g.dragonRecord().level) == totalExpToReach(4) + 5);
    // 상한 이후에는 자동 사냥을 시작할 수 없다
    g.startHunt();
    CHECK(!g.hunting());
    CHECK(g.screen() == Screen::Desktop);
    CHECK(g.setHuntMode(CombatMode::SkillManual));
    CHECK(!g.setHuntMode(CombatMode::Auto));
    CHECK(g.huntMode() == CombatMode::SkillManual);
}

static void testTowerFlow() {
    std::printf("[tower flow]\n");
    SaveData s;
    s.hasDragon = true;
    s.nextDragonId = 2;
    s.dragon.id = 1;
    s.dragon.species = Species::Fire;
    s.dragon.level = 25;
    Game g;
    g.seed(6);
    g.init(s, kDesk);
    g.openTowerWarning();
    CHECK(g.screen() == Screen::TowerWarning);
    g.enterTower(CombatMode::Auto);  // 자동은 거부
    CHECK(g.screen() == Screen::TowerWarning);
    g.enterTower(CombatMode::SkillManual);
    CHECK(g.screen() == Screen::Tower);
    CHECK(g.towerFloor() == 1);
    int guard = 0;
    while (g.screen() != Screen::TowerVictory && g.screen() != Screen::Death && guard++ < 20) {
        PlayerInput in;
        in.skill[0] = in.skill[1] = in.skill[2] = true;
        stepGame(g, 30.f, false, true, in);
        if (g.screen() == Screen::FloorClear) g.nextFloor();
    }
    CHECK(g.screen() == Screen::TowerVictory);
    CHECK(g.save().bestFloor == 3);
    CHECK(g.save().towerClears == 1);
    CHECK(g.save().guardianSeen);
    g.leaveTower();
    CHECK(g.screen() == Screen::Desktop);

    // 입력이 꺼지면 탑 전투는 멈춘다
    g.openTowerWarning();
    g.enterTower(CombatMode::Direct);
    stepGame(g, 1.f, false, false);
    CHECK(g.combatPaused());
}

static void testDirectDodge() {
    std::printf("[direct dodge invulnerability]\n");
    World w;
    w.seed(7);
    w.setBounds(kDesk);
    w.setupDragon(Species::Fire, 1, -1.f, {800.f, 450.f});
    Enemy& e = w.spawnEnemy(EnemyKind::Imp, 3, {850.f, 450.f});
    e.attackCd = 0.f;
    PlayerInput none;
    w.updateCombat(0.01f, CombatMode::Direct, none);  // 임프가 공격 예고
    CHECK(!w.zones().empty());
    float hp = w.dragon().hp;
    // 판정 직전에 회피(무적)
    for (int i = 0; i < 20; ++i) w.updateCombat(0.04f, CombatMode::Direct, none);
    PlayerInput dodge;
    dodge.dodge = true;
    w.updateCombat(0.01f, CombatMode::Direct, dodge);
    for (int i = 0; i < 5; ++i) w.updateCombat(0.03f, CombatMode::Direct, none);
    CHECK(w.statDodges >= 1);
    CHECK(w.dragon().hp >= hp - 0.01f + 0.f || w.statHitsTaken == 0);
}

int main() {
    testExpCurve();
    testAutoCapClamp();
    testMultiLevelUp();
    testSaveRoundTrip();
    testSelectAndDeath();
    testModeRules();
    testHiddenRules();
    testAutoStopsAtCap();
    testTowerFlow();
    testDirectDodge();
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
