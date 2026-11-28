#include "core/CarpetBombardment.h"
// 핵심 규칙 검증. Windows 없이도 빌드·실행된다.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/Config.h"
#include "core/DragonData.h"
#include "core/EnemyData.h"
#include "core/FrameStats.h"
#include "core/Economy.h"
#include "core/Game.h"
#include "core/Progression.h"
#include "core/SaveData.h"
#include "core/TowerData.h"
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

// 루프 안에서 매 프레임 검사할 때 쓴다. 통과는 세지 않고, 실패하면 한 번만 알린다.
static bool g_silentFailed = false;
#define CHECK_SILENT(cond)                                                       \
    do {                                                                         \
        if (!(cond) && !g_silentFailed) {                                        \
            g_silentFailed = true;                                               \
            ++g_failed;                                                          \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                        \
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
    // v0.5: 자동 전투 경험치 상한은 꺼져 있다(기획서 10장). 켜면 아래 값이 된다.
    CHECK(!cfg::kAutoExpCapEnabled);
    CHECK(autoExpCap() == kNoAutoExpCap);
    setAutoExpCapOverride(totalExpToReach(cfg::kAutoExpCapLevel));
    CHECK(autoExpCap() == totalExpToReach(cfg::kAutoExpCapLevel));
    setAutoExpCapOverride(-1);
}

// 상한을 끈 지금도 자동 전투는 제한 없이 경험치를 준다
static void testNoAutoCap() {
    std::printf("[auto exp uncapped]\n");
    DragonRecord d;
    d.level = 1;
    ExpResult r = grantExp(d, 100000, true);
    CHECK(r.granted == 100000);
    CHECK(!r.capReached);
    CHECK(d.level > 5);  // 상한이 없으니 레벨 5를 넘어간다
}

// 상한을 다시 켤 때를 대비해 자르는 계산 자체는 계속 검증한다
static void testAutoCapClamp() {
    std::printf("[auto exp cap (켠 상태)]\n");
    setAutoExpCapOverride(totalExpToReach(cfg::kAutoExpCapLevel));
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
    setAutoExpCapOverride(-1);
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

// 탑 100층 T2(2026-11-18): 드래곤별 층·돌파 기록, 크레딧, 필드 보스·첫 돌파 기록 저장.
static void testTowerSaveKeys() {
    std::printf("[tower save keys]\n");
    SaveData s;
    s.nextDragonId = 3;
    DragonRecord a;
    a.id = 1; a.species = Species::Fire; a.level = 30; a.floor = 23; a.cleared = 22;
    DragonRecord b;
    b.id = 2; b.species = Species::Water; b.level = 5; b.floor = 11; b.cleared = 9;
    s.stable = {a, b};
    s.activeIndex = 1;
    s.dragon = b;
    s.hasDragon = true;
    s.credits = 123456789012LL;
    s.firstClear = 22;
    s.fieldBossCleared = {10, 20};

    SaveData l;
    CHECK(deserializeSave(serializeSave(s), l));
    CHECK(l.stable.size() == 2);
    CHECK(l.stable[0].floor == 23 && l.stable[0].cleared == 22);
    CHECK(l.stable[1].floor == 11 && l.stable[1].cleared == 9);
    CHECK(l.dragon.floor == 11 && l.dragon.cleared == 9);  // 지금 개체는 보유 목록에서 복원된다
    CHECK(l.credits == 123456789012LL);
    CHECK(l.firstClear == 22);
    CHECK(l.fieldBossCleared == std::set<int>({10, 20}));
    {
        SaveData l2;  // 한 번 읽은 것을 다시 저장·읽어도 같다(읽을 때 장착 칸 채우기 등은 첫 번에 끝난다)
        CHECK(deserializeSave(serializeSave(l), l2));
        CHECK(serializeSave(l2) == serializeSave(l));
    }

    // 슬롯: 기본 4 + 깬 20의 배수 필드 보스층 수
    CHECK(l.stableCapacity() == cfg::kMaxStable + 1);  // 20층만 해당
    CHECK(stableCapacityFor({}) == cfg::kMaxStable);
    CHECK(stableCapacityFor({10, 30, 50}) == cfg::kMaxStable);
    CHECK(stableCapacityFor({20, 40, 60, 80, 100}) == cfg::kMaxStableTotal);

    // 옛 저장 파일(새 키 없음): 기본값으로 읽힌다
    const std::string old =
        "version=1\nnextDragonId=2\ndragon.present=1\ndragon.id=1\ndragon.species=fire\ndragon.alive=1\n"
        "dragon.level=7\nstable.count=1\nstable.active=0\nstable.0.id=1\nstable.0.species=fire\n"
        "stable.0.alive=1\nstable.0.level=7\n";
    SaveData o;
    CHECK(deserializeSave(old, o));
    CHECK(o.dragon.floor == 1 && o.dragon.cleared == 0);
    CHECK(o.stable.size() == 1 && o.stable[0].floor == 1);
    CHECK(o.credits == 0 && o.firstClear == 0 && o.fieldBossCleared.empty());

    // 손으로 고친 이상한 값은 범위로 자른다
    const std::string bad =
        "version=1\nstable.count=1\nstable.active=0\nstable.0.id=1\nstable.0.species=fire\nstable.0.alive=1\n"
        "stable.0.floor=999\nstable.0.cleared=-5\ncredits=-100\ntower.firstClear=500\n"
        "tower.fieldBossCleared=10,15,21,abc,200,40\n";
    SaveData x;
    CHECK(deserializeSave(bad, x));
    CHECK(x.stable[0].floor == cfg::kTowerTopFloor && x.stable[0].cleared == 0);
    CHECK(x.credits == 0 && x.firstClear == cfg::kTowerTopFloor);
    CHECK(x.fieldBossCleared == std::set<int>({10, 40}));  // 필드 보스층만 남는다
    SaveData huge;
    CHECK(deserializeSave("version=1\ncredits=99999999999999999\n", huge));
    CHECK(huge.credits == cfg::kMaxCredits);

    // 슬롯이 늘어난 저장(5마리)도 잘리지 않는다
    std::string five = "version=1\nstable.count=5\nstable.active=0\ntower.fieldBossCleared=20\n";
    for (int i = 0; i < 5; ++i) {
        const std::string pre = "stable." + std::to_string(i) + ".";
        five += pre;
        five += "id=" + std::to_string(i + 1) + "\n";
        five += pre;
        five += "species=fire\n";
        five += pre;
        five += "alive=1\n";
    }
    SaveData f;
    CHECK(deserializeSave(five, f));
    CHECK(f.stable.size() == 5);
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
    s.markSeen(Species::Fire);
    s.markSeen(Species::Water);
    s.enemiesSeen.insert(enemyDef(defaultMelee()).key);
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
    CHECK(l.sawSpecies(Species::Fire) && !l.sawSpecies(Species::Earth) && l.sawSpecies(Species::Water));
    CHECK(l.sawEnemy(defaultMelee()) && !l.sawEnemy(defaultBoss()));
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
    for (int i = 0; i < 3; ++i) g.world().spawnEnemy(defaultMelee(), 15, p + Vec2{40.f, -40.f + 40.f * i});
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
    setAutoExpCapOverride(totalExpToReach(cfg::kAutoExpCapLevel));
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
    setAutoExpCapOverride(-1);
}


// 탑 100층 T6(2026-11-24): 크레딧 계산, 상점, 알, 탑 물약 가지고 들어가기·마시기·돌려받기.
static void testEconomy() {
    std::printf("[economy]\n");
    // 처치 크레딧: 2 + 레벨÷5, 정예 ×3, 자동 ×0.7(버림, 최소 1)
    CHECK(creditsForKill(1, false, false) == 2);
    CHECK(creditsForKill(10, false, false) == 4);
    CHECK(creditsForKill(10, true, false) == 12);
    CHECK(creditsForKill(1, false, true) == 1);
    CHECK(creditsForKill(500, false, false) == 102);
    CHECK(isEliteEnemyKey("elite-lich") && !isEliteEnemyKey("lich"));
    // 층 돌파: 일반 ×10, 중간 보스 ×30, 필드 보스(파티) ×100, 단독 필드 보스는 일반으로
    CHECK(creditsForFloorClear(3, true, false, false) == 30);
    CHECK(creditsForFloorClear(5, true, false, false) == 150);
    CHECK(creditsForFloorClear(10, true, false, true) == 1000);
    CHECK(creditsForFloorClear(10, true, false, false) == 100);
    CHECK(creditsForFloorClear(5, false, false, false) == 30);  // 계정이 이미 깬 층 20%
    CHECK(creditsForFloorClear(5, true, true, false) == 0);     // 이 드래곤이 이미 깬 층
    CHECK(eggPrice(1) == 3000 && eggPrice(2) == 4000 && eggPrice(3) == 5000);

    // 가방 저장 왕복과 잘못된 값
    {
        SaveData a;
        a.potionSmall = 3; a.potionLarge = 1; a.eggs = 2;
        SaveData b;
        CHECK(deserializeSave(serializeSave(a), b));
        CHECK(b.potionSmall == 3 && b.potionLarge == 1 && b.eggs == 2);
        SaveData c;
        CHECK(deserializeSave("version=1\ninv.potionSmall=-5\ninv.eggs=999999\n", c));
        CHECK(c.potionSmall == 0 && c.eggs == cfg::kMaxInventory);
    }

    SaveData s;
    s.hasDragon = true;
    s.nextDragonId = 2;
    s.dragon.id = 1;
    s.dragon.species = Species::Fire;
    s.dragon.level = 25;
    s.dragon.alive = true;
    s.stable.push_back(s.dragon);
    s.activeIndex = 0;
    s.credits = 10000;
    Game g;
    g.seed(11);
    g.init(s, kDesk);

    // 물약·알
    CHECK(g.buy(ShopItem::PotionSmall));
    CHECK(g.buy(ShopItem::PotionLarge));
    CHECK(g.save().potionSmall == 1 && g.save().potionLarge == 1);
    CHECK(g.credits() == 10000 - 50 - 150);
    CHECK(g.shopPrice(ShopItem::Egg) == 3000);
    CHECK(g.buy(ShopItem::Egg));
    CHECK(g.stable().size() == 2);
    CHECK(g.stable()[1].level == 1 && g.stable()[1].alive);
    CHECK(g.shopPrice(ShopItem::Egg) == 4000);
    CHECK(g.credits() == 10000 - 200 - 3000);
    CHECK(g.buy(ShopItem::Egg));                            // 6800 → 2800
    CHECK(g.shopBlockedReason(ShopItem::Egg) != nullptr);   // 5000 이 모자란다
    // 칸이 차면 알을 못 산다
    SaveData full = g.snapshotForSave();
    while (full.stable.size() < 4) { DragonRecord d = full.stable[0]; d.id = full.nextDragonId++; full.stable.push_back(d); }
    full.credits = 100000;
    Game gf;
    gf.init(full, kDesk);
    CHECK(gf.stableFull());
    CHECK(!gf.buy(ShopItem::Egg));
    CHECK(gf.stable().size() == 4);
    // 필드 보스 20층을 깬 계정은 칸이 하나 늘어난다
    full.fieldBossCleared.insert(20);
    Game gb;
    gb.init(full, kDesk);
    CHECK(!gb.stableFull());
    CHECK(gb.buy(ShopItem::Egg));
    CHECK(gb.stable().size() == 5);

    // 탑 물약: 가방의 물약을 모두 가지고 들어간다(제한 없음)
    SaveData t = s;
    t.potionSmall = 5;
    t.potionLarge = 0;
    Game gt;
    gt.seed(3);
    gt.init(t, kDesk);
    gt.openTowerWarning();
    gt.enterTower(CombatMode::SkillManual);
    CHECK(gt.screen() == Screen::Tower);
    CHECK(gt.carrySmall() == 5 && gt.carryLarge() == 0);
    CHECK(gt.save().potionSmall == 0);
    CHECK(gt.snapshotForSave().potionSmall == 5);  // 탑 안에서 꺼져도 잃지 않는다
    CHECK(!gt.buy(ShopItem::PotionSmall));         // 탑 안에서는 상점 불가
    // 마시기: 1초 뒤 회복, 그동안 느려진다
    gt.world().dragon().hp = gt.world().dragon().stats.maxHp * 0.2f;
    CHECK(gt.drinkPotion(PotionKind::Small));
    CHECK(!gt.drinkPotion(PotionKind::Small));  // 마시는 중에는 또 못 마신다
    CHECK(gt.carrySmall() == 4);
    PlayerInput none;
    gt.update(0.5f, none, false, true);
    CHECK(gt.world().dragon().moveScale < 1.f);
    const float mid = gt.world().dragon().hp;
    gt.update(0.6f, none, false, true);
    if (gt.screen() == Screen::Tower) {
        CHECK(gt.world().dragon().hp > mid);
        CHECK(gt.world().dragon().moveScale == 1.f);
    }
    // 바탕화면에서는 마실 수 없다
    CHECK(!g.drinkPotion(PotionKind::Small));

    // 분노·수호·신속의 물약: 마시고 1초 뒤 효과가 켜진다
    {
        SaveData b = s;
        b.potionFury = 1; b.potionGuard = 1; b.potionSwift = 1;
        Game gb2;
        gb2.seed(4);
        gb2.init(b, kDesk);
        gb2.openTowerWarning();
        gb2.enterTower(CombatMode::SkillManual);
        CHECK(gb2.carried(PotionKind::Fury) == 1 && gb2.save().potionFury == 0);
        const float atk0 = gb2.world().dragon().attack();
        PlayerInput idle;
        CHECK(gb2.drinkPotion(PotionKind::Fury));
        gb2.update(1.05f, idle, false, true);
        if (gb2.screen() == Screen::Tower) {
            CHECK(gb2.world().dragon().furyTime > 0.f);
            CHECK(gb2.world().dragon().attack() > atk0 * 1.2f);
            CHECK(gb2.drinkPotion(PotionKind::Swift));
            gb2.update(1.05f, idle, false, true);
            CHECK(gb2.world().dragon().swiftTime > 0.f);
            CHECK(gb2.drinkPotion(PotionKind::Guard));
            gb2.update(1.05f, idle, false, true);
            CHECK(gb2.world().dragon().guardTime > 0.f);
        }
        SaveData rt;
        CHECK(deserializeSave(serializeSave(b), rt));
        CHECK(rt.potionFury == 1 && rt.potionGuard == 1 && rt.potionSwift == 1);
    }
    // 사냥꾼의 부적: 실제 시간 30분, 사면 이어 붙는다
    {
        Game gc;
        gc.init(s, kDesk);
        gc.setClockForTest(1000000);
        CHECK(gc.hunterCharmLeft() == 0);
        CHECK(gc.buy(ShopItem::HunterCharm));
        CHECK(gc.hunterCharmLeft() == cfg::kHunterCharmSeconds);
        CHECK(gc.buy(ShopItem::HunterCharm));
        CHECK(gc.hunterCharmLeft() == 2 * cfg::kHunterCharmSeconds);
        gc.setClockForTest(1000000 + 2 * cfg::kHunterCharmSeconds + 5);
        CHECK(gc.hunterCharmLeft() == 0);
        SaveData rt;
        CHECK(deserializeSave(serializeSave(gc.snapshotForSave()), rt));
        CHECK(rt.hunterCharmUntil == 1000000 + 2 * cfg::kHunterCharmSeconds);
    }

    // 바탕화면 처치는 크레딧을 준다
    SaveData h = s;
    h.credits = 0;
    h.huntMode = CombatMode::SkillManual;
    Game gh;
    gh.seed(5);
    gh.init(h, kDesk);
    gh.startHunt();
    PlayerInput in;
    in.skill[0] = in.skill[1] = true;
    for (int i = 0; i < 30 * 60 && gh.credits() == 0 && gh.screen() == Screen::Desktop; ++i) gh.update(1.f / 30.f, in, false, true);
    CHECK(gh.credits() > 0);

    // 처음 4마리만 공짜. 그 뒤는 알로만. 모두 죽으면 한 마리 공짜(2026-11-24)
    {
        Game gp;
        gp.init(SaveData{}, kDesk);
        CHECK(gp.freeDragonAvailable() && gp.freePicksLeft() == 4);
        gp.chooseSpecies(Species::Fire);
        for (int i = 0; i < 3; ++i) {
            gp.openStable();
            gp.beginAddDragon();
            gp.chooseSpecies(Species::Fire);
        }
        CHECK(gp.stable().size() == 4 && gp.freePicksLeft() == 0);
        CHECK(!gp.freeDragonAvailable());
        // 칸이 늘어도 공짜는 없다
        SaveData more = gp.snapshotForSave();
        more.fieldBossCleared.insert(20);
        Game gm;
        gm.init(more, kDesk);
        CHECK(!gm.stableFull() && !gm.freeDragonAvailable());
        gm.openStable();
        gm.beginAddDragon();
        CHECK(gm.screen() == Screen::Stable);  // 선택 화면으로 가지 않는다
        // 모두 죽은 저장: 한 마리 공짜
        SaveData dead;
        dead.freeDragonsUsed = 4;
        GraveEntry gr; gr.id = 1; dead.graves.push_back(gr);
        Game gd;
        gd.init(dead, kDesk);
        CHECK(gd.freeDragonAvailable());
        gd.chooseSpecies(Species::Fire);
        CHECK(gd.hasDragon() && gd.stable().size() == 1);
        CHECK(!gd.freeDragonAvailable());
        // 옛 저장(키 없음)은 가진·잃은 수만큼 이미 쓴 것으로 친다
        SaveData old;
        CHECK(deserializeSave("version=1\nstable.count=0\n", old));
        CHECK(old.freeDragonsUsed == 0);
        SaveData rt;
        CHECK(deserializeSave(serializeSave(more), rt));
        CHECK(rt.freeDragonsUsed == 4);
    }
}


// 탑 100층 T7(2026-11-24): 필드 보스는 4마리 파티만. 동료도 경험치·돌파 기록을 받는다.
// 조작 드래곤이 쓰러지면 다음 동료로 넘어간다. 첫 클리어 특별 보상은 계정에 한 번.
static void testFieldBossParty() {
    std::printf("[field boss party]\n");
    EnemyKind slime{};
    CHECK(enemyFromKey("slime", slime));
    std::vector<TowerFloorDef> floors(20);
    for (auto& f : floors) f.spawns = {{"slime", 1, 1, 0.f}};
    floors[19].spawns = {{"slime", 1, 1, 0.f}};
    const auto makeSave = [](int lastLevel) {
        SaveData s;
        s.nextDragonId = 10;
        for (int i = 0; i < 4; ++i) {
            DragonRecord d;
            d.id = i + 1;
            d.species = Species::Fire;
            d.level = i == 3 ? lastLevel : 40;
            d.alive = true;
            s.stable.push_back(d);
        }
        s.activeIndex = 0;
        s.dragon = s.stable[0];
        s.hasDragon = true;
        s.freeDragonsUsed = 4;
        return s;
    };
    {
        // 레벨 상한(10층 = 50) 넘는 드래곤이 있으면 못 들어간다
        Game g;
        g.setTowerFloors(floors);
        g.init(makeSave(51), kDesk);
        g.openTowerSelection();
        g.selectTowerFloor(10);
        g.openTowerWarning();
        CHECK(g.fieldBossFloorSelected());
        for (int i = 1; i < 4; ++i) g.togglePartyMember(i);
        CHECK(g.partyPick().size() == 4);
        CHECK(g.partyBlockedReason() != nullptr);
        g.enterTower(CombatMode::SkillManual);
        CHECK(g.screen() == Screen::TowerWarning);
    }
    Game g;
    g.seed(21);
    g.setTowerFloors(floors);
    g.init(makeSave(40), kDesk);
    g.openTowerSelection();
    g.selectTowerFloor(10);
    g.openTowerWarning();
    CHECK(g.partyBlockedReason() != nullptr);  // 혼자서는 못 들어간다
    g.enterTower(CombatMode::SkillManual);
    CHECK(g.screen() == Screen::TowerWarning);
    g.togglePartyMember(0);  // 조작 드래곤은 빠지지 않는다
    CHECK(g.partyPicked(1));
    for (int i = 1; i < 4; ++i) g.togglePartyMember(i);
    CHECK(g.partyBlockedReason() == nullptr);
    g.enterTower(CombatMode::SkillManual);
    CHECK(g.screen() == Screen::Tower);
    CHECK(g.partyRun() && g.world().party().size() == 3);
    // 필드 보스층 적은 체력이 kFieldBossHpMul 배
    CHECK(!g.world().enemies().empty());
    if (!g.world().enemies().empty()) {
        const Enemy& e = g.world().enemies().front();
        CHECK(e.maxHp >= enemyStatsFor(slime, 1).maxHp * static_cast<float>(cfg::kFieldBossHpMul) - 0.5f);
    }
    const int exp2 = g.stable()[1].exp;
    PlayerInput in;
    in.skill[0] = in.skill[1] = in.skill[2] = true;
    for (int i = 0; i < 30 * 120 && g.screen() == Screen::Tower; ++i) g.update(1.f / 30.f, in, false, true);
    CHECK(g.screen() == Screen::FloorClear);
    if (g.screen() == Screen::FloorClear) {
        CHECK(g.save().fieldBossCleared.count(10) == 1);
        CHECK(g.save().potionSmall == cfg::kFieldBossFirstPotions);
        bool allyCleared = true;
        for (const DragonRecord& d : g.stable()) allyCleared = allyCleared && d.clearedFloors.count(10) == 1;
        CHECK(allyCleared);
        CHECK(g.stable()[1].exp > exp2 || g.stable()[1].level > 40);  // 동료도 경험치
        // 다음 층으로 가면 파티가 풀리고 혼자 오른다
        g.nextFloor();
        CHECK(!g.partyRun() && g.world().party().empty());
    }

    // 조작 드래곤이 쓰러지면 다음 동료로. 영구 사망은 그대로(목록에서 빠지고 묘비)
    {
        Game gd;
        gd.seed(5);
        std::vector<TowerFloorDef> hard = floors;
        hard[9].spawns = {{"slime", 1, 1, 0.f}};
        gd.setTowerFloors(hard);
        gd.init(makeSave(40), kDesk);
        gd.openTowerSelection();
        gd.selectTowerFloor(10);
        gd.openTowerWarning();
        for (int i = 1; i < 4; ++i) gd.togglePartyMember(i);
        gd.enterTower(CombatMode::SkillManual);
        CHECK(gd.partyRun());
        // 조작 드래곤에게 큰 피해를 직접 준다(예고 판정과 같은 길)
        World& w = gd.world();
        w.dragon().hp = 1.f;
        w.dragon().invuln = 0.f;
        AttackZone z;
        z.shape = ZoneShape::Circle;
        z.a = w.dragon().pos;
        z.radius = 5.f;
        z.damage = 9999.f;
        z.windup = 0.f;
        w.debugResolveZone(z, CombatMode::Direct);
        PlayerInput none;
        gd.update(1.f / 30.f, none, false, true);
        CHECK(gd.screen() == Screen::Tower);
        CHECK(gd.hasDragon() && gd.dragonRecord().id != 1);
        CHECK(gd.stable().size() == 3);
        CHECK(!gd.save().graves.empty() && gd.save().graves.back().id == 1);
    }
}


// 2026-11-25 10층 필드 보스 슬라임 왕: 체력이 깎일 때마다 분열하고, 분열체가 살아 있으면 본체가 단단해진다.
static void testEnemySplit() {
    std::printf("[enemy split]\n");
    EnemyKind slime{};
    CHECK(enemyFromKey("slime", slime));
    EnemyDef def = enemyDef(slime);   // 실제 slime-king/enemy.txt 와 같은 분열 규칙(그림·수치만 다름)
    def.key = "test-split-king";
    def.role = EnemyRole::Boss;
    def.baseHp = 0.f;
    def.hpGrowth = 60.f;
    def.splitKey = "slime";
    def.splitCount = 2;
    def.splitEvery = 0.10f;
    def.splitLevelDrop = 10;
    def.splitGuard = 0.60f;
    const EnemyKind king = registerOrUpdateEnemy(def);
    const EnemyDef& kd = enemyDef(king);
    World w;
    w.seed(3);
    w.setBounds({0.f, 0.f, 1600.f, 900.f});
    w.setupDragon(Species::Fire, 40, -1.f, {300.f, 450.f});
    Enemy& e0 = w.spawnEnemy(king, 50, {1100.f, 450.f}, false);
    const int kingId = e0.id;
    const float maxHp = e0.maxHp;
    const auto findKing = [&]() -> Enemy* {
        for (Enemy& e : w.enemiesMutable()) if (e.id == kingId) return &e;
        return nullptr;
    };
    // 10% 넘게 깎으면 2마리
    findKing()->def = 0.f;
    w.debugDamageEnemy(*findKing(), maxHp * 0.15f);
    PlayerInput none;
    w.updateCombat(1.f / 30.f, CombatMode::SkillManual, none);
    int children = 0;
    bool hopping = true;
    for (const Enemy& e : w.enemies()) {
        if (e.parentId != kingId) continue;
        ++children;
        hopping = hopping && e.hopping() && distance(e.pos, findKing()->pos) < 30.f;  // 왕 몸에서 튀어나온다
    }
    CHECK(children == 2);
    CHECK(hopping);
    CHECK(findKing()->squashLeft > 0.f);
    // 나는 동안은 맞지 않고, kSplitHopTime 뒤에 착지한다
    for (Enemy& e : w.enemiesMutable()) {
        if (e.parentId != kingId) continue;
        const float hp = e.hp;
        w.debugDamageEnemy(e, 50.f);
        CHECK(e.hp == hp);
        break;
    }
    for (int i = 0; i < 20; ++i) w.updateCombat(1.f / 30.f, CombatMode::SkillManual, none);
    for (const Enemy& e : w.enemies())
        if (e.parentId == kingId) CHECK(!e.hopping() && distance(e.pos, findKing()->pos) > 60.f);
    // 분열체가 있으면 본체 피해가 (1 - split_guard) 배
    Enemy* k = findKing();
    CHECK(k != nullptr);
    if (k) {
        const float before = k->hp;
        k->def = 0.f;
        w.debugDamageEnemy(*k, 100.f);
        const float guarded = before - k->hp;
        for (Enemy& e : w.enemiesMutable()) if (e.parentId == kingId) e.alive = false;
        const float before2 = k->hp;
        w.debugDamageEnemy(*k, 100.f);
        const float open = before2 - k->hp;
        CHECK(guarded < open * (1.f - kd.splitGuard) + 1.f && guarded > 0.f);
    }
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
    CHECK(g.save().sawEnemy(defaultBoss()));
    g.leaveTower();
    CHECK(g.screen() == Screen::Desktop);

    // 입력이 꺼지면 탑 전투는 멈춘다
    g.openTowerWarning();
    g.enterTower(CombatMode::Direct);
    stepGame(g, 1.f, false, false);
    CHECK(g.combatPaused());
}

static void testTowerSelection(const std::filesystem::path& assets) {
    const auto tower = loadTowerFloors(assets / "tower");
    CHECK(tower.found && tower.warnings.empty() && tower.floors.size() == 100);
    if (tower.floors.size() != 100) return;
    for (int floor = 1; floor <= 100; ++floor) {
        const auto& def = tower.floors[floor - 1];
        CHECK(def.boss == (floor % 5 == 0));
        CHECK(!def.spawns.empty());
    }

    SaveData save;
    save.hasDragon = true;
    save.dragon.level = 128;
    save.dragon.cleared = 24;
    save.dragon.clearedFloors.insert(24);
    save.dragon.id = 1;
    DragonRecord alternate = save.dragon;
    alternate.id = 2;
    alternate.level = 35;
    alternate.cleared = 2;
    alternate.clearedFloors = {2};
    // T7(2026-11-24): 필드 보스층(10의 배수)은 4마리 파티만. 100층을 깨려면 4마리가 있어야 한다.
    DragonRecord third = alternate, fourth = alternate;
    third.id = 3;
    fourth.id = 4;
    save.stable = {save.dragon, alternate, third, fourth};
    save.nextDragonId = 5;
    save.activeIndex = 0;
    Game g;
    g.setTowerFloors(tower.floors);
    g.init(save, kDesk);
    g.openTowerSelection();
    CHECK(g.selectedTowerFloor() == 25 && g.towerRecommendedLevel(25) == 125);
    CHECK(g.firstVisibleTowerFloor() <= 25 && g.firstVisibleTowerFloor() + Game::visibleTowerFloors > 25);
    g.scrollTowerFloors(200);
    CHECK(g.firstVisibleTowerFloor() == 96 && g.topVisibleTowerFloor() == 100);
    g.scrollTowerFloors(-200);
    CHECK(g.firstVisibleTowerFloor() == 1 && g.topVisibleTowerFloor() == 5);
    g.selectTowerFloor(10);
    g.openTowerWarning();
    g.closeModal();
    CHECK(g.screen() == Screen::TowerSelect && g.selectedTowerFloor() == 10);
    g.openTowerWarning();
    g.enterTower(CombatMode::Auto);
    CHECK(g.screen() == Screen::TowerWarning);
    g.enterTower(CombatMode::Direct);
    CHECK(g.screen() == Screen::TowerWarning);  // 10층은 필드 보스 — 혼자서는 못 들어간다
    g.closeModal();
    g.selectTowerFloor(15);  // 중간 보스층은 혼자 들어간다
    g.openTowerWarning();
    g.enterTower(CombatMode::Direct);
    CHECK(g.screen() == Screen::Tower && g.towerFloor() == 15);
    CHECK(!g.world().enemies().empty());

    const auto selectAndClear = [&](int floor) {
        g.openTowerSelection();
        g.selectTowerFloor(floor);
        g.openTowerWarning();
        if (g.fieldBossFloorSelected())
            for (int i = 0; i < static_cast<int>(g.stable().size()) && g.partyPick().size() < 4; ++i)
                g.togglePartyMember(i);
        g.enterTower(CombatMode::Direct);
        CHECK(g.towerFloor() == floor);
        g.world().clearEnemies(false);
        g.update(0.f, {}, false, true);
    };
    g.world().clearEnemies(false);
    g.update(0.f, {}, false, true);
    g.leaveTower();
    selectAndClear(45);
    CHECK(g.dragonRecord().clearedFloors.count(45) && !g.dragonRecord().clearedFloors.count(44));
    CHECK(g.save().firstClearFloors.count(45) && !g.save().firstClearFloors.count(44));
    CHECK(g.save().fieldBossCleared.empty());
    g.leaveTower();
    selectAndClear(100);
    CHECK(g.screen() == Screen::TowerVictory && g.towerFinalRewardGranted());
    g.leaveTower();
    g.openTowerSelection();
    CHECK(g.selectedTowerFloor() == 100);
    g.closeModal();
    selectAndClear(100);
    CHECK(g.screen() == Screen::TowerVictory && !g.towerFinalRewardGranted());

    SaveData loaded;
    const bool saved = deserializeSave(serializeSave(g.snapshotForSave()), loaded);
    CHECK(saved && loaded.dragon.clearedFloors.count(45) && !loaded.dragon.clearedFloors.count(44));
    CHECK(loaded.firstClearFloors.count(100) && !loaded.firstClearFloors.count(99));
    SaveData legacy;
    CHECK(deserializeSave("version=1\nhasDragon=1\ndragon.cleared=10\ntower.firstClear=10\n", legacy));
    CHECK(legacy.firstClearFloors.count(10) && !legacy.firstClearFloors.count(11));
    g.leaveTower();
    CHECK(g.switchDragon(1));
    g.openTowerSelection();
    // 이 드래곤은 100층 필드 보스 파티에 동료로 나가 100층 돌파를 함께 받았다(T7). 45층은 혼자 간 기록이라 없다.
    CHECK(g.selectedTowerFloor() == 100 && g.world().dragon().level == 35);
    CHECK(g.dragonRecord().clearedFloors.count(2) && g.dragonRecord().clearedFloors.count(100) &&
          !g.dragonRecord().clearedFloors.count(45));
}

// v1.6: 탑 층을 데이터로 뺐다. 파일이 없으면 v1.5와 똑같이 동작해야 한다 — 그것을 여기서 고정한다.
// assets/dragons 폴더가 없으면 어떻게 되는가.
//
// v1.5에서 내장 4종을 지운 뒤로 종은 전부 이 폴더에서 온다. 그래서 폴더가 없으면
// 드래곤이 0마리인 채로 게임이 켜진다. loadDragonFolders 는 그때 **경고도 없이** 돌아오는데
// (아래가 그것을 고정한다), App::init 은 그 사실을 보고 화면에 알려야 한다.
// 실제로 exe 만 들어 있는 압축을 받은 사람이 "드래곤이 사라졌다"고 했다.
static void testMissingDragonsDirIsSilent() {
    const std::filesystem::path nowhere =
        std::filesystem::temp_directory_path() / "dragon-tower-no-such-dir-12345";
    std::error_code ec;
    std::filesystem::remove_all(nowhere, ec);

    DragonScanResult r = loadDragonFolders(nowhere / "dragons");
    // 폴더가 없으면 조용히 돌아온다 — 새 종도 없고 경고도 없다.
    CHECK(r.newSpecies == 0);
    CHECK(r.warnings.empty());
    // 그러므로 **경고 목록만 보고는 문제를 알 수 없다.** 종 수를 직접 봐야 한다.
    // App::init 이 speciesCount() == 0 을 따로 확인하는 이유가 이것이다.
}

static void testTowerFloorsBuiltin() {
    std::printf("[tower floors: builtin fallback]\n");
    SaveData s;
    s.hasDragon = true;
    s.nextDragonId = 2;
    s.dragon.id = 1;
    s.dragon.species = Species::Fire;
    s.dragon.level = 25;
    Game g;
    g.seed(6);
    g.init(s, kDesk);
    CHECK(g.towerFloorCount() == 3);
    CHECK(g.towerFloorDef(1).title == "1층");
    CHECK(g.towerFloorDef(3).boss);
    // 범위를 벗어난 층을 물으면 가장 가까운 층을 준다(터지지 않는다).
    CHECK(g.towerFloorDef(0).title == "1층");
    CHECK(g.towerFloorDef(99).boss);

    g.openTowerWarning();
    g.enterTower(CombatMode::SkillManual);
    // 1층은 슬라임 둘. 세로로 가운데에서 ±110 — v1.5의 하드코딩과 같은 자리다.
    const std::vector<Enemy>& es = g.world().enemies();
    CHECK(es.size() == 2);
    if (es.size() == 2) {
        float cy = kDesk.center().y;
        float lo = std::min(es[0].pos.y, es[1].pos.y);
        float hi = std::max(es[0].pos.y, es[1].pos.y);
        CHECK(std::fabs(lo - (cy - 110.f)) < 0.5f);
        CHECK(std::fabs(hi - (cy + 110.f)) < 0.5f);
        EnemyKind slime;
        CHECK(enemyFromKey("slime", slime));
        CHECK(es[0].kind == slime && es[1].kind == slime);
    }
}

static void testTowerTextParse() {
    std::printf("[tower floors.txt parse]\n");
    std::string ok =
        "; 주석\n"
        "[floor.1]\n"
        "title=첫 층\n"
        "spacing=90\n"
        "enemy=slime x3 lv2\n"
        "\n"
        "[floor.2]\n"
        "boss=yes\n"
        "enemy=- x1 lv9 inset=40  ; 키를 비우면 보스\n"
        "enemy=goblin lv5\n";
    TowerParseResult r = parseTowerText(ok);
    CHECK(r.warnings.empty());
    CHECK(r.floors.size() == 2);
    if (r.floors.size() == 2) {
        CHECK(r.floors[0].title == "첫 층");
        CHECK(r.floors[0].spacing == 90.f);
        CHECK(!r.floors[0].boss);
        CHECK(r.floors[0].spawns.size() == 1);
        CHECK(r.floors[0].spawns[0].key == "slime");
        CHECK(r.floors[0].spawns[0].count == 3);
        CHECK(r.floors[0].spawns[0].level == 2);
        CHECK(r.floors[1].boss);
        CHECK(r.floors[1].spawns.size() == 2);
        CHECK(r.floors[1].spawns[0].key.empty());
        CHECK(r.floors[1].spawns[0].inset == 40.f);
        // 적지 않은 값은 기본값을 쓴다
        CHECK(r.floors[1].spawns[1].count == 1);
        CHECK(r.floors[1].spawns[1].level == 5);
    }

    // 잘못된 것은 조용히 넘기지 않고 경고로 남는다
    std::string bad =
        "[floor.1]\n"
        "enemy=slime x2 lv3\n"
        "spacing=9999\n"       // 범위 밖 → 맞추고 알림
        "wrongkey=1\n"         // 모르는 키
        "enemy=slime zzz\n"    // 모르는 낱말
        "[floor.3]\n"          // 번호 건너뜀
        "enemy=goblin\n"
        "[floor.2]\n"
        "title=적 없는 층\n";  // enemy= 가 없음 → 층을 뺀다
    TowerParseResult w = parseTowerText(bad);
    CHECK(w.warnings.size() >= 4);
    CHECK(w.floors.size() == 1);  // 1층만 남는다
    if (w.floors.size() == 1) CHECK(w.floors[0].spacing <= 600.f);

    TowerParseResult empty = parseTowerText("");
    CHECK(empty.floors.empty() && empty.warnings.empty());
}

static void testTowerFloorsFromData() {
    std::printf("[tower floors from data]\n");
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "dt_tower_test";
    std::error_code ec;
    fs::remove_all(root, ec);

    // 파일이 없으면 found=false — 부르는 쪽이 내장 구성을 쓴다
    TowerScanResult none = loadTowerFloors(root);
    CHECK(!none.found && none.floors.empty() && none.warnings.empty());

    fs::create_directories(root, ec);
    {
        std::ofstream f(root / "floors.txt");
        for (int i = 1; i <= 5; ++i) {
            f << "[floor." << i << "]\n";
            f << "title=" << i << "층\n";
            if (i == 5) f << "boss=yes\nenemy=- x1 lv6\n";
            else f << "enemy=slime x1 lv1\n";
        }
    }
    TowerScanResult r = loadTowerFloors(root);
    CHECK(r.found);
    CHECK(r.floors.size() == 5);
    CHECK(r.warnings.empty());

    SaveData s;
    s.hasDragon = true;
    s.nextDragonId = 2;
    s.dragon.id = 1;
    s.dragon.species = Species::Fire;
    s.dragon.level = 25;
    Game g;
    g.seed(6);
    g.init(s, kDesk);
    g.setTowerFloors(r.floors);
    CHECK(g.towerFloorCount() == 5);

    // 5층까지 올라가야 정복이다(3층이 아니다)
    g.openTowerWarning();
    g.enterTower(CombatMode::SkillManual);
    int guard = 0;
    while (g.screen() != Screen::TowerVictory && g.screen() != Screen::Death && guard++ < 40) {
        PlayerInput in;
        in.skill[0] = in.skill[1] = in.skill[2] = true;
        stepGame(g, 30.f, false, true, in);
        if (g.screen() == Screen::FloorClear) g.nextFloor();
    }
    CHECK(g.screen() == Screen::TowerVictory);
    CHECK(g.save().bestFloor == 5);

    // 빈 목록을 주면 내장 구성으로 돌아간다
    g.setTowerFloors({});
    CHECK(g.towerFloorCount() == 3);

    fs::remove_all(root, ec);
}

// v1.6: 효과(FX)에 발동·지속·잔류 세 단계와 스프라이트 설정을 더했다.
// fx= 를 적지 않은 스킬은 **보이는 모습이 전과 같아야 한다** — 그것을 여기서 고정한다.
static void testFxPhases() {
    std::printf("[fx phases]\n");
    // 단계 경계. 길이 0.1 / 0.2 / 0.1 → 0.4
    Fx f;
    f.windup = 0.1f;
    f.hold = 0.2f;
    f.tail = 0.1f;
    f.duration = 0.4f;
    f.t = 0.f;
    CHECK(f.phase() == FxPhase::Windup);
    CHECK(f.phaseProgress() == 0.f);
    f.t = 0.05f;
    CHECK(f.phase() == FxPhase::Windup);
    CHECK(std::fabs(f.phaseProgress() - 0.5f) < 0.001f);
    f.t = 0.1f;
    CHECK(f.phase() == FxPhase::Hold);  // 경계는 다음 단계에 든다
    f.t = 0.2f;
    CHECK(f.phase() == FxPhase::Hold);
    CHECK(std::fabs(f.phaseProgress() - 0.5f) < 0.001f);
    f.t = 0.3f;
    CHECK(f.phase() == FxPhase::Tail);
    f.t = 0.39f;
    CHECK(f.phase() == FxPhase::Tail);
    CHECK(f.phaseProgress() > 0.8f);
    // 길이가 0인 단계에서도 터지지 않는다
    Fx z;
    z.windup = z.hold = z.tail = 0.f;
    z.duration = 0.f;
    z.t = 0.f;
    CHECK(z.phase() == FxPhase::Tail);
    CHECK(z.phaseProgress() == 1.f);
}

static void testFxShapeUnchanged() {
    std::printf("[fx: fx= 없는 스킬은 전과 같다]\n");
    World w;
    w.setBounds(kDesk);
    w.setupDragon(Species::Fire, 20, -1.f, {400.f, 500.f});
    w.spawnEnemy(defaultMelee(), 3, {460.f, 500.f});
    // 기존 스킬에는 fx= 가 없다. 도형 효과의 전체 길이가 전과 같아야 한다.
    w.addFx(FxType::Ring, {0.f, 0.f}, {0.f, 0.f}, 50.f, 0.45f, 0xFFFFFFFF);
    CHECK(!w.fx().empty());
    if (!w.fx().empty()) {
        const Fx& f = w.fx().back();
        CHECK(f.duration == 0.45f);        // 길이 그대로
        CHECK(f.key.empty());              // 스프라이트 없음 → 도형으로 그린다
        CHECK(std::fabs(f.windup + f.hold + f.tail - f.duration) < 0.001f);
        CHECK(f.windup > 0.f && f.hold > 0.f && f.tail > 0.f);
    }

    // 효과는 쌓이지 않는다 — 상한을 넘으면 오래된 것부터 버린다
    for (int i = 0; i < cfg::kMaxFx + 20; ++i)
        w.addFx(FxType::Hit, {static_cast<float>(i), 0.f}, {0.f, 0.f}, 10.f, 5.f, 0xFFFFFFFF);
    CHECK(static_cast<int>(w.fx().size()) <= cfg::kMaxFx);
    // 시간이 지나면 사라진다
    for (int i = 0; i < 400; ++i) w.updateFx(1.f / 30.f);
    CHECK(w.fx().empty());
}

static void testFxFromSkillData() {
    std::printf("[fx: dragon.txt 의 fx= 설정]\n");
    std::string text =
        "[dragon]\n"
        "name=효과시험룡\n"
        "[skill.1]\n"
        "name=빛터짐\n"
        "kind=Burst\n"
        "radius=120\n"
        "fx=burst\n"
        "fx_front=no\n"
        "fx_scale=1.8\n"
        "fx_offset_y=-30\n"
        "fx_windup=0.2\n"
        "fx_hold=0.5\n"
        "fx_tail=0.3\n"
        "fx_fps=20\n";
    DragonParseResult r = parseDragonText(text, "fxtest", nullptr);
    CHECK(r.warnings.empty());
    // 새 종 템플릿에 기본 스킬 3개가 있으므로 [skill.1] 은 0번을 덮어쓴다(개수는 그대로 3)
    CHECK(r.def.skills.size() >= 1);
    if (!r.def.skills.empty()) {
        const SkillDef& s = r.def.skills[0];
        CHECK(s.fx == "burst");
        CHECK(!s.fxInFront);
        CHECK(std::fabs(s.fxScale - 1.8f) < 0.001f);
        CHECK(std::fabs(s.fxOffsetY + 30.f) < 0.001f);
        CHECK(std::fabs(s.fxWindup - 0.2f) < 0.001f);
        CHECK(s.fxAnim.fps == 20.f);
        CHECK(!s.fxAnim.loop);  // 효과는 1회 재생
    }

    // 범위 밖 값은 울타리에 맞추고 알린다
    DragonParseResult bad = parseDragonText(
        "[dragon]\nname=x\n[skill.1]\nname=y\nfx=z\nfx_scale=-5\nfx_windup=99\nfx_render_scale=100\n", "fxbad",
        nullptr);
    CHECK(bad.def.skills.size() >= 1);
    if (!bad.def.skills.empty()) {
        CHECK(bad.def.skills[0].fxScale > 0.f);
        CHECK(bad.def.skills[0].fxWindup <= 5.f);
        CHECK(bad.def.skills[0].fxAnim.renderScale <= 4.f);
    }

    // fx= 를 적은 스킬을 쓰면 효과에 그 설정이 실려 나온다
    SpeciesDef def = r.def;
    def.key = "fxtest";
    def.skills[0].unlockLevel = 1;
    Species sp = registerOrUpdateSpecies(def);
    World w;
    w.setBounds(kDesk);
    w.setupDragon(sp, 20, -1.f, {400.f, 500.f});
    w.spawnEnemy(defaultMelee(), 3, {430.f, 500.f});
    w.setSlots({0, -1, -1, -1});
    PlayerInput in;
    in.skill[0] = true;
    w.updateCombat(1.f / 30.f, CombatMode::SkillManual, in);
    // 전투 중에는 피격 등 다른 효과도 생기므로 key로 찾는다
    const Fx* found = nullptr;
    for (const Fx& e : w.fx())
        if (e.key == "burst") found = &e;
    CHECK(found != nullptr);
    if (found) {
        CHECK(!found->inFront);
        CHECK(std::fabs(found->scale - 1.8f) < 0.001f);
        CHECK(std::fabs(found->offsetY + 30.f) < 0.001f);
        // 길이는 적은 세 단계의 합(0.2+0.5+0.3)이다 — 도형의 0.45f 가 아니다
        CHECK(std::fabs(found->duration - 1.0f) < 0.001f);
    }
}

static void testDirectDodge() {
    std::printf("[direct dodge invulnerability]\n");
    World w;
    w.seed(7);
    w.setBounds(kDesk);
    w.setupDragon(Species::Fire, 1, -1.f, {800.f, 450.f});
    Enemy& e = w.spawnEnemy(defaultMelee(), 3, {850.f, 450.f});
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

// ---------------------------------------------------------------------------
// v0.2: 드래곤 데이터 파일과 종 레지스트리
// ---------------------------------------------------------------------------

static void testDragonTextParse() {
    std::printf("[dragon.txt parse]\n");
    // 내장 종 덮어쓰기: 적은 값만 바뀌고 나머지는 그대로다.
    const SpeciesDef& fire = speciesDef(Species::Fire);
    float oldAtk = fire.baseAtk;
    std::string txt =
        "[dragon]\n"
        "name=붉은 드래곤   ; 줄 끝 주석\n"
        "hp=123.5\n"
        "color=#102030\n"
        "[anim.move]\n"
        "fps=14\n"
        "loop=no\n"
        "frame_w=192\n"
        "[anim.fly]\n"
        "render_scale=1.37\n"
        "[skill.2]\n"
        "kind=Heal\n"
        "power=0.45\n";
    DragonParseResult r = parseDragonText(txt, "fire", &fire);
    CHECK(r.warnings.empty());
    CHECK(r.def.key == "fire");
    CHECK(r.def.name == "붉은 드래곤");
    CHECK(r.def.baseHp > 123.f && r.def.baseHp < 124.f);
    CHECK(r.def.baseAtk == oldAtk);  // 적지 않은 값은 그대로
    CHECK(r.def.color == 0xFF102030);
    CHECK(r.def.anims[static_cast<int>(AnimState::Move)].fps == 14.f);
    CHECK(!r.def.anims[static_cast<int>(AnimState::Move)].loop);
    CHECK(r.def.anims[static_cast<int>(AnimState::Move)].frameW == 192);
    CHECK(r.def.anims[static_cast<int>(AnimState::Fly)].renderScale > 1.36f &&
          r.def.anims[static_cast<int>(AnimState::Fly)].renderScale < 1.38f);
    CHECK(r.def.anims[static_cast<int>(AnimState::Idle)].renderScale == 1.f);  // 적지 않으면 1배
    CHECK(r.def.skills[1].kind == SkillKind::Heal);
    CHECK(r.def.skills[0].name == fire.skills[0].name);  // 건드리지 않은 스킬은 그대로
}

static void testImpactFpsAndCodex() {
    const std::string skill = "[dragon]\nname=FPS\n[skill.1]\nkind=Projectile\nfx=bolt\nfx_fps=14\n";
    for (const auto& [setting, expected] : std::vector<std::pair<std::string, float>>{
             {"", 14.f}, {"0", 14.f}, {"14", 14.f}, {"24", 24.f}, {"1", 1.f}, {"60", 60.f},
             {"-1", 14.f}, {"61", 14.f}, {"abc", 14.f}}) {
        const auto result = parseDragonText(skill + "fx_impact=hit\n" +
            (setting.empty() ? "" : "fx_impact_fps=" + setting + "\n"), "fps", nullptr);
        CHECK(!result.def.skills.empty());
        if (result.def.skills.empty()) continue;
        const auto& parsed = result.def.skills.front();
        CHECK(parsed.fxPlaybackFps("bolt") == 14.f);
        CHECK(parsed.fxPlaybackFps("hit") == expected);
        CHECK(result.warnings.empty() == (setting != "-1" && setting != "61" && setting != "abc"));
    }
    const auto shared = parseDragonText(skill + "fx_impact=bolt\nfx_impact_fps=24\n", "fps", nullptr);
    CHECK(!shared.def.skills.empty() && shared.def.skills.front().fxPlaybackFps("bolt") == 14.f);

    Game game;
    game.init(SaveData{}, kDesk);
    game.openCodex();
    CHECK(game.screen() == Screen::Codex && game.wantsKeyboard());
    game.dismissWindow();
    CHECK(game.screen() == Screen::Select);
    game.chooseSpecies(Species::Fire);
    game.openStable();
    game.beginAddDragon();
    CHECK(game.screen() == Screen::Select && game.addingDragon());
    game.openCodex();
    game.dismissWindow();
    CHECK(game.screen() == Screen::Select && game.addingDragon());
    game.dismissWindow();
    CHECK(game.screen() == Screen::Stable && !game.addingDragon());
    game.dismissWindow();
    CHECK(game.screen() == Screen::Desktop);
}

static void testDragonTextWarnings() {
    std::printf("[dragon.txt warnings]\n");
    std::string txt =
        "[dragon]\n"
        "hpp=100\n"            // 모르는 키
        "atk=빠름\n"            // 읽을 수 없는 값
        "dodge=9.0\n"          // 범위 초과 → 잘림
        "[wings]\n"            // 모르는 구역
        "span=3\n"
        "[skill.9]\n"          // 잘못된 번호
        "name=없음\n";
    DragonParseResult r = parseDragonText(txt, "test", nullptr);
    CHECK(r.warnings.size() >= 4);  // 오타를 조용히 넘기지 않는다
    CHECK(r.def.baseDodge <= 0.75f);
    DragonParseResult r2 = parseDragonText("[anim.fly]\nrender_scale=99\n", "t2", nullptr);
    CHECK(r2.def.anims[static_cast<int>(AnimState::Fly)].renderScale <= 4.f);
    // 모르는 구역의 내용이 [dragon] 값을 건드리지 않는다
    CHECK(r.def.key == "test");
}

static void testSpeciesRegistry() {
    std::printf("[species registry]\n");
    int before = speciesCount();
    CHECK(before >= kBuiltinSpeciesCount);

    SpeciesDef d;
    d.key = "testdragon";
    d.name = "시험 드래곤";
    d.baseHp = 200.f;
    Species added = registerOrUpdateSpecies(d);
    CHECK(speciesCount() == before + 1);
    CHECK(speciesDef(added).name == "시험 드래곤");
    CHECK(statsFor(added, 1).maxHp == 200.f);

    // 같은 키로 다시 등록하면 덮어쓰고 id는 유지된다
    d.name = "시험 드래곤 2";
    Species again = registerOrUpdateSpecies(d);
    CHECK(again == added);
    CHECK(speciesCount() == before + 1);
    CHECK(speciesDef(added).name == "시험 드래곤 2");

    // 등록이 더 일어나도 앞서 얻은 참조가 깨지지 않는다(deque)
    const SpeciesDef& firstRef = speciesDef(static_cast<Species>(0));
    std::string firstKey = firstRef.key;
    SpeciesDef e;
    e.key = "another";
    registerOrUpdateSpecies(e);
    CHECK(firstRef.key == firstKey);

    Species found;
    CHECK(speciesFromKey("testdragon", found) && found == added);
    CHECK(!speciesFromKey("없는키", found));
}

static void testUnknownSpeciesKeepsIdentity() {
    std::printf("[unknown species in save]\n");
    // 폴더가 사라진 종의 개체도 다른 종으로 바뀌지 않는다.
    std::string text =
        "version=1\nnextDragonId=9\ndragon.present=1\ndragon.id=8\n"
        "dragon.species=ghostwyrm\ndragon.alive=1\ndragon.level=3\n"
        "codex.species=fire,ghostwyrm,longgonekey\n";
    SaveData l;
    CHECK(deserializeSave(text, l));
    CHECK(l.hasDragon);
    CHECK(speciesDef(l.dragon.species).key == "ghostwyrm");
    // 모르는 도감 키도 보관해 다시 넣으면 되살아난다
    CHECK(l.speciesSeen.count("longgonekey") == 1);
    CHECK(serializeSave(l).find("longgonekey") != std::string::npos);
}

static void testAnimStates() {
    std::printf("[anim states]\n");
    CHECK(animFrame(0.f, 8.f, 4, true) == 0);
    CHECK(animFrame(0.3f, 8.f, 4, true) == 2);
    CHECK(animFrame(1.0f, 8.f, 4, true) == 0);   // 8fps 4칸 → 0.5초마다 한 바퀴
    CHECK(animFrame(5.0f, 8.f, 4, false) == 3);  // 1회 재생은 마지막에서 멈춘다
    CHECK(animFrame(1.f, 8.f, 1, true) == 0);
    CHECK(animFrame(-1.f, 8.f, 4, true) == 0);

    World w;
    w.setBounds({0.f, 0.f, 800.f, 600.f});
    w.setupDragon(Species::Fire, 1, -1.f, {400.f, 300.f});
    CHECK(w.dragon().anim == AnimState::Idle);

    // 사거리 안의 적을 치면 공격 상태로 바뀐다(기본 공격은 첫 프레임에 나간다)
    w.spawnEnemy(defaultMelee(), 1, {430.f, 300.f});
    PlayerInput in;
    w.updateCombat(1.f / 30.f, CombatMode::SkillManual, in);
    CHECK(w.dragon().anim == AnimState::Attack);

    // 공격 상태는 유지 시간(kAnimAttackHold)이 지나면 저절로 풀린다.
    // 적이 없으면 자동 행동으로 넘어가므로 Fly도 나올 수 있다.
    w.clearEnemies(false);
    for (int i = 0; i < 30; ++i) w.updateCombat(1.f / 30.f, CombatMode::SkillManual, in);
    CHECK(w.dragon().anim == AnimState::Idle || w.dragon().anim == AnimState::Move ||
          w.dragon().anim == AnimState::Fly);

    // 사망 상태는 다른 상태로 덮이지 않는다
    World w2;
    w2.setBounds({0.f, 0.f, 800.f, 600.f});
    w2.setupDragon(Species::Fire, 1, 1.f, {400.f, 300.f});
    w2.dragon().hp = 0.f;
    w2.dragon().alive = false;
    w2.updateCombat(1.f / 30.f, CombatMode::SkillManual, in);
    CHECK(w2.dragon().anim == AnimState::Death);
    w2.setDragonAnim(AnimState::Move, 1.f);
    CHECK(w2.dragon().anim == AnimState::Death);

    // 배회만 할 때도 애니메이션 시간이 한 번만 흐른다(두 번 흐르면 1.0이 된다)
    World w3;
    w3.setBounds({0.f, 0.f, 800.f, 600.f});
    w3.setupDragon(Species::Fire, 1, -1.f, {400.f, 300.f});
    w3.dragon().behavior = IdleBehavior::Rest;  // 상태가 바뀌지 않게 고정
    w3.dragon().behaviorLeft = 100.f;
    w3.updateWander(0.5f);
    CHECK(w3.dragon().anim == AnimState::Idle);
    CHECK(w3.dragon().animTime > 0.49f && w3.dragon().animTime < 0.51f);
}

// ---------------------------------------------------------------------------
// v0.3: 바탕화면 자동 행동
// ---------------------------------------------------------------------------

static void testIdleBehavior() {
    std::printf("[desktop idle behavior]\n");
    const RectF area{0.f, 0.f, 1600.f, 900.f};

    // 충분히 오래 두면 세 행동이 모두 나온다
    World w;
    w.setBounds(area);
    w.seed(12345u);
    w.setupDragon(Species::Fire, 1, -1.f, {800.f, 700.f});
    bool sawRest = false, sawWalk = false, sawFly = false;
    float topSeenFly = area.bottom(), topSeenWalk = area.bottom();
    for (int i = 0; i < 30 * 600; ++i) {  // 10분
        w.updateWander(1.f / 30.f);
        const DragonActor& d = w.dragon();
        if (d.behavior == IdleBehavior::Rest) sawRest = true;
        if (d.behavior == IdleBehavior::Walk) { sawWalk = true; topSeenWalk = std::min(topSeenWalk, d.pos.y); }
        if (d.behavior == IdleBehavior::Fly) { sawFly = true; topSeenFly = std::min(topSeenFly, d.pos.y); }
        // 어떤 행동이든 화면 밖으로 나가지 않는다
        CHECK_SILENT(d.pos.x >= area.x && d.pos.x <= area.right());
        CHECK_SILENT(d.pos.y >= area.y && d.pos.y <= area.bottom());
    }
    CHECK(sawRest);
    CHECK(sawWalk);
    CHECK(sawFly);
    // 날 때가 걸을 때보다 위로 올라간다
    CHECK(topSeenFly < topSeenWalk);

    // 쉴 때는 제자리, 날 때는 Fly 애니메이션
    World w2;
    w2.setBounds(area);
    w2.setupDragon(Species::Fire, 1, -1.f, {800.f, 700.f});
    w2.dragon().behavior = IdleBehavior::Rest;
    w2.dragon().behaviorLeft = 100.f;
    Vec2 before = w2.dragon().pos;
    for (int i = 0; i < 60; ++i) w2.updateWander(1.f / 30.f);
    CHECK(distance(w2.dragon().pos, before) < 0.01f);
    CHECK(!w2.dragon().flying);
    CHECK(w2.dragon().anim == AnimState::Idle);

    w2.dragon().behavior = IdleBehavior::Fly;
    w2.dragon().behaviorLeft = 100.f;
    w2.dragon().wanderTarget = {200.f, 100.f};
    for (int i = 0; i < 30; ++i) w2.updateWander(1.f / 30.f);
    CHECK(w2.dragon().flying);
    CHECK(w2.dragon().anim == AnimState::Fly);
    CHECK(w2.dragon().pos.y < before.y);  // 위로 올라갔다

    // 전투가 시작되면 날기를 멈춘다(전투에는 Fly를 쓰지 않는다)
    w2.spawnEnemy(defaultMelee(), 1, {820.f, 700.f});
    PlayerInput in;
    w2.updateCombat(1.f / 30.f, CombatMode::SkillManual, in);
    CHECK(!w2.dragon().flying);
    CHECK(w2.dragon().anim != AnimState::Fly);

    // 적이 사라지면 다시 자동 행동으로 돌아간다
    w2.clearEnemies(false);
    for (int i = 0; i < 30 * 40; ++i) w2.updateCombat(1.f / 30.f, CombatMode::SkillManual, in);
    CHECK(w2.dragon().behaviorLeft > 0.f);

    // 걷기는 수평 이동만 한다
    World w4;
    w4.setBounds(area);
    w4.seed(777u);
    w4.setupDragon(Species::Fire, 1, -1.f, {800.f, 700.f});
    float walkedY = -1.f;
    bool everWalked = false;
    for (int i = 0; i < 30 * 600; ++i) {
        w4.updateWander(1.f / 30.f);
        const DragonActor& d = w4.dragon();
        if (d.behavior != IdleBehavior::Walk) { walkedY = -1.f; continue; }
        everWalked = true;
        if (walkedY < 0.f) walkedY = d.pos.y;
        CHECK_SILENT(std::fabs(d.pos.y - walkedY) < 0.01f);  // 걷는 동안 높이가 변하지 않는다
    }
    CHECK(everWalked);
    CHECK(true);  // 위 CHECK_SILENT 가 실패하지 않았음

    // 날다가 걷기로 넘어갈 때는 먼저 내려온다(공중에서 걷지 않는다)
    World w5;
    w5.setBounds(area);
    w5.setupDragon(Species::Fire, 1, -1.f, {800.f, 700.f});
    w5.dragon().behavior = IdleBehavior::Fly;
    w5.dragon().flying = true;
    w5.dragon().behaviorLeft = 0.02f;
    w5.dragon().pos = {800.f, 150.f};  // 공중에 있다
    w5.updateWander(1.f / 30.f);
    CHECK(w5.dragon().landing);                      // 바로 걷지 않고 착지부터
    CHECK(w5.dragon().anim == AnimState::Fly);       // 내려오는 동안에도 나는 모습
    CHECK(w5.dragon().wanderTarget.y > 150.f);       // 아래로 내려간다
    for (int i = 0; i < 30 * 30; ++i) {
        w5.updateWander(1.f / 30.f);
        if (!w5.dragon().landing) break;
    }
    CHECK(!w5.dragon().landing);
    CHECK(std::fabs(w5.dragon().pos.y - w5.dragon().groundY) < 3.f);

    World roaming;
    roaming.setBounds(area);
    roaming.setupDragon(Species::Fire, 1, -1.f, area.center());
    roaming.dragon().behavior = IdleBehavior::Walk;
    roaming.dragon().behaviorLeft = 100.f;
    roaming.dragon().wanderTarget = {area.x + 90.f, area.center().y};
    roaming.sampleRoamCursor(0.f, 10.f);
    roaming.updateWander(0.1f);
    CHECK(roaming.dragon().wanderTarget.x >= area.center().x);
    roaming.sampleRoamCursor(1.f, 0.1f);
    roaming.updateWander(0.1f);
    CHECK(roaming.dragon().wanderTarget.x >= area.center().x);
    roaming.sampleRoamCursor(1.f, 10.f);
    roaming.updateWander(0.1f);
    CHECK(roaming.dragon().wanderTarget.x <= area.center().x);

    // 화면이 아주 낮아도 목적지가 뒤집히지 않는다
    World w3;
    w3.setBounds({0.f, 0.f, 400.f, 120.f});
    w3.setupDragon(Species::Fire, 1, -1.f, {200.f, 60.f});
    for (int i = 0; i < 30 * 60; ++i) {
        w3.updateWander(1.f / 30.f);
        CHECK_SILENT(w3.dragon().pos.y >= 0.f && w3.dragon().pos.y <= 120.f);
    }
    CHECK(true);
}

static void testLoadDragonFolders() {
    std::printf("[dragon folder scan]\n");
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "dt_dragons_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "_skipme", ec);
    fs::create_directories(root / "mossling", ec);
    fs::create_directories(root / "testa", ec);
    fs::create_directories(root / "noTxtHere", ec);  // 대문자: 키 규칙 위반
    {
        std::ofstream f(root / "_skipme" / "dragon.txt");
        f << "[dragon]\nname=읽히면 안 됨\n";
    }
    {
        std::ofstream f(root / "mossling" / "dragon.txt");
        f << "[dragon]\nname=이끼 드래곤\nhp=133\n";
    }
    {
        // 이미 등록된 종의 폴더는 새 종이 아니라 덮어쓰기가 된다(여기서는 testa)
        std::ofstream f(root / "testa" / "dragon.txt");
        f << "[dragon]\nrole=덮어쓴 역할\n";
    }

    int before = speciesCount();
    DragonScanResult r = loadDragonFolders(root);
    CHECK(r.newSpecies == 1);                 // mossling만 새 종
    CHECK(speciesCount() == before + 1);
    CHECK(!r.warnings.empty());               // noTxtHere는 경고

    Species sp;
    CHECK(speciesFromKey("mossling", sp));
    CHECK(speciesDef(sp).name == "이끼 드래곤");
    CHECK(statsFor(sp, 1).maxHp == 133.f);
    {
        // 이미 있던 종의 폴더는 덮어쓰기가 된다(새 종이 아니다)
        Species over;
        CHECK(speciesFromKey("testa", over));
        CHECK(speciesDef(over).role == "덮어쓴 역할");
    }
    CHECK(!speciesFromKey("_skipme", sp));

    // 없는 폴더를 가리켜도 안전하다
    DragonScanResult none = loadDragonFolders(root / std::filesystem::u8path("없음"));
    CHECK(none.folderCount == 0 && none.warnings.empty());
    fs::remove_all(root, ec);
}


// ---------------------------------------------------------------------------
// v0.4: 적 등록부와 enemy.txt
// ---------------------------------------------------------------------------

static void testEnemyRegistry() {
    std::printf("[enemy registry]\n");
    CHECK(enemyCount() >= kBuiltinEnemyCount);
    EnemyKind k;
    CHECK(enemyFromKey("slime", k));
    CHECK(enemyDef(k).role == EnemyRole::Melee);
    CHECK(enemyFromKey("lich", k));
    CHECK(enemyDef(k).role == EnemyRole::Boss);
    CHECK(enemyDef(defaultBoss()).role == EnemyRole::Boss);
    CHECK(enemyDef(defaultMelee()).role == EnemyRole::Melee);

    // 레벨이 오르면 수치가 오른다
    EnemyStats a = enemyStatsFor(defaultMelee(), 1);
    EnemyStats b = enemyStatsFor(defaultMelee(), 5);
    CHECK(b.maxHp > a.maxHp && b.atk > a.atk && b.expReward > a.expReward);

    // 같은 키로 다시 등록하면 덮어쓰고 id는 유지된다
    int before = enemyCount();
    EnemyDef d = enemyDef(defaultMelee());
    d.name = "바뀐 이름";
    EnemyKind again = registerOrUpdateEnemy(d);
    CHECK(again == defaultMelee());
    CHECK(enemyCount() == before);
    CHECK(enemyDef(again).name == "바뀐 이름");
    d.name = enemyDef(defaultMelee()).key == "slime" ? "슬라임" : d.name;
    registerOrUpdateEnemy(d);

    // 모르는 키는 자리 표시로 등록된다
    EnemyKind ghost = ensureEnemy("nosuchenemy");
    CHECK(enemyDef(ghost).key == "nosuchenemy");
    CHECK(enemyCount() == before + 1);
}

static void testEnemyTextParse() {
    std::printf("[enemy.txt parse]\n");
    EnemyKind sl;
    CHECK(enemyFromKey("slime", sl));
    const EnemyDef& base = enemyDef(sl);
    float oldAtk = base.baseAtk;
    std::string txt =
        "[enemy]\n"
        "name=큰 슬라임   ; 주석\n"
        "hp=99\n"
        "role=boss\n"
        "[anim.attack]\n"
        "render_scale=0.81\n";
    EnemyParseResult r = parseEnemyText(txt, "slime", &base);
    CHECK(r.warnings.empty());
    CHECK(r.def.name == "큰 슬라임");
    CHECK(r.def.baseHp == 99.f);
    CHECK(r.def.baseAtk == oldAtk);  // 적지 않은 값은 그대로
    CHECK(r.def.role == EnemyRole::Boss);
    CHECK(r.def.anims[static_cast<int>(AnimState::Attack)].renderScale > 0.8f);

    // 모르는 키·잘못된 값은 경고로 남고 범위는 잘린다
    EnemyParseResult w = parseEnemyText(
        "[enemy]\nhpp=1\nrole=없는역할\nkeep_ratio=9\nwindup=99\n[wings]\na=1\n", "t", nullptr);
    CHECK(w.warnings.size() >= 4);
    CHECK(w.def.keepRatio >= 0.05f && w.def.keepRatio <= 1.f);
    CHECK(w.def.windup <= 5.f);
}

static void testEnemyFolderScan() {
    std::printf("[enemy folder scan]\n");
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "dt_enemies_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "_skip", ec);
    fs::create_directories(root / "wisp", ec);
    fs::create_directories(root / "slime", ec);
    { std::ofstream f(root / "_skip" / "enemy.txt"); f << "[enemy]\nname=안됨\n"; }
    { std::ofstream f(root / "wisp" / "enemy.txt"); f << "[enemy]\nname=도깨비불\nhp=33\nspeed=140\n"; }
    { std::ofstream f(root / "slime" / "enemy.txt"); f << "[enemy]\ndesc=덮어쓴 설명\n"; }

    int before = enemyCount();
    EnemyScanResult r = loadEnemyFolders(root);
    CHECK(r.newEnemies == 1);
    CHECK(enemyCount() == before + 1);
    EnemyKind k;
    CHECK(enemyFromKey("wisp", k));
    CHECK(enemyDef(k).name == "도깨비불");
    CHECK(enemyStatsFor(k, 1).maxHp == 33.f + enemyDef(k).hpGrowth);
    CHECK(!enemyFromKey("_skip", k));
    CHECK(enemyFromKey("slime", k) && enemyDef(k).desc == "덮어쓴 설명");

    EnemyScanResult none = loadEnemyFolders(root / std::filesystem::u8path("없음"));
    CHECK(none.folderCount == 0 && none.warnings.empty());
    fs::remove_all(root, ec);
}

static void testBossPhase2Summons() {
    std::printf("[boss phase2 summon]\n");
    World w;
    w.setBounds(kDesk);
    w.setupDragon(Species::Fire, 20, -1.f, {400.f, 500.f});
    Enemy& boss = w.spawnEnemy(defaultBoss(), 8, {1000.f, 500.f});
    int id = boss.id;
    CHECK(w.enemies().size() == 1);
    // 체력을 절반 아래로 떨어뜨리면 부하를 부른다
    for (Enemy& e : w.enemiesMutable()) if (e.id == id) e.hp = e.maxHp * 0.4f;
    PlayerInput in;
    for (int i = 0; i < 10 && w.enemies().size() < 2; ++i) w.updateCombat(1.f / 30.f, CombatMode::SkillManual, in);
    CHECK(w.enemies().size() >= 2);
    bool foundAdd = false;
    for (const Enemy& e : w.enemies())
        if (e.id != id && enemyDef(e.kind).role == EnemyRole::Melee) foundAdd = true;
    CHECK(foundAdd);
}


// ---------------------------------------------------------------------------
// v0.5: 바탕화면 디펜스 전투
// ---------------------------------------------------------------------------

static void testDefenseBattle() {
    std::printf("[defense battle]\n");
    Game g;
    SaveData s;
    s.hasDragon = true;
    s.dragon.species = Species::Fire;
    s.dragon.level = 6;
    s.dragon.hp = -1.f;
    s.huntMode = CombatMode::SkillManual;
    s.confineToBand = true;  // #47: 띠에서 싸우는 것은 "아래쪽에만"을 켰을 때다
    g.init(s, kDesk);

    // 전투 전에는 바탕화면 전체를 쓴다
    CHECK(g.world().bounds().h > cfg::kBandHeight + 1.f);

    RectF band = g.battleBand();
    // 띠는 화면 맨 아래에 붙어 있다(작업 표시줄 바로 위)
    CHECK(band.bottom() <= kDesk.bottom());
    CHECK(kDesk.bottom() - band.bottom() <= cfg::kBandBottomMargin + 0.01f);
    CHECK(band.h <= cfg::kBandHeight + 0.01f);
    CHECK(band.x == kDesk.x && band.w == kDesk.w);

    g.startHunt();
    CHECK(g.hunting());
    // 전투를 켜면 경계가 띠로 바뀌고 드래곤이 띠 안으로 내려온다
    CHECK(g.world().bounds().h <= cfg::kBandHeight + 0.01f);
    Vec2 dp = g.world().dragon().pos;
    CHECK(dp.y >= band.y && dp.y <= band.bottom());

    // 첫 무리가 나오면 모두 화면 왼쪽 밖에서 들어온다
    PlayerInput in;
    for (int i = 0; i < 30 * 10 && g.world().enemies().empty(); ++i) g.update(1.f / 30.f, in, false, true);
    CHECK(!g.world().enemies().empty());
    bool allFromLeft = true, allEntering = true;
    for (const Enemy& e : g.world().enemies()) {
        if (e.pos.x > band.x) allFromLeft = false;
        if (!e.entering) allEntering = false;
    }
    CHECK(allFromLeft);
    CHECK(allEntering);

    // 적은 오른쪽(드래곤 쪽)으로 다가와 띠 안으로 들어온다
    float before = g.world().enemies().front().pos.x;
    for (int i = 0; i < 30 * 5; ++i) g.update(1.f / 30.f, in, false, true);
    bool someoneEntered = false;
    for (const Enemy& e : g.world().enemies())
        if (!e.entering) someoneEntered = true;
    CHECK(someoneEntered || g.world().enemies().empty() || g.world().enemies().front().pos.x > before);

    // 전투를 끄면 "아래쪽에만"(켜짐)대로 띠에 머물고, 설정을 끄면 다시 바탕화면 전체를 돌아다닌다
    g.stopHunt("테스트");
    CHECK(!g.hunting());
    CHECK(g.world().bounds().h <= cfg::kBandHeight + 0.01f);
    g.setConfineToBand(false);
    CHECK(g.world().bounds().h > cfg::kBandHeight + 1.f);
}

// #47(2026-11-16 사용자 결정): "아래쪽에만"을 끄면 바탕화면 전투도 화면 전체에서 한다.
// 적은 여전히 왼쪽 밖에서 들어오고(디펜스), 드래곤이 있는 높이 근처로 온다.
// 탑 100층 T3(2026-11-18): 바탕화면 디펜스는 드래곤이 머무는 층의 직전 일반 층 구성으로 나온다.
// 탑 층을 돌파하면 그 드래곤의 진행(floor·cleared)과 계정 첫 돌파 기록이 오른다.
static void testHuntFloorPool() {
    std::printf("[hunt floor pool]\n");
    EnemyKind slime{}, goblin{}, ogre{};
    CHECK(enemyFromKey("slime", slime) && enemyFromKey("goblin", goblin) && enemyFromKey("ogre", ogre));
    std::vector<TowerFloorDef> floors(4);
    floors[0].spawns = {{"slime", 2, 4, 0.f}};
    floors[1].spawns = {{"goblin", 2, 6, 0.f}, {"ogre", 1, 6, 0.f}};
    floors[2].boss = true;
    floors[2].spawns = {{"", 1, 8, 0.f}};  // 보스층: 바탕화면에는 쓰지 않는다
    floors[3].spawns = {{"ogre", 1, 9, 0.f}};
    const auto hunt = [&](int dragonFloor) {
        Game g;
        SaveData s;
        s.hasDragon = true;
        s.dragon.species = Species::Fire;
        s.dragon.level = 8;
        s.dragon.floor = dragonFloor;
        s.huntMode = CombatMode::SkillManual;
        g.setTowerFloors(floors);
        g.init(s, kDesk);
        return g;
    };
    {
        Game g = hunt(1);  // 1층 → 1층 구성(슬라임)
        CHECK(g.huntFloor() == 1);
        g.startHunt();
        PlayerInput in;
        for (int i = 0; i < 30 * 10 && g.world().enemies().empty(); ++i) g.update(1.f / 30.f, in, false, true);
        CHECK(!g.world().enemies().empty());
        g_silentFailed = false;
        for (const Enemy& e : g.world().enemies()) CHECK_SILENT(e.kind == slime);
        CHECK(!g_silentFailed);
    }
    {
        Game g = hunt(3);  // 3층 → 직전 일반 층 2층(고블린·오우거)
        CHECK(g.huntFloor() == 2);
        g.startHunt();
        PlayerInput in;
        for (int i = 0; i < 30 * 10 && g.world().enemies().empty(); ++i) g.update(1.f / 30.f, in, false, true);
        g_silentFailed = false;
        for (const Enemy& e : g.world().enemies()) CHECK_SILENT(e.kind == goblin || e.kind == ogre);
        CHECK(!g_silentFailed);
    }
    // 층 수보다 높이 있으면 마지막 층까지만 본다(floors.txt 가 아직 100층이 아님)
    CHECK(hunt(30).huntFloor() <= 4);

    // 돌파 기록: 그 드래곤만 오르고, 계정 첫 돌파는 가장 높은 값을 남긴다
    {
        Game g;
        SaveData s;
        DragonRecord a;
        a.id = 1; a.species = Species::Fire; a.level = 8;
        s.stable = {a};
        s.activeIndex = 0;
        s.dragon = a;
        s.hasDragon = true;
        g.setTowerFloors(floors);
        g.init(s, kDesk);
        g.openTowerWarning();
        g.enterTower(CombatMode::Direct);
        CHECK(g.screen() == Screen::Tower);
        g.world().clearEnemies(false);
        PlayerInput in;
        g.update(1.f / 30.f, in, false, true);
        CHECK(g.screen() == Screen::FloorClear);
        const SaveData snap = g.snapshotForSave();
        CHECK(snap.dragon.cleared == 1 && snap.dragon.floor == 2);
        CHECK(snap.stable[0].cleared == 1 && snap.stable[0].floor == 2);
        CHECK(snap.firstClear == 1);
    }
}

static void testBattleWithoutConfine() {
    std::printf("[battle without confine]\n");
    Game g;
    SaveData s;
    s.hasDragon = true;
    s.dragon.species = Species::Fire;
    s.dragon.level = 6;
    s.dragon.hp = -1.f;
    s.huntMode = CombatMode::SkillManual;
    s.confineToBand = false;
    g.init(s, kDesk);
    g.placeDragon({800.f, 200.f});  // 화면 위쪽
    const float beforeY = g.world().dragon().pos.y;
    g.startHunt();
    CHECK(g.hunting());
    CHECK(g.world().bounds().h > cfg::kBandHeight + 1.f);             // 경계는 화면 전체 그대로
    CHECK(std::fabs(g.world().dragon().pos.y - beforeY) < 1.f);       // 아래로 끌려 내려가지 않는다

    PlayerInput in;
    for (int i = 0; i < 30 * 10 && g.world().enemies().empty(); ++i) g.update(1.f / 30.f, in, false, true);
    CHECK(!g.world().enemies().empty());
    const float dy = g.world().dragon().pos.y;
    g_silentFailed = false;
    for (const Enemy& e : g.world().enemies()) {
        CHECK_SILENT(e.pos.x < kDesk.x);                 // 왼쪽 밖에서 들어온다
        CHECK_SILENT(std::fabs(e.pos.y - dy) <= 100.f);  // 드래곤이 있는 높이 근처로
    }

    // 사냥 중에 "아래쪽에만"을 켜면 띠로 내려간다
    g.setConfineToBand(true);
    CHECK(g.world().confining() || g.world().bounds().h <= cfg::kBandHeight + 0.01f);
    // 사냥을 멈추면 설정(켜짐)대로 띠에 머문다
    for (int i = 0; i < 30 * 30 && g.world().confining(); ++i) g.update(1.f / 30.f, in, false, true);
    g.stopHunt("테스트");
    CHECK(g.world().bounds().h <= cfg::kBandHeight + 0.01f);
}

static void testDefenseWaveGrows() {
    std::printf("[defense wave grows]\n");
    // 웨이브가 쌓이면 한 번에 나오는 적이 늘어난다(상한은 kWaveMax)
    Game g;
    SaveData s;
    s.hasDragon = true;
    s.dragon.species = Species::Earth;
    s.dragon.level = 1;
    s.dragon.hp = -1.f;
    s.huntMode = CombatMode::Auto;
    g.init(s, kDesk);
    g.startHunt();
    g.confirmAutoWarning();
    CHECK(g.hunting());
    g.world().dragon().hp = 1e6f;  // 죽지 않게 해서 웨이브만 센다

    int maxSeen = 0, waves = 0;
    bool wasActive = false;
    PlayerInput in;
    for (int i = 0; i < 30 * 600; ++i) {
        g.update(1.f / 30.f, in, false, true);
        bool active = g.waveActive();
        if (active && !wasActive) ++waves;
        wasActive = active;
        maxSeen = std::max(maxSeen, static_cast<int>(g.world().enemies().size()));
        if (waves >= 12) break;
    }
    CHECK(waves >= 3);
    CHECK(maxSeen >= 1 && maxSeen <= cfg::kWaveMax + 2);  // 보스 소환 등을 감안한 여유
}

static void testAutoBattleNoLongerCapped() {
    std::printf("[auto battle uncapped in game]\n");
    // 상한을 껐으므로 자동 전투가 레벨 5에서 멈추지 않는다
    Game g;
    SaveData s;
    s.hasDragon = true;
    s.dragon.species = Species::Fire;
    s.dragon.level = 4;
    s.dragon.hp = -1.f;
    s.huntMode = CombatMode::Auto;
    g.init(s, kDesk);
    g.world().dragon().hp = 1e6f;
    g.startHunt();
    g.confirmAutoWarning();
    PlayerInput in;
    for (int i = 0; i < 30 * 900 && g.dragonRecord().level <= 6; ++i) g.update(1.f / 30.f, in, false, true);
    CHECK(g.dragonRecord().level > 5);  // 예전 상한(5)을 넘어선다
    CHECK(g.hunting());                 // 상한 때문에 멈추지 않는다
}


// ---------------------------------------------------------------------------
// v0.6: 바탕화면 UI 설정
// ---------------------------------------------------------------------------

static void testUiSettingsSaved() {
    std::printf("[ui settings save]\n");
    SaveData s;
    s.panelX = 123.5f;
    s.panelY = 45.f;
    s.panelCollapsed = true;
    s.confineToBand = true;
    SaveData l;
    CHECK(deserializeSave(serializeSave(s), l));
    CHECK(l.panelX > 123.f && l.panelX < 124.f);
    CHECK(l.panelY == 45.f);
    CHECK(l.panelCollapsed);
    CHECK(l.confineToBand);

    // 이전 저장 파일(설정 키가 없음)도 그대로 읽히고 기본값이 쓰인다
    SaveData old;
    CHECK(deserializeSave("version=1\nnextDragonId=2\nhuntMode=1\n", old));
    CHECK(old.panelX < 0.f && old.panelY < 0.f);
    CHECK(!old.panelCollapsed && !old.confineToBand);
}

static void testConfineToBand() {
    std::printf("[confine to band]\n");
    Game g;
    SaveData s;
    s.hasDragon = true;
    s.dragon.species = Species::Wind;
    s.dragon.level = 3;
    s.dragon.hp = -1.f;
    g.init(s, kDesk);
    CHECK(!g.confineToBand());
    CHECK(g.world().bounds().h > cfg::kBandHeight + 1.f);

    g.setConfineToBand(true);
    CHECK(g.confineToBand());
    // v1.0: 순간이동이 아니라 날아서 내려간다. 도착할 때까지는 경계가 아직 화면 전체다.
    RectF band = g.battleBand();
    PlayerInput in;
    CHECK(g.world().confining());
    for (int i = 0; i < 30 * 30 && g.world().confining(); ++i) g.update(1.f / 30.f, in, false, false);
    CHECK(!g.world().confining());  // 30초 안에는 도착한다
    CHECK(g.world().bounds().h <= cfg::kBandHeight + 0.01f);
    // 그 뒤로는 오래 두어도 띠를 벗어나지 않는다
    for (int i = 0; i < 30 * 120; ++i) {
        g.update(1.f / 30.f, in, false, false);
        CHECK_SILENT(g.world().dragon().pos.y >= band.y - 1.f &&
                     g.world().dragon().pos.y <= band.bottom() + 1.f);
    }
    CHECK(true);

    g.setConfineToBand(false);
    CHECK(g.world().bounds().h > cfg::kBandHeight + 1.f);
}

static void testPlaceAndPanel() {
    std::printf("[drag dragon / panel pos]\n");
    Game g;
    SaveData s;
    s.hasDragon = true;
    s.dragon.species = Species::Water;
    s.dragon.level = 2;
    s.dragon.hp = -1.f;
    g.init(s, kDesk);

    // 끌어다 놓으면 그 자리에 선다
    g.placeDragon({700.f, 500.f});
    CHECK(std::fabs(g.world().dragon().pos.x - 700.f) < 1.f);
    CHECK(std::fabs(g.world().dragon().pos.y - 500.f) < 1.f);
    CHECK(!g.world().dragon().flying);
    // 경계 밖으로는 놓이지 않는다
    g.placeDragon({-9999.f, 9999.f});
    CHECK(g.world().bounds().contains(g.world().dragon().pos.x, g.world().dragon().pos.y));

    g.placeDragon({700.f, 500.f});
    auto& w = g.world();
    auto& actor = w.dragon();
    actor.behavior = IdleBehavior::Walk;
    actor.behaviorLeft = 2.f;
    actor.wanderTarget = {800.f, 500.f};
    g.setForcedMotion(AnimState::Fly);
    w.updateWander(0.1f);
    CHECK(actor.pos.x > 700.f && actor.pos.y == 500.f && !actor.flying);
    CHECK(actor.anim == AnimState::Fly && actor.behaviorLeft < 2.f);
    w.setDragonAnim(AnimState::Attack, 0.1f);
    w.updateCombat(0.01f, CombatMode::Direct, {});
    CHECK(actor.anim == AnimState::Attack);
    w.updateWander(0.15f);
    CHECK(actor.anim == AnimState::Fly);
    g.clearForcedMotion();
    w.updateWander(0.01f);
    CHECK(actor.anim == AnimState::Move);

    const Vec2 stationary = actor.pos;
    for (AnimState pose : {AnimState::Idle, AnimState::Rest}) {
        g.setForcedMotion(pose);
        w.updateWander(1.f);
        CHECK(actor.anim == pose && distance(actor.pos, stationary) == 0.f);
    }
    g.setForcedMotion(AnimState::Fly);
    actor.behavior = IdleBehavior::Rest;
    actor.wanderTarget = actor.pos;
    actor.behaviorLeft = 2.f;
    w.updateWander(0.1f);
    CHECK(actor.anim == AnimState::Fly && !actor.flying && distance(actor.pos, stationary) > 0.f);
    g.clearForcedMotion();

    w.seed(271);
    int flightFrames = 0, restFrames = 0, walkFrames = 0, landFrames = 0;
    for (int i = 0; i < 12000; ++i) {
        const Vec2 before = actor.pos;
        w.updateWander(0.1f);
        CHECK_SILENT(actor.anim != AnimState::Fly || distance(actor.pos, before) > 0.f);
        if (actor.flying) ++flightFrames;
        else if (actor.behavior == IdleBehavior::Rest) ++restFrames;
        else if (actor.behavior == IdleBehavior::Walk) ++walkFrames;
        if (actor.landing) ++landFrames;
    }
    std::printf("  roaming samples: fly=%d rest=%d walk=%d landing=%d\n",
                flightFrames, restFrames, walkFrames, landFrames);
    CHECK(flightFrames > 7200 && restFrames > 0 && walkFrames > 0 && landFrames > 0);
    CHECK(w.bounds().contains(actor.pos.x, actor.pos.y));

    // 상태창 위치·접힘은 저장 요청을 남긴다
    g.setPanelPos({10.f, 20.f});
    CHECK(g.panelPos().x == 10.f && g.panelPos().y == 20.f);
    g.setPanelCollapsed(true);
    CHECK(g.panelCollapsed());
    CHECK(g.snapshotForSave().panelCollapsed);
    CHECK(g.snapshotForSave().panelX == 10.f);
}


// ---------------------------------------------------------------------------
// v0.7: 안정성·편의
// ---------------------------------------------------------------------------

static void testAlerts() {
    std::printf("[alerts]\n");
    Game g;
    SaveData s;
    s.hasDragon = true;
    s.dragon.species = Species::Fire;
    s.dragon.level = 1;
    s.dragon.hp = -1.f;
    g.init(s, kDesk);
    CHECK(!g.hasAlert());

    // 사망하면 화면을 못 봐도 알 수 있게 알림이 남는다
    g.setHuntMode(CombatMode::Direct);
    g.startHunt();
    Vec2 dp = g.world().dragon().pos;
    for (int i = 0; i < 3; ++i) g.world().spawnEnemy(defaultMelee(), 15, dp + Vec2{40.f, -40.f + 40.f * i});
    g.world().dragon().hp = 1.f;
    stepGame(g, 20.f, false, true);
    CHECK(g.screen() == Screen::Death);
    CHECK(g.hasAlert());
    Game::Alert a = g.takeAlert();
    CHECK(!a.title.empty() && !a.body.empty());
    CHECK(a.warning);                       // 사망은 경고로
    CHECK(!g.hasAlert());                   // 꺼내 가면 비워진다
    CHECK(g.takeAlert().title.empty());     // 비었을 때 꺼내도 안전하다

    // 알림은 쌓이기만 하지 않는다(오래된 것부터 버린다)
    for (int i = 0; i < 10; ++i) g.alert("t" + std::to_string(i), "b");
    int n = 0;
    while (g.hasAlert()) { g.takeAlert(); ++n; }
    CHECK(n <= 4);
}

static void testFirstRunHint() {
    std::printf("[first run hint]\n");
    SaveData s;
    CHECK(!s.hintShown);
    Game g;
    g.init(s, kDesk);
    CHECK(!g.hintShown());
    g.markHintShown();
    CHECK(g.hintShown());
    CHECK(g.snapshotForSave().hintShown);
    // 저장하고 다시 읽어도 유지된다(안내가 매번 뜨지 않게)
    SaveData l;
    CHECK(deserializeSave(serializeSave(g.snapshotForSave()), l));
    CHECK(l.hintShown);
    // 이전 저장 파일에는 없는 키라 기본값(false)이다
    SaveData old;
    CHECK(deserializeSave("version=1\nnextDragonId=2\n", old));
    CHECK(!old.hintShown);
}

static void testViewSettings() {
    std::printf("[view settings]\n");
    SaveData s;
    CHECK(s.menuScale == 1.f);
    CHECK(s.charScale == 1.f);
    CHECK(!s.powerSave);
    CHECK(s.menuX < 0.f);
    Game g;
    g.init(s, kDesk);

    // 메뉴 위치는 그대로 저장된다
    g.setMenuPos({120.f, 240.f});
    CHECK(g.menuPos().x == 120.f && g.menuPos().y == 240.f);

    // 배수는 허용 범위를 넘지 않는다
    g.setMenuScale(99.f);
    CHECK(g.menuScale() == cfg::kMenuScaleMax);
    g.setMenuScale(-5.f);
    CHECK(g.menuScale() == cfg::kMenuScaleMin);
    g.setCharScale(99.f);
    CHECK(g.charScale() == cfg::kCharScaleMax);
    g.setCharScale(0.f);
    CHECK(g.charScale() == cfg::kCharScaleMin);

    g.setCharScale(1.4f);
    g.setPowerSave(true);
    CHECK(g.powerSave());

    // 저장하고 다시 읽어도 유지된다
    SaveData l;
    CHECK(deserializeSave(serializeSave(g.snapshotForSave()), l));
    CHECK(l.menuX == 120.f && l.menuY == 240.f);
    CHECK(std::fabs(l.charScale - 1.4f) < 0.001f);
    CHECK(l.powerSave);

    // 이전 저장 파일에는 없는 키라 기본값이 쓰인다
    SaveData old;
    CHECK(deserializeSave("version=1\nnextDragonId=2\n", old));
    CHECK(old.menuScale == 1.f && old.charScale == 1.f && !old.powerSave);

    // 손으로 고친 저장 파일의 범위 밖 값도 읽을 때 맞춘다
    SaveData bad;
    CHECK(deserializeSave("version=1\nui.charScale=50\nui.menuScale=0.01\n", bad));
    CHECK(bad.charScale == cfg::kCharScaleMax);
    CHECK(bad.menuScale == cfg::kMenuScaleMin);
}

static void testBattleBandHeight() {
    std::printf("[battle band]\n");
    // 띠는 작업 표시줄 바로 위에 붙어 있어야 한다. 높이를 바꿔도 이 관계는 유지된다.
    SaveData s;
    Game g;
    g.init(s, kDesk);
    g.chooseSpecies(Species::Fire);
    RectF band = g.battleBand();
    CHECK(std::fabs(band.h - cfg::kBandHeight) < 0.001f);
    CHECK(std::fabs((band.y + band.h) - (kDesk.y + kDesk.h - cfg::kBandBottomMargin)) < 0.001f);
    CHECK(band.w > 0.f);
}

static void testThinBandStanding() {
    std::printf("[thin band standing]\n");
    // v0.8: 전투 띠(50)가 드래곤 지름(68)보다 얇다. 이때 드래곤이 위로 떠오르거나
    // 띠 밖으로 나가면 안 된다. 예전 clampPoint는 범위가 뒤집혀 위쪽에 붙였다.
    CHECK(cfg::kBandHeight < cfg::kDragonRadius * 2.f);
    SaveData s;
    Game g;
    g.init(s, kDesk);
    g.chooseSpecies(Species::Fire);
    RectF band = g.battleBand();
    float floorY = band.bottom() - cfg::kDragonRadius;

    // 띠에만 머물게 하면 날아서 내려가 바닥 줄에 선다
    g.setConfineToBand(true);
    for (int i = 0; i < 30 * 30 && g.world().confining(); ++i) stepGame(g, 1.f / 30.f, false, true);
    CHECK(!g.world().confining());
    CHECK(std::fabs(g.world().dragon().pos.y - floorY) < 0.001f);

    // 도착한 뒤에는 위쪽으로 끌어다 놓아도 바닥 줄로 맞춰진다(경계가 이미 띠다)
    g.placeDragon({band.x + 200.f, band.y - 300.f});
    CHECK(std::fabs(g.world().dragon().pos.y - floorY) < 0.001f);

    // 한참 돌아다녀도 띠 안에 머문다
    for (int i = 0; i < 400; ++i) {
        stepGame(g, 0.05f, false, true);
        const DragonActor& d = g.world().dragon();
        CHECK(d.pos.y <= floorY + 0.5f);
        CHECK(d.pos.y >= band.y - 0.5f);
    }

    // 사냥을 시작해도(아래쪽에만을 켠 그대로) 마찬가지, 적도 띠 안에 들어온다
    g.setHuntMode(CombatMode::Auto);
    g.confirmAutoWarning();
    g.startHunt();
    for (int i = 0; i < 200; ++i) {
        stepGame(g, 0.05f, false, true);
        if (!g.world().dragon().alive) break;
        CHECK(g.world().dragon().pos.y <= floorY + 0.5f);
        for (const Enemy& e : g.world().enemies()) {
            CHECK(e.pos.y >= band.y - 0.5f);
            CHECK(e.pos.y <= band.bottom() + 0.5f);
        }
    }
}

static void testKeepRange() {
    std::printf("[keep range]\n");
    SaveData s;
    CHECK(s.keepRange == 0);
    // 레벨을 올려 사거리가 긴 스킬을 연다(바깥 간격이 의미를 갖게)
    s.hasDragon = true;
    s.dragon.species = Species::Fire;
    s.dragon.level = 8;
    Game g;
    g.init(s, kDesk);
    g.setHuntMode(CombatMode::SkillManual);
    g.startHunt();

    auto settleDistance = [&](int keep) {
        g.setKeepRange(keep);
        float last = 0.f;
        for (int i = 0; i < 600; ++i) {
            stepGame(g, 0.03f, false, true);
            if (!g.world().dragon().alive) break;
            if (g.world().enemies().empty()) continue;
            const Enemy& e = g.world().enemies().front();
            last = distance(g.world().dragon().pos, e.pos) - e.radius;
        }
        return last;
    };

    float nearD = settleDistance(0);
    float farD = settleDistance(2);
    // 바깥 간격이 근접보다 확실히 멀어야 한다. 이게 이번 기능의 전부다.
    CHECK(farD > nearD);

    // 설정은 저장되고 범위를 넘지 않는다
    g.setKeepRange(99);
    CHECK(g.keepRange() == cfg::kKeepRangeCount - 1);
    g.setKeepRange(-5);
    CHECK(g.keepRange() == 0);
    g.setKeepRange(1);
    SaveData l;
    CHECK(deserializeSave(serializeSave(g.snapshotForSave()), l));
    CHECK(l.keepRange == 1);
    SaveData old;
    CHECK(deserializeSave("version=1\nnextDragonId=2\n", old));
    CHECK(old.keepRange == 0);
}

static void testStable() {
    std::printf("[stable]\n");
    SaveData s;
    Game g;
    g.init(s, kDesk);
    CHECK(g.screen() == Screen::Select);
    g.chooseSpecies(Species::Fire);
    CHECK(g.stable().size() == 1 && g.activeIndex() == 0);

    // 관리창에서 한 마리 더 데려온다. 띄운 개체는 그대로다.
    g.openStable();
    CHECK(g.screen() == Screen::Stable);
    g.beginAddDragon();
    CHECK(g.screen() == Screen::Select && g.addingDragon());
    g.chooseSpecies(Species::Water);
    CHECK(g.stable().size() == 2);
    CHECK(g.dragonRecord().species == Species::Fire);  // 띄운 개체는 바뀌지 않는다
    CHECK(g.activeIndex() == 0);

    // 최대 마리 수를 넘지 않는다
    for (int i = 0; i < 5; ++i) {
        g.openStable();
        g.beginAddDragon();
        g.chooseSpecies(Species::Earth);
    }
    CHECK(static_cast<int>(g.stable().size()) == cfg::kMaxStable);
    CHECK(g.stableFull());

    // 보관 중인 개체는 쉬면서 회복한다(경험치는 오르지 않는다)
    SaveData hurt = g.snapshotForSave();
    hurt.stable[1].hp = 1.f;
    int expBefore = hurt.stable[1].exp;
    Game g2;
    g2.init(hurt, kDesk);
    PlayerInput none;
    for (int i = 0; i < 30 * 30; ++i) g2.update(1.f / 30.f, none, false, false);
    SaveData after = g2.snapshotForSave();
    CHECK(after.stable[1].hp > 1.f);
    CHECK(after.stable[1].exp == expBefore);

    // 전투 중에는 바꿀 수 없다
    g2.setHuntMode(CombatMode::Direct);
    g2.startHunt();
    CHECK(g2.hunting());
    CHECK(!g2.canSwitchDragon());
    CHECK(!g2.switchDragon(1));
    CHECK(g2.activeIndex() == 0);

    // 멈추면 바꿀 수 있고, 띄운 개체의 상태가 목록에 저장된다
    g2.stopHunt();
    CHECK(g2.canSwitchDragon());
    CHECK(g2.switchDragon(1));
    CHECK(g2.activeIndex() == 1);
    CHECK(g2.dragonRecord().id == after.stable[1].id);
}

static void testStablePermadeath() {
    std::printf("[stable permadeath]\n");
    // 여러 마리를 가지고 있어도 죽은 개체는 되살아나지 않는다.
    SaveData s;
    Game g;
    g.init(s, kDesk);
    g.chooseSpecies(Species::Fire);
    g.openStable();
    g.beginAddDragon();
    g.chooseSpecies(Species::Water);
    CHECK(g.stable().size() == 2);
    int deadId = g.dragonRecord().id;

    // 띄워 둔 개체를 죽인다. 관리창이 떠 있으면 사냥이 시작되지 않으므로 먼저 닫는다.
    g.closeStable();
    CHECK(g.screen() == Screen::Desktop);
    g.setHuntMode(CombatMode::Direct);
    g.startHunt();
    CHECK(g.hunting());
    Vec2 dp = g.world().dragon().pos;
    for (int i = 0; i < 3; ++i) g.world().spawnEnemy(defaultMelee(), 15, dp + Vec2{40.f, -40.f + 40.f * i});
    g.world().dragon().hp = 1.f;
    stepGame(g, 20.f, false, true);
    CHECK(!g.hasDragon());
    CHECK(g.screen() == Screen::Death);

    // 죽은 개체는 목록에서 빠지고 묘비로만 남는다
    CHECK(g.stable().size() == 1);
    for (const DragonRecord& d : g.stable()) CHECK(d.id != deadId);
    CHECK(g.lastGrave() != nullptr && g.lastGrave()->id == deadId);

    // 남은 개체가 있으므로 관리창으로 간다(자동으로 띄우지는 않는다)
    g.acknowledgeDeath();
    CHECK(g.screen() == Screen::Stable);
    CHECK(!g.hasDragon());

    // 다시 켜도 죽은 개체는 없다
    SaveData saved = g.snapshotForSave();
    SaveData reloaded;
    CHECK(deserializeSave(serializeSave(saved), reloaded));
    CHECK(reloaded.stable.size() == 1);
    for (const DragonRecord& d : reloaded.stable) CHECK(d.id != deadId);
    Game g3;
    g3.init(reloaded, kDesk);
    CHECK(g3.screen() == Screen::Stable);
    CHECK(g3.switchDragon(0));
    CHECK(g3.dragonRecord().id != deadId);
}

static void testTinyScreen() {
    std::printf("[tiny screen]\n");
    // 작업 영역이 아주 작아도 좌표가 뒤집히거나 밖으로 나가지 않는다
    const RectF tiny{0.f, 0.f, 320.f, 80.f};
    Game g;
    SaveData s;
    s.hasDragon = true;
    s.dragon.species = Species::Water;
    s.dragon.level = 2;
    s.dragon.hp = -1.f;
    g.init(s, tiny);
    RectF band = g.battleBand();
    CHECK(band.w > 0.f && band.h > 0.f);
    CHECK(band.y >= tiny.y - 0.01f);
    CHECK(band.bottom() <= tiny.bottom() + 0.01f);

    PlayerInput in;
    g.startHunt();
    for (int i = 0; i < 30 * 60; ++i) {
        g.update(1.f / 30.f, in, false, true);
        const Vec2 p = g.world().dragon().pos;
        CHECK_SILENT(p.x >= tiny.x - 1.f && p.x <= tiny.right() + 1.f);
        CHECK_SILENT(p.y >= tiny.y - 1.f && p.y <= tiny.bottom() + 1.f);
    }
    CHECK(true);
}


// ---------------------------------------------------------------------------
// v1.4 스킬 해제·장착
// ---------------------------------------------------------------------------

// 시험용 종을 하나 만든다. 레벨을 적은 스킬과 비워 둔 스킬을 섞는다.
static SpeciesDef makeSkillTestSpecies(const std::string& key) {
    SpeciesDef d;
    d.key = key;
    d.name = key;
    d.role = "시험";
    d.desc = "스킬 해제 시험용";
    d.baseHp = 100.f; d.baseAtk = 10.f; d.baseDef = 0.f; d.baseSpeed = 170.f; d.baseDodge = 0.f;
    d.hpGrowth = 0.f; d.atkGrowth = 0.f; d.defGrowth = 0.f;
    d.skills = {
        {"A", "", 1, 4.f, SkillKind::Projectile, 1.5f, 0.f, 380.f, 0.f, 0.f},   // level=1 (직접)
        {"B", "", 0, 4.f, SkillKind::Burst, 1.5f, 120.f, 0.f, 0.f, 0.f},        // 자동
        {"C", "", 5, 4.f, SkillKind::Line, 1.5f, 30.f, 300.f, 0.f, 0.f},        // level=5 (직접)
        {"D", "", 0, 4.f, SkillKind::Zone, 1.0f, 90.f, 300.f, 3.f, 0.f},        // 자동
        {"E", "", 0, 4.f, SkillKind::Dash, 1.5f, 30.f, 200.f, 0.f, 0.f},        // 자동
        {"P", "", 0, 0.f, SkillKind::PassiveStat, 0.5f, 0.f, 0.f, 0.f, 0.f},    // 자동(패시브)
    };
    d.skills[5].stat = StatKind::Atk;
    for (int i = 0; i < kAnimStateCount; ++i) d.anims[i] = {8.f, true, 0, 1.f};
    return d;
}

static void testSkillUnlockLevels() {
    std::printf("[skill-unlock]\n");
    Species s = registerOrUpdateSpecies(makeSkillTestSpecies("testskills"));
    const SpeciesDef& d = speciesDef(s);
    // 적어 둔 값은 지킨다
    CHECK(d.skills[0].unlockLevel == 1);
    CHECK(d.skills[2].unlockLevel == 5);
    // 빈 것은 아직 쓰이지 않은 가장 낮은 레벨을 목록 순서대로 가져간다 (2, 3, 4, 6)
    CHECK(d.skills[1].unlockLevel == 2);
    CHECK(d.skills[3].unlockLevel == 3);
    CHECK(d.skills[4].unlockLevel == 4);
    CHECK(d.skills[5].unlockLevel == 6);
    // 한 레벨에 둘이 겹치지 않는다
    std::vector<int> lv;
    for (const SkillDef& sk : d.skills) lv.push_back(sk.unlockLevel);
    std::sort(lv.begin(), lv.end());
    CHECK(std::adjacent_find(lv.begin(), lv.end()) == lv.end());

    // 해제된 액티브만 돌려준다(패시브 제외)
    CHECK(unlockedActiveSkills(d, 1).size() == 1);
    CHECK(unlockedActiveSkills(d, 4).size() == 4);
    CHECK(unlockedActiveSkills(d, 6).size() == 5);  // P는 패시브라 빠진다

    // 등록을 다시 해도 결과가 같다(두 번 돌려도 레벨이 밀리지 않는다)
    registerOrUpdateSpecies(makeSkillTestSpecies("testskills"));
    CHECK(speciesDef(s).skills[1].unlockLevel == 2);
    CHECK(speciesDef(s).skills[5].unlockLevel == 6);
}

static void testSkillSlots() {
    std::printf("[skill-slots]\n");
    Species sp = registerOrUpdateSpecies(makeSkillTestSpecies("testslots"));
    SaveData s;
    Game g;
    g.init(s, kDesk);
    g.chooseSpecies(sp);

    // Lv1에서는 A 하나만 해제돼 1번 칸에 자동으로 들어간다
    CHECK(g.skillSlots()[0] == 0);
    CHECK(g.skillSlots()[1] == -1);
    CHECK(!g.hasUnequippedSkill());

    // 레벨을 올리면 새로 열린 스킬이 빈 칸을 채운다
    SaveData up = g.snapshotForSave();
    up.dragon.level = 6;
    up.stable[0].level = 6;
    Game g2;
    g2.init(up, kDesk);
    // 액티브 5개(A~E) 중 앞의 4개가 칸을 채우고 하나가 남는다
    int filled = 0;
    for (int v : g2.skillSlots()) if (v >= 0) ++filled;
    CHECK(filled == kSkillSlots);
    CHECK(g2.hasUnequippedSkill());
    // 패시브(5번)는 칸에 들어가지 않는다
    for (int v : g2.skillSlots()) CHECK(v != 5);

    // 남은 스킬을 2번 칸에 넣으면 거기 있던 것이 빠진다
    const int replaced = g2.skillSlots()[1];      // 2번 칸에 있던 것
    const int slot0Before = g2.skillSlots()[0];
    const int slot2Before = g2.skillSlots()[2];
    CHECK(replaced != 4);  // 넣으려는 것이 이미 거기 있으면 이 검사 전체가 의미가 없다
    CHECK(g2.equipSkill(1, 4));
    CHECK(g2.skillSlots()[1] == 4);
    // 다른 칸은 그대로다.
    // 예전에는 `slots[0] != before || before == slots[0]` 으로 적혀 있어 **무엇을 넣어도 통과**했고
    // 게다가 before 가 비교 대상이 아닌 2번 칸의 값이었다. 두 칸을 각각 따로 붙든다.
    CHECK(g2.skillSlots()[0] == slot0Before);
    CHECK(g2.skillSlots()[2] == slot2Before);
    // 같은 스킬이 두 칸을 차지하지 않는다
    int count = 0;
    for (int v : g2.skillSlots()) if (v == 4) ++count;
    CHECK(count == 1);

    // 이미 1번에 있는 것을 3번으로 옮기면 두 칸이 맞바뀐다
    int a = g2.skillSlots()[0], c = g2.skillSlots()[2];
    CHECK(g2.equipSkill(2, a));
    CHECK(g2.skillSlots()[2] == a && g2.skillSlots()[0] == c);

    // 패시브와 미해제 스킬은 넣을 수 없다
    CHECK(!g2.equipSkill(0, 5));             // 패시브
    CHECK(!g2.equipSkill(0, 99));            // 없는 번호
    g2.clearSlot(0);
    CHECK(g2.skillSlots()[0] == -1);
}

static void testSkillSlotsSaved() {
    std::printf("[skill-slots-save]\n");
    Species sp = registerOrUpdateSpecies(makeSkillTestSpecies("testsave"));
    SaveData s;
    s.hasDragon = true;
    s.dragon.species = sp;
    s.dragon.level = 6;
    s.dragon.id = 1;
    s.stable.push_back(s.dragon);
    s.activeIndex = 0;
    Game g;
    g.init(s, kDesk);
    CHECK(g.equipSkill(0, 4));
    SaveData out = g.snapshotForSave();

    // 저장 -> 불러오기에서 장착이 그대로 남는다
    SaveData back;
    CHECK(deserializeSave(serializeSave(out), back));
    CHECK(back.dragon.slots == out.dragon.slots);
    CHECK(back.stable[0].slots == out.dragon.slots);

    // v1.3 이전 저장 파일(장착 칸 없음)이면 해제된 액티브로 채워진다
    std::string text = serializeSave(out);
    std::string stripped;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("slots=") != std::string::npos) continue;
        stripped += line + "\n";
    }
    SaveData old;
    CHECK(deserializeSave(stripped, old));
    int filled = 0;
    for (int v : old.dragon.slots) if (v >= 0) ++filled;
    CHECK(filled == kSkillSlots);
    for (int v : old.dragon.slots) CHECK(v != 5);  // 패시브는 들어가지 않는다

    // 저장 파일에 이상한 값이 들어 있어도 그대로 쓰지 않는다
    std::string broken = stripped + "dragon.slots=99,-7,5,1\n";
    SaveData fixed;
    CHECK(deserializeSave(broken, fixed));
    for (int v : fixed.dragon.slots) CHECK(v == -1 || (v >= 0 && v != 5));
}

static void testPassiveSkills() {
    std::printf("[skill-passive]\n");
    // PassiveStat 은 해제된 뒤에만 능력치에 반영된다
    Species sp = registerOrUpdateSpecies(makeSkillTestSpecies("testpassive"));
    DragonStats before = statsFor(sp, 5);   // 패시브는 Lv6 해제
    DragonStats after = statsFor(sp, 6);
    CHECK(std::fabs(before.atk - 10.f) < 0.01f);
    CHECK(std::fabs(after.atk - 15.f) < 0.01f);  // power=0.5 -> +50%

    // 회피 패시브는 상한(0.75)을 넘지 못한다
    SpeciesDef dodgy = makeSkillTestSpecies("testdodge");
    dodgy.baseDodge = 0.7f;
    dodgy.skills[5].stat = StatKind::Dodge;
    dodgy.skills[5].power = 0.5f;
    Species dsp = registerOrUpdateSpecies(dodgy);
    CHECK(statsFor(dsp, 6).dodge <= 0.75f + 0.001f);

    // PassiveOnKill 은 적을 쓰러뜨릴 때 회복시킨다
    SpeciesDef healer = makeSkillTestSpecies("testonkill");
    healer.skills[5].kind = SkillKind::PassiveOnKill;
    healer.skills[5].power = 0.2f;
    healer.skills[5].unlockLevel = 1;
    Species hsp = registerOrUpdateSpecies(healer);
    World w;
    w.setBounds({0.f, 0.f, 1600.f, 900.f});
    w.setupDragon(hsp, 1, 50.f, {800.f, 450.f});
    Enemy& e = w.spawnEnemy(defaultMelee(), 1, {830.f, 450.f});
    float hpBefore = w.dragon().hp;
    e.hp = 0.5f;  // 한 대면 죽는다
    PlayerInput in;
    for (int i = 0; i < 120 && w.aliveEnemyCount() > 0; ++i) w.updateCombat(1.f / 60.f, CombatMode::Auto, in);
    CHECK(w.aliveEnemyCount() == 0);
    CHECK(w.dragon().hp > hpBefore);  // 처치하면서 회복했다
}

static void testAutoUsesEquippedOnly() {
    std::printf("[skill-auto-equipped]\n");
    Species sp = registerOrUpdateSpecies(makeSkillTestSpecies("testauto"));
    World w;
    w.setBounds({0.f, 0.f, 1600.f, 900.f});
    w.setupDragon(sp, 6, -1.f, {800.f, 450.f});
    // 칸을 모두 비우면 자동 전투에서도 스킬이 나가지 않는다(기본 공격만 한다)
    w.setSlots({-1, -1, -1, -1});
    w.spawnEnemy(defaultMelee(), 6, {900.f, 450.f});
    PlayerInput in;
    for (int i = 0; i < 300; ++i) w.updateCombat(1.f / 60.f, CombatMode::Auto, in);
    for (float cd : w.dragon().skillCd) CHECK(cd <= 0.f);  // 아무 스킬도 쓰지 않았다

    // 한 칸만 채우면 그 스킬만 쓴다
    World w2;
    w2.setBounds({0.f, 0.f, 1600.f, 900.f});
    w2.setupDragon(sp, 6, -1.f, {800.f, 450.f});
    w2.setSlots({2, -1, -1, -1});  // C(Line)만
    w2.spawnEnemy(defaultMelee(), 6, {880.f, 450.f});
    for (int i = 0; i < 300; ++i) w2.updateCombat(1.f / 60.f, CombatMode::Auto, in);
    CHECK(w2.dragon().skillCd[2] > 0.f || w2.aliveEnemyCount() == 0);
    CHECK(w2.dragon().skillCd[0] <= 0.f);
    CHECK(w2.dragon().skillCd[1] <= 0.f);

    // 수동에서도 키(칸)와 스킬이 짝지어진다 — 2번 키는 2번 칸의 스킬을 쓴다
    World w3;
    w3.setBounds({0.f, 0.f, 1600.f, 900.f});
    w3.setupDragon(sp, 6, -1.f, {800.f, 450.f});
    w3.setSlots({4, 2, -1, -1});
    w3.spawnEnemy(defaultMelee(), 6, {880.f, 450.f});
    PlayerInput press;
    press.skill[1] = true;
    w3.updateCombat(1.f / 60.f, CombatMode::SkillManual, press);
    CHECK(w3.dragon().skillCd[2] > 0.f);   // 2번 칸에 넣어 둔 C가 나갔다
    CHECK(w3.dragon().skillCd[4] <= 0.f);  // 1번 칸의 E는 그대로다
}

// v1.3에서 빠져 있던 것: 다친 드래곤의 체력이 저장되지 않아 다시 켜면 가득 찬 채로 돌아왔다.
static void testActiveDragonHpSaved() {
    std::printf("[active hp saved]\n");
    SaveData s;
    Game g;
    g.init(s, kDesk);
    g.chooseSpecies(Species::Fire);
    float maxHp = g.world().dragon().stats.maxHp;
    g.world().dragon().hp = maxHp * 0.3f;
    SaveData out = g.snapshotForSave();
    CHECK(std::fabs(out.stable[0].hp - maxHp * 0.3f) < 0.01f);
    SaveData back;
    CHECK(deserializeSave(serializeSave(out), back));
    CHECK(std::fabs(back.dragon.hp - maxHp * 0.3f) < 0.6f);  // 저장 파일은 소수점을 줄여 적는다
    // 다시 켜도 가득 차 있지 않다
    Game g2;
    g2.init(back, kDesk);
    CHECK(g2.world().dragon().hp < maxHp * 0.5f);
}


// 보기 크기가 판정에도 쓰이는지 (2026-11-17)
// - scale_with_size 를 켠 스킬만 반경이 커진다
// - 사거리(range)는 켜도 커지지 않는다
// - 100% 에서는 예전과 완전히 같아야 한다(균형이 바뀌면 안 된다)
static void testSizeScaledHitbox() {
    std::printf("[size-scaled-hitbox]\n");
    std::string text =
        "[dragon]\nname=크기시험\n"
        "[skill.1]\nname=넓어지는장판\nkind=Burst\nradius=100\nrange=200\nscale_with_size=yes\n"
        "[skill.2]\nname=그대로장판\nkind=Burst\nradius=100\nrange=200\n";
    DragonParseResult r = parseDragonText(text, "sizetest", nullptr);
    // 파서가 뒤에 기본 스킬을 채우므로 개수는 2개 이상이면 된다
    CHECK(r.def.skills.size() >= 2);
    CHECK(r.def.skills[0].scaleWithSize == true);
    CHECK(r.def.skills[1].scaleWithSize == false);

    World w;
    CHECK(std::fabs(w.sizeScale() - 1.f) < 0.001f);  // 기본은 100%
    // 100% 에서는 둘 다 적힌 값 그대로다 — 기존 균형이 바뀌지 않는다
    CHECK(std::fabs(w.skillRadius(r.def.skills[0]) - 100.f) < 0.01f);
    CHECK(std::fabs(w.skillRadius(r.def.skills[1]) - 100.f) < 0.01f);

    w.setSizeScale(3.f);
    CHECK(std::fabs(w.skillRadius(r.def.skills[0]) - 300.f) < 0.01f);  // 켠 쪽만 커진다
    CHECK(std::fabs(w.skillRadius(r.def.skills[1]) - 100.f) < 0.01f);  // 끈 쪽은 그대로
    // 사거리는 어느 쪽도 커지지 않는다
    CHECK(std::fabs(r.def.skills[0].range - 200.f) < 0.01f);

    w.setSizeScale(0.f);   // 잘못된 값은 100% 로 되돌린다
    CHECK(std::fabs(w.sizeScale() - 1.f) < 0.001f);
}

static void testSkillMotionParse() {
    std::printf("[skill-motion]\n");
    std::string text =
        "[dragon]\nname=모션시험\n"
        "[skill.1]\nname=하나\nkind=Line\nmotion=claw\nmotion_fps=14\nmotion_frame_w=256\n"
        "[skill.2]\nname=둘\nkind=PassiveStat\nstat=dodge\npower=0.04\n"
        "[skill.5]\nname=다섯\nkind=Burst\n";
    DragonParseResult r = parseDragonText(text, "motiontest", nullptr);
    CHECK(r.def.skills.size() == 5);
    CHECK(r.def.skills[0].motion == "claw");
    CHECK(std::fabs(r.def.skills[0].motionAnim.fps - 14.f) < 0.01f);
    CHECK(r.def.skills[0].motionAnim.loop == false);  // 스킬 모션은 항상 1회 재생
    CHECK(r.def.skills[1].kind == SkillKind::PassiveStat);
    CHECK(r.def.skills[1].stat == StatKind::Dodge);
    CHECK(skillIsPassive(r.def.skills[1].kind));
    // 번호를 건너뛰면 경고한다(사이는 기본값 스킬로 채워진다)
    bool warnedGap = false;
    for (const std::string& w : r.warnings)
        if (w.find("건너뛰") != std::string::npos) warnedGap = true;
    CHECK(warnedGap);

    // 모르는 stat 과 kind 는 경고로 알린다
    DragonParseResult bad = parseDragonText("[dragon]\nname=x\n[skill.1]\nkind=Nope\nstat=luck\n", "badtest", nullptr);
    CHECK(bad.warnings.size() >= 2);
}

// v1.5: 내장 종을 없앴으므로(이제 전부 assets/dragons/ 폴더에서 온다) 테스트가 쓸 종을
// 여기서 네 개 등록한다. 등록 순서가 곧 Species::Fire(0) ~ Species::Water(3) 번호다.
// 게임 실제 종과는 무관한 시험용이다.
static void registerTestSpecies() {
    const char* keys[4] = {"testa", "testb", "testc", "testd"};
    for (const char* k : keys) ensureSpecies(k);
    CHECK(speciesCount() >= 4);
}

// 레지스트리가 비어 있을 때 죽지 않는가.
// v1.5에서 내장 4종을 지운 뒤로 "종이 하나도 없는 상태"가 실제로 생길 수 있다
// (assets/dragons 가 없거나 전부 읽기에 실패). 예전에는 speciesDef 가 빈 deque 를
// 인덱싱해 UB 로 죽었고, serializeSave 가 묘비마다 이 함수를 불러서
// **저장 파일을 읽는 순간 죽는** 경로였다.
// 이 테스트는 종을 등록하기 **전에** 돌아야 하므로 main 의 맨 앞에 둔다.
static void testEmptySpeciesRegistry() {
    CHECK(speciesCount() == 0);  // 여기서 0이 아니면 이 테스트는 의미가 없다(순서를 확인할 것)
    const SpeciesDef& d = speciesDef(Species::Fire);
    CHECK(!d.key.empty());
    CHECK(!d.name.empty());
    // 저장 직렬화가 종을 참조하는 두 자리(현재 개체·묘비)를 빈 레지스트리에서 지난다.
    SaveData s{};
    s.hasDragon = true;
    s.dragon.level = 3;
    s.graves.push_back({1, Species::Water, 2, "tower1"});
    const std::string text = serializeSave(s);
    CHECK(!text.empty());
    SaveData back{};
    CHECK(deserializeSave(text, back));
    CHECK(back.graves.size() == 1);
}

// 저장 파일의 레벨 상한. dragon.level 과 stable.N.level 이 같은 규칙을 써야 한다.
// 퍼저(fuzz_save_roundtrip)가 찾았다: dragon.level 에만 상한이 없어서
// "읽기 -> 쓰기 -> 읽기" 가 같은 글을 내놓지 않았다(34 -> 34 -> 30).
static void testSaveLevelClamp() {
    const std::string text =
        "# Dragon Tower save\n"
        "version=1\n"
        "hasDragon=1\n"
        "dragon.level=534\n"
        "stable.count=1\n"
        "stable.0.level=534\n"
        "activeIndex=0\n";
    SaveData a{};
    CHECK(deserializeSave(text, a));
    CHECK(a.dragon.level == cfg::kMaxLevel);
    // 왕복이 안정적인가 — 한 번 쓴 글을 다시 읽어 쓰면 같아야 한다.
    const std::string once = serializeSave(a);
    SaveData b{};
    CHECK(deserializeSave(once, b));
    CHECK(serializeSave(b) == once);
}


// ---------------------------------------------------------------------------
// v1.6: 그림을 빌려 쓰는 적 변종 (enemy.txt 의 art_from)
// ---------------------------------------------------------------------------

static void testEnemyArtFromParse() {
    std::printf("[enemy art_from parse]\n");
    // 제대로 적으면 읽힌다. 소문자로 맞춰 담는다
    EnemyParseResult r = parseEnemyText("[enemy]\nname=정예 슬라임\nart_from=SLIME\n", "elite-slime", nullptr);
    CHECK(r.warnings.empty());
    CHECK(r.def.artFrom == "slime");

    // 자기 자신·폴더 이름 규칙에 맞지 않는 값은 경고로 남고 비워 둔다(그림은 자기 폴더에서 읽는다)
    EnemyParseResult self = parseEnemyText("[enemy]\nart_from=mine\n", "mine", nullptr);
    CHECK(self.warnings.size() == 1);
    CHECK(self.def.artFrom.empty());
    EnemyParseResult bad = parseEnemyText("[enemy]\nart_from=../slime\n", "x", nullptr);
    CHECK(bad.warnings.size() == 1);
    CHECK(bad.def.artFrom.empty());

    // v1.5에 정하고 파서에 빠져 있던 art_facing
    EnemyParseResult f = parseEnemyText("[enemy]\nart_facing=right\n", "x", nullptr);
    CHECK(f.warnings.empty() && f.def.artFacesRight);
    EnemyParseResult fl = parseEnemyText("[enemy]\nart_facing=left\n", "x", nullptr);
    CHECK(fl.warnings.empty() && !fl.def.artFacesRight);
    EnemyParseResult fw = parseEnemyText("[enemy]\nart_facing=위쪽\n", "x", nullptr);
    CHECK(fw.warnings.size() == 1 && !fw.def.artFacesRight);
}

static void testEnemyArtFromScanWarnings() {
    std::printf("[enemy art_from scan]\n");
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "dt_artfrom_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "arta", ec);
    fs::create_directories(root / "artb", ec);
    fs::create_directories(root / "artc", ec);
    { std::ofstream f(root / "arta" / "enemy.txt"); f << "[enemy]\nname=원본\n"; }
    // 있는 폴더를 가리킨다 -> 경고 없음
    { std::ofstream f(root / "artb" / "enemy.txt"); f << "[enemy]\nname=빌림\nart_from=arta\n"; }
    // 없는 폴더를 가리킨다 -> 경고
    { std::ofstream f(root / "artc" / "enemy.txt"); f << "[enemy]\nname=없는곳\nart_from=nowhere\n"; }

    EnemyScanResult r = loadEnemyFolders(root);
    bool missing = false;
    for (const std::string& w : r.warnings)
        if (w.find("artc") != std::string::npos && w.find("nowhere") != std::string::npos) missing = true;
    CHECK(missing);
    bool falseAlarm = false;
    for (const std::string& w : r.warnings)
        if (w.find("artb") != std::string::npos) falseAlarm = true;
    CHECK(!falseAlarm);

    // 빌린 쪽을 또 빌리면(한 다리 넘기면) 경고한다
    { std::ofstream f(root / "artc" / "enemy.txt"); f << "[enemy]\nname=두다리\nart_from=artb\n"; }
    EnemyScanResult chain = loadEnemyFolders(root);
    bool chained = false;
    for (const std::string& w : chain.warnings)
        if (w.find("artc") != std::string::npos && w.find("artb") != std::string::npos) chained = true;
    CHECK(chained);
    fs::remove_all(root, ec);
}

// 실제 assets 폴더를 읽어 변종 3종이 등록되고 실제로 뽑히는지 본다.
// ctest 가 assets 경로를 인자로 준다. 인자가 없으면(직접 실행) 건너뛴다.
// 등록부를 실제 데이터로 바꾸므로 main 의 마지막에서 돈다.
static void testEliteVariantsSpawn(const std::filesystem::path& assets) {
    std::printf("[elite variants spawn]\n");
    EnemyScanResult scan = loadEnemyFolders(assets / "enemies");
    for (const std::string& w : scan.warnings) std::printf("  경고: %s\n", w.c_str());
    CHECK(scan.warnings.empty());  // asset_check 와 같은 기준

    EnemyKind es, eg, el, sl, li;
    CHECK(enemyFromKey("elite-slime", es));
    CHECK(enemyFromKey("elite-goblin", eg));
    CHECK(enemyFromKey("elite-lich", el));
    CHECK(enemyFromKey("slime", sl) && enemyFromKey("lich", li));

    // 그림은 원본에서 빌리고, 원본은 빌리지 않는다
    CHECK(enemyDef(es).artFrom == "slime");
    CHECK(enemyDef(eg).artFrom == "goblin");
    CHECK(enemyDef(el).artFrom == "lich");
    CHECK(enemyDef(sl).artFrom.empty() && enemyDef(li).artFrom.empty());

    // 행동이 원본과 다르다: 돌진형은 더 빠르고, 궁수는 더 멀리서 때린다
    CHECK(enemyDef(es).baseSpeed > enemyDef(sl).baseSpeed);
    CHECK(enemyDef(eg).attackRange > enemyDef(sl).attackRange * 2.f);
    CHECK(enemyDef(eg).keepRatio < enemyDef(sl).keepRatio);
    // 예고 없이 맞는 적은 만들지 않는다(확정 규칙: 적 공격은 항상 예고 후 판정)
    CHECK(enemyDef(es).windup > 0.f && enemyDef(eg).windup > 0.f && enemyDef(el).windup > 0.f);

    // 원본 리치가 여전히 탑의 기본 보스다(변종이 자리를 빼앗지 않는다)
    CHECK(enemyDef(el).role == EnemyRole::Boss);
    CHECK(defaultBoss() == li);

    // 바탕화면 사냥: 일반 적 두 변종이 실제로 나온다.
    // T3(2026-11-18)부터 나오는 적은 드래곤이 머무는 층의 직전 일반 층 구성을 따른다 —
    // 실제 floors.txt 에서 정예 슬라임은 8층, 정예 고블린은 7층에 있다(9층→8층, 8층→7층).
    TowerScanResult towerScan = loadTowerFloors(assets / "tower");
    CHECK(towerScan.found);
    const auto sawInHunt = [&](int dragonFloor, EnemyKind want, int& wavesOut) {
        Game g;
        SaveData s;
        s.hasDragon = true;
        s.dragon.species = Species::Fire;
        s.dragon.level = 60;  // 치이지 않게 높게 둔다. 매 무리를 바로 치운다
        s.dragon.hp = -1.f;
        s.dragon.floor = dragonFloor;
        s.huntMode = CombatMode::SkillManual;
        g.setTowerFloors(towerScan.floors);
        g.init(s, kDesk);
        g.startHunt();
        PlayerInput in;
        bool saw = false;
        for (int w = 0; w < 200 && !saw; ++w) {
            for (int i = 0; i < 30 * 20 && g.world().enemies().empty(); ++i) g.update(1.f / 30.f, in, false, true);
            if (g.world().enemies().empty()) break;
            ++wavesOut;
            for (const Enemy& e : g.world().enemies())
                if (e.kind == want) saw = true;
            g.world().clearEnemies(false);
        }
        return saw;
    };
    int waves = 0;
    const bool sawSlime = sawInHunt(9, es, waves);
    const bool sawGoblin = sawInHunt(8, eg, waves);
    CHECK(waves > 0);  // 무리가 실제로 나왔다(안 나왔으면 아래 두 줄이 뜻이 없다)
    CHECK(sawSlime);
    CHECK(sawGoblin);
    std::printf("  무리 %d번 안에 두 변종이 모두 나왔다\n", waves);

    // 보스 변종: 체력 절반에서 정예 슬라임 3마리를 부른다
    PlayerInput in;
    World wd;
    wd.setBounds(kDesk);
    wd.setupDragon(Species::Fire, 20, -1.f, {400.f, 500.f});
    Enemy& boss = wd.spawnEnemy(el, 8, {1000.f, 500.f});
    int id = boss.id;
    for (Enemy& e : wd.enemiesMutable()) if (e.id == id) e.hp = e.maxHp * 0.4f;
    for (int i = 0; i < 20 && wd.enemies().size() < 4; ++i) wd.updateCombat(1.f / 30.f, CombatMode::SkillManual, in);
    int adds = 0;
    for (const Enemy& e : wd.enemies())
        if (e.id != id && e.kind == es) ++adds;
    CHECK(adds == 3);
}

// 탑 100층 구조(2026-11-15 사용자 결정): 층 종류, 자동사냥 기준 층, 필드 보스 레벨 제한.
// 설계: docs/tower-100-design.md 4.1. 아직 게임 흐름에는 연결되지 않은 순수 함수들이다.
static void testTowerHundredRules() {
    std::printf("[tower 100 rules]\n");
    CHECK(floorType(1) == FloorType::Normal);
    CHECK(floorType(4) == FloorType::Normal);
    CHECK(floorType(5) == FloorType::MidBoss);
    CHECK(floorType(9) == FloorType::Normal);
    CHECK(floorType(10) == FloorType::FieldBoss);
    CHECK(floorType(15) == FloorType::MidBoss);
    CHECK(floorType(20) == FloorType::FieldBoss);
    CHECK(floorType(95) == FloorType::MidBoss);
    CHECK(floorType(99) == FloorType::Normal);
    CHECK(floorType(100) == FloorType::FieldBoss);
    CHECK(floorType(0) == FloorType::Invalid);
    CHECK(floorType(101) == FloorType::Invalid);
    CHECK(floorType(-5) == FloorType::Invalid);

    CHECK(huntFloorFor(23) == 22);
    CHECK(huntFloorFor(21) == 19);   // 20층(필드 보스)을 건너뛴다
    CHECK(huntFloorFor(16) == 14);   // 15층(중간 보스)을 건너뛴다
    CHECK(huntFloorFor(11) == 9);    // 10층(필드 보스)을 건너뛴다
    CHECK(huntFloorFor(6) == 4);
    CHECK(huntFloorFor(2) == 1);
    CHECK(huntFloorFor(1) == 1);     // 아직 깬 층이 없으면 1층 몬스터
    CHECK(huntFloorFor(0) == 1);
    CHECK(huntFloorFor(101) == 99);  // 100층 정복 뒤
    g_silentFailed = false;
    for (int fl = 1; fl <= 101; ++fl) CHECK_SILENT(floorType(huntFloorFor(fl)) == FloorType::Normal);

    CHECK(fieldBossLevelCap(10) == 50);
    CHECK(fieldBossLevelCap(20) == 100);
    CHECK(fieldBossLevelCap(30) == 150);
    CHECK(fieldBossLevelCap(100) == 500);
    CHECK(fieldBossLevelCap(100) == cfg::kMaxLevel);  // 100층 상한이 곧 최고 레벨
    CHECK(fieldBossLevelCap(5) == 0);
    CHECK(fieldBossLevelCap(11) == 0);

    CHECK(partyMeetsLevelCap(10, {50, 50, 50, 50}));   // 50 포함
    CHECK(!partyMeetsLevelCap(10, {50, 51, 1, 1}));    // 한 마리라도 51이면 안 된다
    CHECK(partyMeetsLevelCap(20, {100, 1, 99, 100}));
    CHECK(!partyMeetsLevelCap(20, {101, 1, 1, 1}));
    CHECK(!partyMeetsLevelCap(10, {1, 1, 1}));         // 4마리 미만
    CHECK(!partyMeetsLevelCap(10, {1, 1, 1, 1, 1}));   // 5마리
    CHECK(!partyMeetsLevelCap(15, {1, 1, 1, 1}));      // 필드 보스층이 아니다

    // 최고 레벨 500: 지금 경험치 식으로 누적이 32비트 정수 안에 여유 있게 들어가는가.
    // 곡선이나 보상 배율을 바꾸면 이 검사가 먼저 깨진다 — 그때 64비트로 옮긴다.
    CHECK(totalExpToReach(cfg::kMaxLevel) > 0);
    CHECK(totalExpToReach(cfg::kMaxLevel) < 1000000000);
    DragonRecord d;
    ExpResult r = grantExp(d, totalExpToReach(cfg::kMaxLevel) + 12345, false);
    CHECK(d.level == cfg::kMaxLevel);
    CHECK(r.levelsGained == cfg::kMaxLevel - 1);
    CHECK(d.exp == 0);  // 최고 레벨에서는 남는 경험치를 버린다
    CHECK(cfg::kMaxSkillsPerDragon == 30);  // 레벨 상한을 올려도 스킬 칸은 그대로
}

// 큐 11번: 프레임 측정의 순수 계산(include/core/FrameStats.h). 시계는 Windows 쪽에 있고
// 모으고 평균 내는 규칙만 여기서 고정한다.
static void testFrameStats() {
    std::printf("\n[프레임 측정 누적]\n");
    FrameStats st(3);
    CHECK(st.window() == 3);
    CHECK(!st.hasAverage());          // 창이 차기 전에는 보여 줄 평균이 없다
    CHECK(st.avgTotalMs() == 0.0);
    CHECK(st.fps() == 0.0);           // 0 으로 나누지 않는다

    // 한 프레임에 같은 단계를 여러 번 넣으면 더해진다.
    st.addStage(FrameStage::Ui, 1.0);
    st.addStage(FrameStage::Ui, 0.5);
    st.addStage(FrameStage::Clear, 2.0);
    st.endFrame();
    CHECK(std::fabs(st.row(FrameStage::Ui).lastMs - 1.5) < 1e-9);
    CHECK(std::fabs(st.row(FrameStage::Clear).lastMs - 2.0) < 1e-9);
    CHECK(st.totalFrames() == 1);
    CHECK(!st.hasAverage());          // 아직 3 프레임이 안 됐다

    // 프레임이 바뀌면 누적이 비워진다(앞 프레임 값이 새 프레임에 섞이지 않는다).
    st.addStage(FrameStage::Clear, 4.0);
    st.endFrame();
    CHECK(std::fabs(st.row(FrameStage::Clear).lastMs - 4.0) < 1e-9);
    CHECK(st.row(FrameStage::Ui).lastMs == 0.0);

    // 세 번째 프레임에서 창이 차고 평균·최대가 한 번 갱신된다.
    st.addStage(FrameStage::Clear, 3.0);
    st.endFrame();
    CHECK(st.hasAverage());
    CHECK(st.totalFrames() == 3);
    CHECK(std::fabs(st.row(FrameStage::Clear).avgMs - 3.0) < 1e-9);  // (2+4+3)/3
    CHECK(std::fabs(st.row(FrameStage::Clear).maxMs - 4.0) < 1e-9);
    CHECK(std::fabs(st.row(FrameStage::Ui).avgMs - 0.5) < 1e-9);     // (1.5+0+0)/3
    CHECK(std::fabs(st.avgTotalMs() - 3.5) < 1e-9);                  // 단계 평균의 합
    CHECK(std::fabs(st.fps() - 1000.0 / 3.5) < 1e-6);

    // 창이 찬 뒤에는 최대가 새 창의 것만 본다(앞 창의 4.0 이 남지 않는다).
    for (int i = 0; i < 3; ++i) {
        st.addStage(FrameStage::Clear, 1.0);
        st.endFrame();
    }
    CHECK(std::fabs(st.row(FrameStage::Clear).maxMs - 1.0) < 1e-9);
    CHECK(std::fabs(st.row(FrameStage::Clear).avgMs - 1.0) < 1e-9);

    // 시계가 튀어도(음수·NaN) 표가 망가지지 않는다.
    FrameStats bad(1);
    bad.addStage(FrameStage::World, -5.0);
    bad.addStage(FrameStage::World, std::nan(""));
    bad.addStage(FrameStage::World, 2.0);
    bad.endFrame();
    CHECK(std::fabs(bad.row(FrameStage::World).avgMs - 2.0) < 1e-9);

    // 창 길이를 0 이나 음수로 줘도 1 로 맞춰진다(0 으로 나누지 않는다).
    FrameStats zero(0);
    CHECK(zero.window() == 1);
    zero.addStage(FrameStage::Present, 1.0);
    zero.endFrame();
    CHECK(zero.hasAverage());

    // 끄고 켤 때 쓰는 reset: 창 길이는 남고 모은 숫자는 사라진다.
    st.reset();
    CHECK(st.window() == 3);
    CHECK(!st.hasAverage());
    CHECK(st.totalFrames() == 0);
    CHECK(st.row(FrameStage::Clear).avgMs == 0.0);
}

static void testLayeredBreath() {
    const auto spec = parseDragonText(
        "[dragon]\nname=Breath test\n[skill.1]\nkind=Line\nrange=300\nradius=70\npower=2.6\ncooldown=14\n"
        "scale_with_size=yes\nlayered_breath=bonestorm\nbreath_input_lock=1.92\n"
        "[skill.2]\nkind=Line\nrange=300\nradius=70\npower=2.6\n"
        "[skill.3]\nkind=Line\nrange=300\nradius=44\npower=2.4\ncooldown=11\n"
        "scale_with_size=yes\nlayered_breath=umbral_vortex_v2\nbreath_defer_hit=no\n",
        "layered-breath-test", nullptr);
    CHECK(spec.warnings.empty());
    const Species species = registerOrUpdateSpecies(spec.def);
    const Vec2 origin{600.f, 300.f};
    const auto scenario = [&](Vec2 aim, float scale, int skill) {
        World world;
        world.setBounds({0.f, 0.f, 1500.f, 1000.f});
        world.setSizeScale(scale);
        world.setupDragon(species, cfg::kMaxLevel, 0.f, origin);
        world.dragon().basicCd = 99.f;
        world.setSlots({skill, -1, -1, -1});
        const auto target = [&](Vec2 at) {
            Enemy& enemy = world.spawnEnemy(defaultMelee(), 1, at);
            enemy.radius = 8.f;
            enemy.hp = enemy.maxHp = 10000.f;
            enemy.stun = 99.f;
        };
        target(origin + aim * 80.f);
        target(origin + aim * 300.f);
        target(origin + aim * (320.f + 80.f * scale));
        return world;
    };
    PlayerInput cast{};
    cast.skill[0] = true;
    for (Vec2 aim : {Vec2{1.f, 0.f}, Vec2{-1.f, 0.f}, Vec2{-0.8f, -0.6f}}) {
        for (float scale : {1.f, 3.f, 10.f}) {
            auto world = scenario(aim, scale, 0);
            world.updateCombat(0.f, CombatMode::Direct, cast);
            const auto castBreath = world.dragon().breath;
            CHECK(castBreath.has_value());
            if (!castBreath) continue;
            CHECK(distance(castBreath->aim, aim) < 0.001f);
            CHECK(world.dragon().skillCd[0] == 14.f);
            PlayerInput move{};
            move.moveX = 1.f;
            world.updateCombat(0.47f, CombatMode::Direct, move);
            CHECK(distance(world.dragon().pos, origin) < 0.001f);
            CHECK(world.enemies()[0].hp == 10000.f && world.enemies()[1].hp == 10000.f);
            world.updateCombat(0.01f, CombatMode::Direct, {});
            const float hitHp = world.enemies()[0].hp;
            CHECK(hitHp < 10000.f && world.enemies()[1].hp < 10000.f);
            CHECK(world.enemies()[2].hp == 10000.f);
            world.updateCombat(1.1f, CombatMode::Direct, {});
            CHECK(world.enemies()[0].hp == hitHp);
            world.updateFx(0.4f, true);  // Floor clear can stop the combat clock during the tail.
            CHECK(!world.dragon().breath);
            CHECK(world.enemies()[0].hp == hitHp);
        }
    }
    auto cancelled = scenario({1.f, 0.f}, 1.f, 0);
    cancelled.updateCombat(0.f, CombatMode::Direct, cast);
    cancelled.dragon().alive = false;
    cancelled.updateCombat(0.5f, CombatMode::Direct, {});
    CHECK(!cancelled.dragon().breath);
    CHECK(cancelled.enemies()[0].hp == 10000.f);

    auto ordinary = scenario({1.f, 0.f}, 1.f, 1);
    ordinary.updateCombat(0.f, CombatMode::Direct, cast);
    CHECK(!ordinary.dragon().breath);
    CHECK(ordinary.enemies()[0].hp < 10000.f);

    for (Vec2 aim : {Vec2{1.f, 0.f}, Vec2{-1.f, 0.f}, Vec2{0.8f, -0.6f}}) {
        for (float scale : {1.f, 3.f, 10.f}) {
            auto shadow = scenario(aim, scale, 2);
            shadow.updateCombat(0.f, CombatMode::Direct, cast);
            const auto castBreath = shadow.dragon().breath;
            CHECK(castBreath.has_value());
            if (!castBreath) continue;
            const auto& attack = spec.def.skills[2];
            CHECK(castBreath->bodyFrame(attack) == 2);
            CHECK(castBreath->facingLeft == (aim.x < 0.f));
            CHECK(distance(castBreath->aim, aim) < 0.001f);
            CHECK(shadow.dragon().skillCd[2] == 11.f);
            CHECK(castBreath->radius == 44.f * scale);
            const float hitHp = shadow.enemies()[0].hp;
            CHECK(hitHp < 10000.f && shadow.enemies()[1].hp < 10000.f);
            CHECK(shadow.enemies()[2].hp == 10000.f);
            PlayerInput move{};
            move.moveX = -1.f;
            shadow.updateCombat(0.48f, CombatMode::Direct, move);
            CHECK(shadow.dragon().pos.x < origin.x);
            const auto activeBreath = shadow.dragon().breath;
            CHECK(activeBreath && activeBreath->bodyFrame(attack) == 6);
            CHECK(shadow.enemies()[0].hp == hitHp);
            shadow.updateCombat(0.96f, CombatMode::Direct, {});
            const auto endingBreath = shadow.dragon().breath;
            CHECK(endingBreath && endingBreath->bodyFrame(attack) == 8);
            shadow.updateCombat(0.36f, CombatMode::Direct, {});
            const auto tailBreath = shadow.dragon().breath;
            CHECK(tailBreath && tailBreath->bodyFrame(attack) == 11);
            shadow.updateCombat(0.12f, CombatMode::Direct, {});
            CHECK(!shadow.dragon().breath);
            CHECK(shadow.enemies()[0].hp == hitHp);
        }
    }
}

// 2026-11-18: 타격 프레임 스킬(hit_frame=). 피해는 정해진 모션 프레임에, 효과는 fx_frame 에
// 본체 기준점(fx_anchor)에 붙여 뜬다. 그사이 제자리에 서 있고, 끊기면 쿨타임 일부를 돌려준다.
static void testTimedHit() {
    const auto spec = parseDragonText(
        "[dragon]\nname=Timed hit test\n[skill.1]\nkind=Line\nrange=150\nradius=34\npower=1.8\ncooldown=4.5\n"
        "motion=claw\nmotion_fps=12\nmotion_frame_w=256\nmotion_frames=16\nhit_frame=10\nfx_frame=9\n"
        "fx=claw\nfx_frame_w=443\nfx_scale=0.75\nfx_anchor=203.6,82.2\nfx_origin=0,0\n",
        "timed-hit-test", nullptr);
    CHECK(spec.warnings.empty());
    const SkillDef& def = spec.def.skills[0];
    CHECK(def.usesTimedHit());
    CHECK(def.hitFrame == 10 && def.fxFrame == 9 && def.motionFrames == 16);
    CHECK(def.hasFxAnchor && def.fxOriginX == 0.f && def.fxOriginY == 0.f);
    const Species species = registerOrUpdateSpecies(spec.def);
    const Vec2 origin{600.f, 300.f};
    const auto scenario = [&](Vec2 aim) {
        World world;
        world.setBounds({0.f, 0.f, 1500.f, 1000.f});
        world.setupDragon(species, cfg::kMaxLevel, 0.f, origin);
        world.dragon().basicCd = 99.f;
        world.setSlots({0, -1, -1, -1});
        Enemy& enemy = world.spawnEnemy(defaultMelee(), 1, origin + aim * 80.f);
        enemy.radius = 8.f;
        enemy.hp = enemy.maxHp = 10000.f;
        enemy.stun = 99.f;
        return world;
    };
    PlayerInput cast{};
    cast.skill[0] = true;
    float fxDx[2] = {0.f, 0.f};
    int side = 0;
    for (Vec2 aim : {Vec2{1.f, 0.f}, Vec2{-1.f, 0.f}}) {
        auto world = scenario(aim);
        world.updateCombat(0.f, CombatMode::Direct, cast);
        CHECK(world.dragon().skillCast.has_value());
        CHECK(world.dragon().skillCd[0] == 4.5f);
        CHECK(world.enemies()[0].hp == 10000.f);  // 시전 순간에는 피해가 없다
        const size_t fxBefore = world.fx().size();
        PlayerInput move{};
        move.moveX = 1.f;
        world.updateCombat(0.70f, CombatMode::Direct, move);  // fx_frame(0.667초)은 지났고 hit_frame(0.75초) 전
        CHECK(distance(world.dragon().pos, origin) < 0.001f);  // 제자리
        CHECK(world.enemies()[0].hp == 10000.f);
        CHECK(world.fx().size() == fxBefore + 1);
        if (world.fx().size() == fxBefore + 1) fxDx[side] = world.fx().back().a.x - origin.x;
        world.updateCombat(0.06f, CombatMode::Direct, {});
        CHECK(world.enemies()[0].hp < 10000.f);  // hit_frame 에 맞는다
        world.updateCombat(0.30f, CombatMode::Direct, {});
        CHECK(!world.dragon().skillCast);  // 타격 + 0.25초 뒤 풀린다
        ++side;
    }
    CHECK(fxDx[0] > 0.f && std::fabs(fxDx[0] + fxDx[1]) < 0.01f);  // 왼쪽을 보면 좌우가 뒤집힌다

    // 타격 전 직접 조작 회피 → 끊기고 쿨타임은 절반만 남는다
    auto dodged = scenario({1.f, 0.f});
    dodged.updateCombat(0.f, CombatMode::Direct, cast);
    PlayerInput dodge{};
    dodge.dodge = true;
    dodged.updateCombat(0.3f, CombatMode::Direct, dodge);
    CHECK(!dodged.dragon().skillCast);
    CHECK(std::fabs(dodged.dragon().skillCd[0] - (4.5f * cfg::kSkillCastCancelCooldown)) < 0.05f);
    dodged.updateCombat(1.0f, CombatMode::Direct, {});
    CHECK(dodged.enemies()[0].hp == 10000.f);

    // 죽으면 피해 없이 끝난다
    auto dead = scenario({1.f, 0.f});
    dead.updateCombat(0.f, CombatMode::Direct, cast);
    dead.dragon().alive = false;
    dead.updateCombat(1.0f, CombatMode::Direct, {});
    CHECK(!dead.dragon().skillCast);
    CHECK(dead.enemies()[0].hp == 10000.f);

    // 전투가 끝나 끊기면 쿨타임을 모두 돌려준다
    auto ended = scenario({1.f, 0.f});
    ended.updateCombat(0.f, CombatMode::Direct, cast);
    ended.updateFx(0.1f, true);
    CHECK(!ended.dragon().skillCast);
    CHECK(ended.dragon().skillCd[0] == 0.f);
}

static void testRevenantCurse(const std::filesystem::path& assets) {
    std::printf("[revenant curse stacks]\n");
    loadDragonFolders(assets / "dragons");
    Species revenant{};
    const bool found = speciesFromKey("revenant", revenant);
    CHECK(found);
    if (!found) return;

    const auto& def = speciesDef(revenant);
    CHECK(def.skills.size() >= 2);
    if (def.skills.size() < 2) return;
    CHECK(def.skills[0].appliesCurse && def.skills[1].appliesCurse);
    CHECK(def.skills[1].fxZoneSpriteOnly && def.skills[1].fxAnim.fps == 10.f);

    auto setup = [](Species species) {
        World w;
        w.setBounds({0.f, 0.f, 1400.f, 650.f});
        w.setupDragon(species, 3, -1.f, {420.f, 450.f});
        w.setSlots({0, 1, -1, -1});
        w.dragon().basicCd = 100.f;
        return w;
    };
    auto enemyAt = [](World& w, Vec2 at) {
        Enemy& e = w.spawnEnemy(defaultMelee(), 1, at);
        e.hp = e.maxHp = 100000.f;
        e.stun = 100.f;
    };
    auto cast = [](World& w, int slot) {
        w.dragon().skillCd[static_cast<size_t>(slot)] = 0.f;
        PlayerInput in{};
        in.skill[slot] = true;
        w.updateCombat(0.f, CombatMode::Direct, in);
    };
    auto step = [](World& w, float seconds) { w.updateCombat(seconds, CombatMode::Direct, {}); };

    World bolts = setup(revenant);
    enemyAt(bolts, {520.f, 450.f});
    enemyAt(bolts, {720.f, 450.f});
    cast(bolts, 0);
    CHECK(bolts.enemies()[0].curseStacks == 0);
    CHECK(bolts.dragon().skillCd[0] == def.skills[0].cooldown);
    step(bolts, 0.25f);
    CHECK(bolts.enemies()[0].curseStacks == 1);
    CHECK(bolts.enemies()[1].curseStacks == 0);
    CHECK(bolts.enemies()[0].curseRemaining == 6.0);
    for (int i = 0; i < 5; ++i) {
        cast(bolts, 0);
        step(bolts, 0.25f);
    }
    CHECK(bolts.enemies()[0].curseStacks == 5);
    CHECK(bolts.enemies()[0].curseRemaining == 6.0);
    step(bolts, 5.75f);
    CHECK(bolts.enemies()[0].curseStacks == 5);
    cast(bolts, 0);
    step(bolts, 0.25f);
    CHECK(bolts.enemies()[0].curseStacks == 1);
    CHECK(bolts.enemies()[0].curseRemaining == 6.0);
    step(bolts, 6.f);
    CHECK(bolts.enemies()[0].curseStacks == 0);
    CHECK(bolts.enemies()[0].curseRemaining == 0.0);

    World basic = setup(revenant);
    enemyAt(basic, {520.f, 450.f});
    basic.dragon().basicCd = 0.f;
    step(basic, 0.25f);
    CHECK(basic.enemies()[0].curseStacks == 0 && basic.enemies()[0].hp < 100000.f);
    basic.enemiesMutable()[0].curseStacks = 3;
    basic.enemiesMutable()[0].curseRemaining = 1.0;
    basic.dragon().basicCd = 0.f;
    step(basic, 0.25f);
    CHECK(basic.enemies()[0].curseStacks == 3 && basic.enemies()[0].curseRemaining == kCurseDuration);

    for (int index : {2, 3, 4}) {
        World blast = setup(revenant);
        blast.setupDragon(revenant, 8, -1.f, {420.f, 450.f});
        blast.dragon().basicCd = 100.f;
        blast.setSlots({index, -1, -1, -1});
        enemyAt(blast, {500.f, 450.f});
        enemyAt(blast, {520.f, 452.f});
        for (auto& enemy : blast.enemiesMutable()) enemy.curseRemaining = kCurseDuration;
        blast.enemiesMutable()[0].curseStacks = kCurseMaxStacks;
        blast.enemiesMutable()[1].curseStacks = kCurseMaxStacks - 1;
        cast(blast, 0);
        const float normal = mitigate(blast.dragon().stats.atk * def.skills[index].power, blast.enemies()[0].def);
        CHECK(std::fabs((100000.f - blast.enemies()[0].hp) - 2.f * normal) < 0.01f);
        CHECK(std::fabs((100000.f - blast.enemies()[1].hp) - 1.8f * normal) < 0.01f);
        CHECK(blast.enemies()[0].curseStacks == 0 && blast.enemies()[1].curseStacks == 0);
        CHECK(std::any_of(blast.texts().begin(), blast.texts().end(), [](const FloatText& text) {
            return text.text.find("저주 폭발!") != std::string::npos;
        }));
    }

    const auto breathWorld = [&] {
        World w = setup(revenant);
        w.setupDragon(revenant, 8, -1.f, {420.f, 450.f});
        w.dragon().basicCd = 100.f;
        w.setSlots({5, -1, -1, -1});
        enemyAt(w, {520.f, 450.f});
        enemyAt(w, {600.f, 620.f});
        auto& boss = w.spawnEnemy(defaultBoss(), 8, {650.f, 520.f});
        boss.hp = boss.maxHp = 100000.f;
        boss.stun = 100.f;
        cast(w, 0);
        return w;
    };
    World sustained = breathWorld();
    CHECK(sustained.dragon().skillCd[5] == 25.f);
    auto& target = sustained.enemiesMutable()[0];
    const float tick = mitigate(sustained.dragon().stats.atk * 1.56f, target.def);
    target.curseStacks = kCurseMaxStacks;
    target.curseRemaining = kCurseDuration;
    step(sustained, 0.47f);
    CHECK(target.hp == 100000.f);
    step(sustained, 0.01f);
    CHECK(target.curseStacks == 0 && target.pos.x == 550.f);
    CHECK(std::fabs(100000.f - target.hp - 2.f * tick) < 0.01f);
    target.curseStacks = kCurseMaxStacks;
    target.curseRemaining = kCurseDuration;
    step(sustained, 0.75f);
    CHECK(target.curseStacks == kCurseMaxStacks);
    CHECK(std::fabs(100000.f - target.hp - 5.f * tick) < 0.04f);
    target.pos.y = 620.f;
    step(sustained, 0.25f);
    const float outsideHp = target.hp;
    target.pos.y = 450.f;
    step(sustained, 0.25f);
    CHECK(std::fabs(outsideHp - target.hp - tick) < 0.01f);
    step(sustained, 3.f);
    CHECK(!sustained.dragon().breath && sustained.enemies()[1].hp == 100000.f);
    CHECK(sustained.enemies()[2].hp < 100000.f && sustained.enemies()[2].pos.x == 650.f);

    World coarse = breathWorld();
    World fine = breathWorld();
    step(coarse, 4.f);
    for (int i = 0; i < 400; ++i) step(fine, 0.01f);
    CHECK(coarse.enemies()[0].hp == fine.enemies()[0].hp && coarse.enemies()[0].pos.x == 820.f);
    CHECK(std::fabs(100000.f - coarse.enemies()[0].hp - 10.f * tick) < 0.05f);
    World interrupted = breathWorld();
    step(interrupted, 0.5f);
    const float interruptedHp = interrupted.enemies()[0].hp;
    interrupted.updateFx(10.f, true);
    step(interrupted, 4.f);
    CHECK(!interrupted.dragon().breath && interrupted.enemies()[0].hp == interruptedHp);

    for (int level : {7, 8, 9, 24}) {
        World control = breathWorld();
        auto& enemy = control.enemiesMutable()[0];
        enemy.level = level;
        step(control, 0.47f);
        enemy.stun = 0.f;
        const float before = enemy.pos.x;
        step(control, 0.01f);
        step(control, 0.10f);
        if (level < 8) CHECK(enemy.pos.x == before + 12.f);
        else if (level == 8) CHECK(enemy.pos.x == before);
        else {
            const float factor = level == 9 ? 0.35f : 0.75f;
            CHECK(std::fabs(enemy.pos.x - (before - enemy.speed * 0.11f * factor)) < 0.01f);
        }
    }

    World edge = breathWorld();
    const float rightEdge = 543.f - edge.enemies()[0].radius;
    edge.setBounds({0.f, 0.f, 543.f, 650.f});
    edge.enemiesMutable()[0].pos.x = rightEdge - 0.5f;
    step(edge, 0.5f);
    CHECK(edge.enemies()[0].pos.x == rightEdge);

    World lostTarget = setup(revenant);
    enemyAt(lostTarget, {520.f, 450.f});
    enemyAt(lostTarget, {620.f, 450.f});
    cast(lostTarget, 0);
    CHECK(lostTarget.projectiles().size() == 1);
    lostTarget.enemiesMutable()[0].alive = false;
    step(lostTarget, 0.25f);
    CHECK(lostTarget.projectiles().empty());
    CHECK(lostTarget.enemies().size() == 1);
    CHECK(lostTarget.enemies()[0].curseStacks == 0);
    lostTarget.enemiesMutable()[0].pos = {1100.f, 450.f};
    cast(lostTarget, 0);
    CHECK(lostTarget.projectiles().empty());

    World zones = setup(revenant);
    enemyAt(zones, {520.f, 450.f});
    enemyAt(zones, {1100.f, 450.f});
    cast(zones, 1);
    CHECK(zones.dragon().skillCd[1] == def.skills[1].cooldown);
    cast(zones, 1);
    CHECK(zones.dots().size() == 2);
    CHECK(zones.enemies()[0].curseStacks == 0);
    step(zones, 0.125f);
    CHECK(zones.enemies()[0].curseStacks == 1);
    CHECK(zones.enemies()[1].curseStacks == 0);
    zones.enemiesMutable()[1].pos = {530.f, 450.f};
    step(zones, 0.5f);
    CHECK(zones.enemies()[0].curseStacks == 1);
    CHECK(zones.enemies()[1].curseStacks == 1);
    CHECK(zones.enemies()[0].curseRemaining < zones.enemies()[1].curseRemaining);
    step(zones, 0.375f);
    CHECK(zones.enemies()[0].curseStacks == 1);
    step(zones, 0.125f);
    CHECK(zones.enemies()[0].curseStacks == 2);
    CHECK(zones.enemies()[1].curseStacks == 1);
    for (int i = 0; i < 3; ++i) step(zones, 1.f);
    CHECK(zones.enemies()[0].curseStacks == 5);
    step(zones, 1.f);  // 만료를 넘긴 마지막 피해 틱은 저주를 갱신하지 않는다.
    CHECK(zones.dots().empty());
    CHECK(zones.enemies()[0].curseStacks == 5);
    CHECK(zones.enemies()[0].curseRemaining == 5.0);
    zones.enemiesMutable()[0].hp = 1.f;
    cast(zones, 0);
    step(zones, 0.25f);
    CHECK(zones.enemies().size() == 1);
    enemyAt(zones, {520.f, 450.f});
    CHECK(zones.enemies().back().curseStacks == 0);
    CHECK(zones.enemies().back().curseZoneCooldown == 0.0);

    SpeciesDef otherDef = def;
    otherDef.key = "test-curse-other";
    const Species otherSpecies = registerOrUpdateSpecies(otherDef);
    World other = setup(otherSpecies);
    World compare = setup(revenant);
    for (World* w : {&other, &compare}) {
        enemyAt(*w, {520.f, 450.f});
        cast(*w, 0);
        step(*w, 0.25f);
        cast(*w, 1);
        step(*w, 0.25f);
    }
    CHECK(other.enemies()[0].curseStacks == 0);
    CHECK(compare.enemies()[0].curseStacks == 2);
    CHECK(other.enemies()[0].hp == compare.enemies()[0].hp);

    const std::string oldFormat =
        "version=1\ndragon.present=1\ndragon.id=7\ndragon.species=revenant\n"
        "dragon.alive=1\ndragon.level=6\ndragon.slots=0,1,2,3\n";
    SaveData loaded;
    CHECK(deserializeSave(oldFormat, loaded));
    CHECK(loaded.hasDragon && loaded.dragon.species == revenant);
    const std::array<int, kSkillSlots> oldSlots{0, 1, 2, 3};
    CHECK(loaded.dragon.slots == oldSlots);
    const std::string reserialized = serializeSave(loaded);
    CHECK(reserialized.find("curse") == std::string::npos);
}

static void testZoneSprite() {
    const auto data = parseDragonText(
        "[dragon]\nname=Zone QA\n"
        "[skill.1]\nkind=Zone\nduration=5\nradius=140\nrange=340\n"
        "scale_with_size=yes\nfx=fog\nfx_zone_sprite_only=yes\nfx_fps=10\n"
        "[skill.2]\nkind=Zone\nduration=5\nradius=140\nrange=340\nfx=legacy\n",
        "zone-sprite-qa", nullptr);
    CHECK(data.warnings.empty());
    const auto species = registerOrUpdateSpecies(data.def);
    World world;
    world.setBounds({0, 0, 1000, 500});
    world.setSizeScale(3.f);
    world.setupDragon(species, cfg::kMaxLevel, 0.f, {400, 250});
    world.dragon().basicCd = 99.f;
    world.setSlots({0, 1, -1, -1});
    auto& target = world.spawnEnemy(defaultMelee(), 1, {480, 250});
    target.stun = 100.f;
    target.hp = target.maxHp = 10000.f;
    PlayerInput input{};
    input.skill[0] = input.skill[1] = true;
    world.updateCombat(0.f, CombatMode::Direct, input);
    CHECK(world.dots().size() == 2);
    if (world.dots().size() != 2) return;
    CHECK(world.dots()[0].spriteSkill == 0 && world.dots()[0].radius == 420.f);
    CHECK(world.dots()[1].spriteSkill == -1 && world.dots()[1].radius == 140.f);
    CHECK(std::none_of(world.fx().begin(), world.fx().end(), [](const Fx& fx) { return fx.key == "fog"; }));
    CHECK(std::any_of(world.fx().begin(), world.fx().end(), [](const Fx& fx) { return fx.key == "legacy"; }));
    for (int tick = 0; tick < 499; ++tick)
        world.updateCombat(0.01f, CombatMode::Direct, {});
    CHECK(world.dots().size() == 2);
    CHECK(world.enemies().empty() || world.enemies().front().hp < 10000.f);
    world.updateCombat(0.02f, CombatMode::Direct, {});
    CHECK(world.dots().empty());
}

static void testRevenantSouls(const std::filesystem::path& assets) {
    loadDragonFolders(assets / "dragons");
    loadEnemyFolders(assets / "enemies");
    Species revenant{}, shadow{};
    const bool loaded = speciesFromKey("revenant", revenant) && speciesFromKey("shadow", shadow);
    CHECK(loaded);
    if (!loaded) return;
    const auto& skills = speciesDef(revenant).skills;
    CHECK(skills.size() >= 7);
    if (skills.size() < 7) return;
    const SkillDef& harvest = skills[6];
    CHECK(harvest.grantsSoul && harvest.kind == SkillKind::PassiveOnKill && harvest.power == 0.06f);

    const auto prepareStrike = [](World& world) {
        world.dragon().basicCd = 99.f;
        world.dragon().skillCd[3] = world.dragon().skillCd[4] = 0.f;
        world.setSlots({3, 4, -1, -1});
        PlayerInput strike{};
        strike.skill[0] = strike.skill[1] = true;  // Both skills attempt to hit the same target.
        return strike;
    };
    const auto kill = [&](World& world) {
        auto& enemy = world.spawnEnemy(defaultMelee(), 1, world.dragon().pos + Vec2{55.f, 0.f});
        enemy.hp = 1.f;
        enemy.stun = 100.f;
        world.updateCombat(0.f, CombatMode::Direct, prepareStrike(world));
        CHECK(world.aliveEnemyCount() == 0);
        const auto events = world.takeEvents();
        CHECK(std::count_if(events.begin(), events.end(), [](const WorldEvent& event) {
            return event.type == WorldEventType::EnemyKilled;
        }) == 1);
        world.updateCombat(0.f, CombatMode::Direct, {});
    };
    const auto hasSoulText = [](const World& world) {
        return std::any_of(world.texts().begin(), world.texts().end(), [](const FloatText& text) {
            return text.text == "영혼 +1";
        });
    };
    const auto hasHarvestFx = [](const World& world) {
        return std::any_of(world.fx().begin(), world.fx().end(), [](const Fx& effect) {
            return effect.key == "harvest";
        });
    };

    World locked;
    locked.setupDragon(revenant, harvest.unlockLevel - 1, 1.f, {400, 250});
    kill(locked);
    CHECK(locked.dragon().souls == 0 && locked.dragon().hp == 1.f);
    CHECK(!hasSoulText(locked) && !hasHarvestFx(locked));

    World world;
    world.setupDragon(revenant, harvest.unlockLevel, -1.f, {400, 250});
    kill(world);
    CHECK(world.dragon().souls == 1 && world.dragon().hp == world.dragon().stats.maxHp);
    CHECK(hasSoulText(world) && !hasHarvestFx(world));
    world.dragon().hp = 1.f;
    kill(world);
    const float healedHp = 1.f + 0.06f * world.dragon().stats.maxHp;
    CHECK(world.dragon().souls == 2 && std::fabs(world.dragon().hp - healedHp) < 0.001f);
    CHECK(hasHarvestFx(world));
    for (int i = 2; i < 12; ++i) {
        kill(world);
        CHECK(world.dragon().souls == std::min(i + 1, kRevenantSoulCap));
    }
    world.updateFx(1.f);
    world.dragon().hp = 1.f;
    kill(world);
    CHECK(world.dragon().souls == kRevenantSoulCap && !hasSoulText(world));
    CHECK(std::fabs(world.dragon().hp - healedHp) < 0.001f);
    world.clearEnemies(false);
    CHECK(world.dragon().souls == kRevenantSoulCap);

    SpeciesDef otherDef = speciesDef(revenant);
    otherDef.key = "soul-passive-qa";
    const auto other = registerOrUpdateSpecies(otherDef);
    World otherWorld;
    otherWorld.setupDragon(other, harvest.unlockLevel, 1.f, {400, 250});
    kill(otherWorld);
    CHECK(otherWorld.dragon().souls == 0 && otherWorld.dragon().hp > 1.f);
    CHECK(!hasSoulText(otherWorld) && hasHarvestFx(otherWorld));

    SaveData save;
    save.hasDragon = true;
    save.dragon.species = revenant;
    save.dragon.level = harvest.unlockLevel;
    save.dragon.id = 1;
    save.dragon.slots = {3, 4, 0, 1};
    save.activeIndex = 0;
    save.stable = {save.dragon, save.dragon};
    save.stable[1].id = 2;
    save.stable[1].species = shadow;
    save.nextDragonId = 3;
    Game game;
    game.init(save, kDesk);
    game.setHuntMode(CombatMode::Direct);
    game.world().dragon().souls = 4;
    game.startHunt();
    CHECK(game.hunting() && game.world().dragon().souls == 0);
    kill(game.world());
    CHECK(game.world().dragon().souls == 1);
    game.stopHunt("");
    CHECK(!game.hunting() && game.world().dragon().souls == 0);
    game.startHunt();
    CHECK(game.hunting() && game.world().dragon().souls == 0);
    game.stopHunt("");
    game.setHuntMode(CombatMode::Auto);
    game.world().dragon().souls = 4;
    game.startHunt();
    game.confirmAutoWarning();
    CHECK(game.hunting() && game.world().dragon().souls == 0);
    game.stopHunt("");

    game.world().dragon().souls = 2;
    CHECK(game.switchDragon(1) && game.world().dragon().souls == 0);
    CHECK(game.switchDragon(0) && game.world().dragon().souls == 0);
    game.world().dragon().souls = 4;
    const auto serialized = serializeSave(game.snapshotForSave());
    CHECK(serialized.find("souls") == std::string::npos);
    SaveData decoded;
    CHECK(deserializeSave(serialized, decoded));
    Game restarted;
    restarted.init(decoded, kDesk);
    CHECK(restarted.world().dragon().souls == 0);
    CHECK(restarted.skillSlots() == save.dragon.slots);

    TowerFloorDef floor;
    floor.spawns.push_back(TowerSpawn{});
    game.setTowerFloors({floor, floor});
    const auto enterTower = [&] {
        game.openTowerWarning();
        game.enterTower(CombatMode::Direct);
        CHECK(game.screen() == Screen::Tower && game.world().dragon().souls == 0);
    };
    const auto clearFloor = [&] {
        for (Enemy& enemy : game.world().enemiesMutable()) {
            enemy.pos = game.world().dragon().pos + Vec2{55.f, 0.f};
            enemy.hp = 1.f;
            enemy.stun = 100.f;
        }
        game.update(0.f, prepareStrike(game.world()), false, true);
    };
    enterTower();
    clearFloor();
    CHECK(game.screen() == Screen::FloorClear && game.world().dragon().souls == 1);
    game.nextFloor();
    CHECK(game.screen() == Screen::Tower && game.world().dragon().souls == 1);
    clearFloor();
    CHECK(game.screen() == Screen::TowerVictory && game.world().dragon().souls == 0);
    game.leaveTower();
    enterTower();
    clearFloor();
    CHECK(game.screen() == Screen::FloorClear && game.world().dragon().souls == 1);
    game.leaveTower();
    CHECK(game.screen() == Screen::Desktop && game.world().dragon().souls == 0);

    game.setHuntMode(CombatMode::Direct);
    game.startHunt();
    kill(game.world());
    game.world().dragon().hp = 1.f;
    game.world().dragon().stats.regen = 0.f;
    game.world().dragon().invuln = 0.f;
    game.world().spawnEnemy(defaultMelee(), 15, game.world().dragon().pos + Vec2{35.f, 0.f});
    stepGame(game, 4.f, false, true);
    CHECK(game.screen() == Screen::Death && game.world().dragon().souls == 0);
}

static void testRevenantUltimateCharge() {
    std::printf("[revenant ultimate charge]\n");
    loadDragonFolders("assets/dragons");
    Species revenant{}, shadow{};
    const bool loaded = speciesFromKey("revenant", revenant) && speciesFromKey("shadow", shadow);
    CHECK(loaded);
    if (!loaded) return;

    const SpeciesDef& def = speciesDef(revenant);
    const auto harvest = std::find_if(def.skills.begin(), def.skills.end(), [](const SkillDef& skill) {
        return skill.kind == SkillKind::PassiveOnKill && skill.grantsSoul;
    });
    CHECK(harvest != def.skills.end());
    if (harvest == def.skills.end()) return;

    World world;
    world.setupDragon(revenant, harvest->unlockLevel, -1.f, {400.f, 250.f});
    const UltimateUseConditions allowed{true, true, true};
    for (const int souls : {0, 1, 5, 9, 10, 11}) {
        world.dragon().souls = souls;
        const auto charge = world.ultimateCharge();
        CHECK(charge.souls == std::min(souls, 10));
        CHECK(charge.percent() == std::min(souls * 10, 100));
        CHECK(charge.full() == (souls >= 10));
        CHECK(world.canUseUltimate(allowed) == charge.full());
    }

    world.dragon().souls = 10;
    CHECK(!world.canUseUltimate({false, true, true}));
    CHECK(!world.canUseUltimate({true, false, true}));
    CHECK(!world.canUseUltimate({true, true, false}));
    CHECK(!world.beginUltimatePayment({}));
    CHECK(!world.commitUltimatePayment(0));
    auto cancelled = world.beginUltimatePayment(allowed);
    CHECK(cancelled.has_value());
    if (!cancelled) return;
    CHECK(world.ultimateCharge().full() && world.dragon().souls == 10);
    CHECK(!world.canUseUltimate(allowed) && !world.beginUltimatePayment(allowed));
    world.cancelUltimatePayment(*cancelled + 1);
    CHECK(!world.canUseUltimate(allowed));
    world.cancelUltimatePayment(*cancelled);
    CHECK(world.canUseUltimate(allowed) && world.dragon().souls == 10);
    CHECK(!world.commitUltimatePayment(*cancelled));

    auto hit = world.beginUltimatePayment(allowed);
    CHECK(hit.has_value());
    if (!hit) return;
    CHECK(!world.commitUltimatePayment(*cancelled));
    CHECK(world.commitUltimatePayment(*hit));
    CHECK(world.ultimateCharge().percent() == 0 && !world.ultimateCharge().full());
    world.dragon().souls = 10;
    CHECK(!world.commitUltimatePayment(*hit) && world.dragon().souls == 10);

    auto shortfall = world.beginUltimatePayment(allowed);
    CHECK(shortfall.has_value());
    if (!shortfall) return;
    world.dragon().souls = 9;
    CHECK(!world.commitUltimatePayment(*shortfall) && world.dragon().souls == 9);
    CHECK(!world.beginUltimatePayment(allowed));
    world.dragon().souls = 10;
    auto oldSession = world.beginUltimatePayment(allowed);
    CHECK(oldSession.has_value());
    if (!oldSession) return;
    world.resetSouls();
    CHECK(world.ultimateCharge().percent() == 0);
    world.dragon().souls = 10;
    CHECK(!world.commitUltimatePayment(*oldSession) && world.dragon().souls == 10);
    auto beforeSwitch = world.beginUltimatePayment(allowed);
    CHECK(beforeSwitch.has_value());
    if (!beforeSwitch) return;
    world.setupDragon(revenant, harvest->unlockLevel, -1.f, {400.f, 250.f});
    world.dragon().souls = 10;
    CHECK(!world.commitUltimatePayment(*beforeSwitch));

    world.setupDragon(revenant, harvest->unlockLevel - 1, -1.f, {400.f, 250.f});
    world.dragon().souls = 10;
    CHECK(!world.ultimateCharge().soulHarvestUnlocked && world.ultimateCharge().percent() == 0);
    CHECK(!world.beginUltimatePayment(allowed));
    world.setupDragon(shadow, harvest->unlockLevel, -1.f, {400.f, 250.f});
    world.dragon().souls = 10;
    CHECK(!world.ultimateCharge().full() && !world.canUseUltimate(allowed));

    SaveData saved;
    CHECK(deserializeSave("version=1\ndragon.present=1\ndragon.species=revenant\n"
                          "dragon.alive=1\ndragon.level=7\ndragon.slots=0,1,2,3\n", saved));
    CHECK(saved.hasDragon && saved.dragon.species == revenant && saved.dragon.slots[0] == 0);
    Game game;
    game.init(saved, {0.f, 0.f, 960.f, 540.f});
    CHECK(game.world().ultimateCharge().percent() == 0);
    CHECK(serializeSave(saved).find("ultimate") == std::string::npos);
}

static void testRevenantProcession() {
    std::printf("[revenant procession]\n");
    loadDragonFolders("assets/dragons");
    Species revenant{}, shadow{};
    const bool loaded = speciesFromKey("revenant", revenant) && speciesFromKey("shadow", shadow);
    CHECK(loaded);
    if (!loaded) return;
    constexpr int skill = 7;
    const auto& def = speciesDef(revenant);
    CHECK(def.skills.size() > skill);
    if (def.skills.size() <= skill) return;
    CHECK(def.skills[skill].kind == SkillKind::Procession && def.skills[skill].unlockLevel == 8);
    CHECK(def.skills[skill].cooldown == 30.f && def.skills[skill].motion == "procession");
    for (int i = 0; i < 7; ++i) CHECK(def.skills[i].kind != SkillKind::Procession);

    const auto setup = [&](int souls = 10, int level = 8) {
        World w;
        w.setBounds({0.f, 0.f, 1400.f, 640.f});
        w.setupDragon(revenant, level, -1.f, {400.f, 300.f});
        w.dragon().souls = souls;
        w.dragon().basicCd = 90.f;
        w.setSlots({skill, -1, -1, -1});
        for (Vec2 position : {Vec2{620.f, 300.f}, Vec2{680.f, 330.f}, Vec2{900.f, 300.f}}) {
            auto& e = w.spawnEnemy(defaultMelee(), 1, position);
            e.hp = e.maxHp = 100000.f;
            e.stun = 100.f;
        }
        return w;
    };
    // 2026-11-20: 궁극기는 장착 칸 밖의 전용 칸이다 — 키 5(PlayerInput::ultimate)로 쏜다.
    CHECK(def.skills[skill].ultimate && def.skills[skill].ultimateUsesSouls);
    for (int i = 0; i < 7; ++i) CHECK(!def.skills[i].ultimate);
    PlayerInput input{};
    input.ultimate = true;
    const auto cast = [&](World& w, CombatMode mode = CombatMode::Direct) { w.updateCombat(0.f, mode, input); };
    const auto step = [](World& w, float dt) { w.updateCombat(dt, CombatMode::Direct, {}); };
    const auto activeCast = [](const World& world) -> const ProcessionCast& {
        const auto& active = world.dragon().procession;
        if (!active) {
            CHECK(active.has_value());
            std::exit(EXIT_FAILURE);
        }
        return *active;
    };
    World shortfall = setup(9);
    cast(shortfall);
    CHECK(!shortfall.dragon().procession && shortfall.dragon().souls == 9);
    World locked = setup(10, 7);
    cast(locked);
    CHECK(!locked.dragon().procession && locked.dragon().souls == 10);
    // 칸에 넣으려 해도 빠지고, 칸에 없어도 쓸 수 있다.
    World unequipped = setup();
    CHECK(unequipped.slotSkill(0) == -1);
    unequipped.setSlots({-1, -1, -1, -1});
    CHECK(unequipped.ultimateSkill() == skill && !unequipped.ultimateBlockedReason(true));
    cast(unequipped);
    CHECK(unequipped.dragon().procession && unequipped.dragon().souls == 10);
    // 자동 모드에서는 AI 가 쓴다(키 입력이 없어도). 영혼이 모자라면 쓰지 않는다.
    World automatic = setup();
    automatic.updateCombat(0.f, CombatMode::Auto, {});
    CHECK(automatic.dragon().procession && automatic.dragon().souls == 10);
    for (int tick = 0; tick < 250; ++tick) automatic.updateCombat(0.02f, CombatMode::Auto, {});
    CHECK(automatic.dragon().souls == 0 && automatic.dragon().skillCd[skill] > 0.f);
    CHECK(automatic.enemies()[0].hp < 100000.f);

    World movingDuringCast = setup();
    cast(movingDuringCast);
    const Vec2 castOrigin = movingDuringCast.dragon().pos;
    PlayerInput moveDuringCast{};
    moveDuringCast.moveX = 1.f;
    movingDuringCast.updateCombat(1.f, CombatMode::Direct, moveDuringCast);
    CHECK(movingDuringCast.dragon().pos.x == castOrigin.x);
    movingDuringCast.updateCombat(0.45f, CombatMode::Direct, moveDuringCast);
    const auto& active = movingDuringCast.dragon().procession;
    CHECK(active && active->launched == 1 && active->origin.x == castOrigin.x);
    CHECK(movingDuringCast.dragon().pos.x > castOrigin.x);
    World autoShort = setup(9);
    autoShort.updateCombat(0.f, CombatMode::Auto, {});
    CHECK(!autoShort.dragon().procession && autoShort.dragon().souls == 9);
    // 상태창 아이콘 클릭(요청)은 자동 모드에서도 받는다.
    World clicked = setup();
    clicked.requestUltimate();
    clicked.updateCombat(0.f, CombatMode::Auto, {});
    CHECK(clicked.dragon().procession.has_value());

    CHECK(def.skills[skill].processionShots == 3);
    CHECK(def.skills[skill].processionInterval == 0.4f);
    CHECK(def.skills[skill].processionSpeed == 620.f && def.skills[skill].processionLifetime == 2.f);
    CHECK(def.skills[skill].power == 11.69f && def.skills[skill].radius == 110.f);

    World w = setup();
    const int firstId = w.enemies()[0].id, secondId = w.enemies()[1].id, thirdId = w.enemies()[2].id;
    w.enemiesMutable()[0].curseStacks = 3;
    w.enemiesMutable()[0].curseRemaining = 6.0;
    cast(w, CombatMode::SkillManual);
    CHECK(w.dragon().procession.has_value());
    if (!w.dragon().procession) return;
    CHECK(w.dragon().skillMotion == skill && w.dragon().souls == 10);
    step(w, 1.39f);
    CHECK(activeCast(w).launched == 0);
    step(w, 0.06f);
    CHECK(activeCast(w).launched == 1 && activeCast(w).waves.size() == 1);
    CHECK(activeCast(w).waves[0].targetId == firstId);
    CHECK(activeCast(w).waves[0].pos.x > activeCast(w).launchOrigin.x);
    CHECK(w.enemies()[0].hp == 100000.f && w.dragon().souls == 10);
    step(w, 0.34f);
    CHECK(activeCast(w).launched == 1);
    step(w, 0.06f);
    CHECK(activeCast(w).launched == 2 && activeCast(w).waves[1].targetId == secondId);
    CHECK(activeCast(w).paid && w.dragon().souls == 0 && w.ultimateCharge().percent() == 0);
    CHECK(w.dragon().skillCd[skill] == 30.f);
    CHECK(w.enemies()[0].hp < 100000.f && w.enemies()[1].hp < 100000.f && w.enemies()[2].hp == 100000.f);
    CHECK(w.enemies()[0].curseStacks == 3 && w.enemies()[1].curseStacks == 0);
    const float firstHitHp = w.enemies()[0].hp;
    step(w, 0.39f);
    CHECK(activeCast(w).launched == 3 && activeCast(w).waves[2].targetId == thirdId);
    CHECK(w.enemies()[0].hp == firstHitHp);  // 첫 충돌은 피해를 반복하지 않는다.
    w.dragon().souls = 10;
    step(w, 0.30f);
    CHECK(w.enemies()[0].hp < firstHitHp && w.dragon().souls == 10);  // 두 번째 폭발은 같은 적을 맞힐 수 있다.
    step(w, 1.f);
    CHECK(w.enemies()[2].hp < 100000.f && activeCast(w).launched == 3);
    step(w, 1.f);
    CHECK(!w.dragon().procession);
    cast(w);
    CHECK(!w.dragon().procession);  // 재사용 대기 중이다.

    World moving = setup();
    cast(moving);
    step(moving, 1.45f);
    const float priorY = activeCast(moving).waves[0].pos.y;
    moving.enemiesMutable()[0].pos.y += 100.f;
    step(moving, 0.1f);
    CHECK(activeCast(moving).waves[0].pos.y > priorY);
    const int replacement = moving.enemies()[1].id;
    moving.enemiesMutable()[0].alive = false;
    step(moving, 0.1f);
    CHECK(activeCast(moving).waves[0].targetId == replacement);
    moving.enemiesMutable()[0].pos.x += 600.f;
    step(moving, 0.1f);
    CHECK(activeCast(moving).waves[0].targetId == replacement);

    World wide = setup();
    wide.setBounds({0.f, 0.f, 3840.f, 2160.f});
    wide.enemiesMutable()[0].pos = {3740.f, 2060.f};
    wide.enemiesMutable()[1].alive = wide.enemiesMutable()[2].alive = false;
    const int farTarget = wide.enemies()[0].id;
    cast(wide);
    CHECK(wide.dragon().procession.has_value());
    step(wide, 4.5f);
    CHECK(wide.dragon().procession && activeCast(wide).waves.size() == 3);
    if (wide.dragon().procession)
        for (const auto& wave : activeCast(wide).waves)
            CHECK(wave.targetId == farTarget && !wave.impacted);
    CHECK(wide.enemies()[0].hp == 100000.f && wide.dragon().souls == 10);
    step(wide, 5.f);
    CHECK(wide.enemies()[0].hp < 100000.f && wide.dragon().souls == 0);
    CHECK(!wide.dragon().procession);

    World single = setup();
    single.enemiesMutable()[1].alive = single.enemiesMutable()[2].alive = false;
    cast(single);
    step(single, 2.25f);
    CHECK(single.dragon().procession && activeCast(single).launched == 3);
    CHECK(std::all_of(activeCast(single).waves.begin(), activeCast(single).waves.end(),
                      [&](const ProcessionWave& wave) { return wave.targetId == single.enemies()[0].id; }));
    const float oneHit = 100000.f - firstHitHp;
    step(single, 2.5f);
    CHECK(!single.dragon().procession);
    CHECK(std::fabs((100000.f - single.enemies()[0].hp) - 3.f * oneHit) < 0.03f);

    for (const auto& [kind, level] : {std::pair{defaultMelee(), 1}, std::pair{defaultMelee(), 8},
                                     std::pair{defaultMelee(), 20}, std::pair{defaultBoss(), 8}}) {
        World balance = setup();
        balance.enemiesMutable().clear();
        auto& enemy = balance.spawnEnemy(kind, level, {620.f, 300.f});
        const float naturalHp = enemy.maxHp;
        enemy.hp = enemy.maxHp = 100000.f;
        enemy.stun = 100.f;
        enemy.curseStacks = kCurseMaxStacks;
        enemy.curseRemaining = kCurseDuration;
        const float oldOne = mitigate(balance.dragon().stats.atk * 1.67f, enemy.def);
        cast(balance);
        step(balance, 2.f);
        const float first = 100000.f - balance.enemies()[0].hp;
        step(balance, 3.f);
        const float total = 100000.f - balance.enemies()[0].hp;
        std::printf("  procession %s Lv.%d: hp=%.1f old=%.1f/%.1f new=%.1f/%.1f\n",
                    enemyDef(kind).key.c_str(), level, naturalHp, oldOne, 3.f * oldOne, first, total);
        CHECK(std::fabs(first - 7.f * oldOne) < 0.01f);
        CHECK(std::fabs(total - 21.f * oldOne) < 0.03f);
        CHECK(balance.enemies()[0].curseStacks == kCurseMaxStacks && balance.dragon().souls == 0);
    }

    World kills = setup();
    kills.enemiesMutable()[0].hp = 1.f;
    kills.enemiesMutable()[1].hp = 1.f;
    cast(kills);
    step(kills, 2.f);
    CHECK(kills.enemies().size() == 1 && kills.dragon().souls == 2);

    World dodged = setup();
    cast(dodged);
    step(dodged, 1.45f);  // 첫 군세가 움직이는 동안 취소한다.
    PlayerInput dodge{};
    dodge.dodge = true;
    dodged.updateCombat(2.f, CombatMode::Direct, dodge);
    CHECK(!dodged.dragon().procession && dodged.dragon().souls == 10 && dodged.dragon().skillCd[skill] == 0.f);
    CHECK(dodged.enemies()[0].hp == 100000.f);
    World paidCancel = setup();
    cast(paidCancel);
    step(paidCancel, 1.85f);
    CHECK(paidCancel.dragon().procession && activeCast(paidCancel).paid);
    const float paidHp = paidCancel.enemies()[0].hp;
    paidCancel.updateCombat(1.f, CombatMode::Direct, dodge);
    CHECK(!paidCancel.dragon().procession && paidCancel.dragon().souls == 0);
    CHECK(paidCancel.dragon().skillCd[skill] > 0.f && paidCancel.enemies()[0].hp == paidHp);

    World missed = setup();
    cast(missed);
    for (auto& enemy : missed.enemiesMutable()) enemy.alive = false;
    step(missed, 2.25f);
    CHECK(missed.dragon().procession && activeCast(missed).launched == 3);
    step(missed, 2.5f);
    CHECK(!missed.dragon().procession && missed.dragon().souls == 10);
    CHECK(missed.enemies().empty());

    World ended = setup();
    cast(ended);
    ended.updateFx(0.f, true);
    CHECK(!ended.dragon().procession && ended.dragon().souls == 10);
    CHECK(ended.dragon().anim == AnimState::Idle);
    cast(ended);
    ended.resetSouls();
    CHECK(!ended.dragon().procession && ended.dragon().souls == 0);
    ended.dragon().souls = 10;
    cast(ended);
    ended.clearEnemies(false);
    CHECK(!ended.dragon().procession && ended.dragon().souls == 10);
    World died = setup();
    cast(died);
    died.dragon().alive = false;
    step(died, 2.f);
    CHECK(!died.dragon().procession && died.enemies()[0].hp == 100000.f);
    World swapped = setup();
    cast(swapped);
    swapped.setupDragon(shadow, 8, -1.f, {400.f, 300.f});
    CHECK(!swapped.dragon().procession && swapped.dragon().souls == 0);
    cast(swapped);
    CHECK(!swapped.dragon().procession);

    World mirrored = setup();
    for (auto& enemy : mirrored.enemiesMutable()) enemy.pos.x = 800.f - enemy.pos.x;
    cast(mirrored);
    CHECK(mirrored.dragon().procession && activeCast(mirrored).facingLeft);

    SaveData saved;
    CHECK(deserializeSave("version=1\ndragon.present=1\ndragon.species=revenant\ndragon.alive=1\n"
                          "dragon.level=8\ndragon.slots=0,1,2,3\n", saved));
    const std::array<int, kSkillSlots> originalSlots{0, 1, 2, 3};
    CHECK(saved.dragon.slots == originalSlots);
    Game game;
    game.init(saved, {0.f, 0.f, 1280.f, 720.f});
    CHECK(!game.equipSkill(3, skill));  // 궁극기는 칸에 넣지 않는다
    CHECK(game.skillSlots()[3] == 3);
    // 예전 저장에서 칸에 들어 있던 궁극기는 불러올 때 빠진다.
    SaveData legacy;
    CHECK(deserializeSave("version=1\ndragon.present=1\ndragon.species=revenant\ndragon.alive=1\n"
                          "dragon.level=8\ndragon.slots=7,1,2,3\n", legacy));
    Game legacyGame;
    legacyGame.init(legacy, {0.f, 0.f, 1280.f, 720.f});
    for (int v : legacyGame.skillSlots()) CHECK(v != skill);

    SpeciesDef testDef;
    testDef.key = "test-procession-autofill";
    testDef.skills = {def.skills[skill]};
    DragonRecord record;
    record.species = registerOrUpdateSpecies(testDef);
    record.level = 8;
    fillEmptySkillSlots(record);
    CHECK(record.slots[0] == -1);
    World autoFill;
    autoFill.setupDragon(record.species, 8, -1.f, {400.f, 300.f});
    CHECK(autoFill.slotSkill(0) == -1);
}

static void testNetherSoulfire(const std::filesystem::path& assets) {
    std::printf("[nether soulfire]\n");
    loadDragonFolders(assets / "dragons");
    Species revenant{};
    CHECK(speciesFromKey("revenant", revenant));
    if (!speciesFromKey("revenant", revenant)) return;
    constexpr int skill = 8;
    const auto& skills = speciesDef(revenant).skills;
    CHECK(skills.size() > skill);
    if (skills.size() <= skill) return;
    const auto& def = skills[skill];
    CHECK(def.kind == SkillKind::Zone && def.soulfire && def.unlockLevel == 9 && def.motionFrames == 40);
    CHECK(skills[1].kind == SkillKind::Zone && !skills[1].soulfire && skills[7].ultimate);

    const auto setup = [&](int level = 9, bool withTarget = true, bool left = false) {
        World w;
        w.setBounds({0.f, 0.f, 1200.f, 640.f});
        w.setupDragon(revenant, level, -1.f, {600.f, 300.f});
        w.dragon().basicCd = 60.f;
        w.setSlots({skill, -1, -1, -1});
        if (withTarget) {
            Enemy& enemy = w.spawnEnemy(defaultMelee(), 1, {left ? 400.f : 800.f, 300.f});
            enemy.hp = enemy.maxHp = 2000.f;
            enemy.stun = 100.f;
            enemy.curseStacks = 3;
            enemy.curseRemaining = kCurseDuration;
        }
        return w;
    };
    PlayerInput hotkey{};
    hotkey.skill[0] = true;
    const auto step = [](World& w, float seconds) { w.updateCombat(seconds, CombatMode::Direct, {}); };

    const float takeoff = def.soulfireTakeoff;
    const float firstContact = takeoff + 0.01f;
    const float landing = takeoff + kSoulfireBreathDuration + def.soulfireLanding;
    World locked = setup(8);
    locked.updateCombat(0.f, CombatMode::Direct, hotkey);
    CHECK(!locked.dragon().skillCast);
    World autoEmpty = setup(9, false);
    autoEmpty.updateCombat(0.f, CombatMode::Auto, {});
    CHECK(!autoEmpty.dragon().skillCast);

    World world = setup(9, false);
    world.updateCombat(0.f, CombatMode::Direct, hotkey);
    CHECK(world.dragon().skillCast && world.dragon().skillMotion == skill);
    if (!world.dragon().skillCast) return;
    step(world, takeoff * 0.5f);
    CHECK(!world.soulfireBeam() && world.dragon().pos.x == 600.f && world.dots().empty());
    CHECK(!world.soulfireBeam(true));
    step(world, firstContact - takeoff * 0.5f);
    CHECK(world.dragon().pos.x > 600.f && world.soulfireBeam() && world.dots().size() == 1);
    if (world.dots().empty() || !world.soulfireBeam()) return;
    const Vec2 first = world.dots().front().pos;
    CHECK(first.y == world.soulfireBeam()->ground.y);
    CHECK(std::fabs(world.soulfireBeam()->ground.x - first.x) < 3.f);
    CHECK(world.dots().front().flames.size() == kSoulfireFlameCount);
    CHECK(world.dots().front().radius == def.radius);
    CHECK(std::fabs(def.soulfireWidth - 0.44f * 3.f) < 0.001f);
    CHECK(std::fabs(takeoff - 0.4f) < 0.001f && std::fabs(def.soulfireLanding - 0.4f) < 0.001f);
    Enemy& enemy = world.spawnEnemy(defaultMelee(), 1, {first.x + 30.f, 300.f});
    enemy.hp = enemy.maxHp = 2000.f;
    enemy.stun = 100.f;
    enemy.curseStacks = 3;
    enemy.curseRemaining = kCurseDuration;
    step(world, 0.55f);
    CHECK(world.dots().size() >= 2);
    CHECK(world.dots().front().pos.x == first.x);
    for (size_t i = 1; i < world.dots().size(); ++i)
        CHECK(std::fabs(world.dots()[i].pos.x - world.dots()[i - 1].pos.x - kSoulfireTrailSpacing) < 0.01f);
    CHECK(world.soulfireBeam() && world.soulfireBeam()->ground.x > world.dots().back().pos.x);
    CHECK(std::fabs(world.enemies().front().hp - (2000.f - mitigate(world.dragon().stats.atk * def.power * 0.55f,
                                                                      world.enemies().front().def))) < 0.01f);
    CHECK(world.enemies().front().curseStacks == 3);
    step(world, kSoulfireBreathDuration - 0.56f + 0.01f);
    const float v2Distance = world.dragon().stats.speed * cfg::kIdleFlySpeed * 0.8f;
    CHECK(std::fabs(world.dragon().pos.x - 600.f - 2.5f * v2Distance) < 0.01f);
    CHECK(!world.soulfireBeam() && !world.dots().empty());
    const auto landingContact = world.soulfireBeam(true);
    CHECK(landingContact && landingContact->ground.y == first.y &&
          landingContact->ground.x >= world.dots().back().pos.x);
    step(world, def.soulfireLanding - 0.03f);
    CHECK(world.dragon().skillCast && world.dragon().skillMotion == skill && world.dragon().anim == AnimState::Attack);
    step(world, 0.03f);
    CHECK(!world.dragon().skillCast && !world.dragon().moving && world.dragon().skillMotion == -1);
    CHECK(!world.soulfireBeam(true));
    CHECK(!world.dots().empty());
    world.updateFx(0.f, true);
    CHECK(world.dots().empty());

    for (float scale : {0.5f, 1.f, 1.5f}) {
        World scaled = setup(9, false);
        scaled.setBounds({0.f, 0.f, 2400.f, 640.f});
        scaled.setSizeScale(scale);
        scaled.updateCombat(0.f, CombatMode::Direct, hotkey);
        step(scaled, firstContact);
        CHECK(scaled.soulfireBeam() && scaled.dots().size() == 1);
        if (!scaled.soulfireBeam() || scaled.dots().empty()) continue;
        const auto beam = *scaled.soulfireBeam();
        const auto& firstPatch = scaled.dots().front();
        const float baseBody = cfg::kDragonRadius * 3.f;
        const float poseBody = baseBody * def.motionAnim.renderScale;
        const float baseDrop = (kSoulfireFoot - def.soulfireMouthY) * poseBody + 0.18f * baseBody;
        CHECK(std::fabs(def.motionAnim.renderScale - 1.26f) < 0.001f);
        CHECK(std::fabs(beam.mouth.x - scaled.dragon().pos.x -
                        ((def.soulfireMouthX - 0.5f) * poseBody + kSoulfireBodyOffsetX * baseBody) * scale) < 0.01f);
        CHECK(std::fabs(beam.ground.y - beam.mouth.y - baseDrop * scale) < 0.01f);
        CHECK(std::fabs(beam.ground.x - beam.mouth.x -
                        baseDrop / std::tan(def.soulfireAngle * kPi / 180.f) * scale) < 0.01f);
        CHECK(std::fabs(firstPatch.radius - def.radius * scale) < 0.01f);
        CHECK(std::fabs(firstPatch.flameHeight - baseBody * def.soulfireFlameScale * scale) < 0.01f);
        CHECK(firstPatch.flames.size() == kSoulfireFlameCount);
        CHECK(std::fabs(scaled.skillRange(skills[1]) - skills[1].range) < 0.01f);

        for (float offset : {-2.f, 2.f}) {
            Enemy& edgeTarget = scaled.spawnEnemy(defaultMelee(), 1, firstPatch.pos);
            edgeTarget.hp = edgeTarget.maxHp = 2000.f;
            edgeTarget.stun = 100.f;
            edgeTarget.pos.y += def.radius * scale - edgeTarget.radius * 0.5f + offset;
        }
        step(scaled, 0.02f);
        CHECK(scaled.enemies()[0].hp < 2000.f && scaled.enemies()[1].hp == 2000.f);
        CHECK(scaled.enemies()[0].soulfireHit.intensity == 0.5f);
        CHECK(scaled.enemies()[1].soulfireHit.intensity == 0.f);
        step(scaled, kSoulfireBreathDuration);
        const float expectedDistance = scaled.dragon().stats.speed * cfg::kIdleFlySpeed *
                                       kSoulfireBreathDuration * scale;
        CHECK(std::fabs(scaled.dragon().pos.x - 600.f - expectedDistance) < 0.01f);
        CHECK(!scaled.soulfireBeam());
        CHECK(scaled.dots().size() >= 2);
        for (size_t i = 1; i < scaled.dots().size(); ++i)
            CHECK(std::fabs(scaled.dots()[i].pos.x - scaled.dots()[i - 1].pos.x -
                            kSoulfireTrailSpacing * scale) < 0.01f);

        for (float offset : {-5.f, 5.f}) {
            World search = setup(9, false);
            search.setBounds({0.f, 0.f, 2400.f, 640.f});
            search.setSizeScale(scale);
            Enemy& target = search.spawnEnemy(defaultMelee(), 1, {600.f + def.range * scale + offset, 300.f});
            target.pos.x += target.radius;  // Auto targeting measures to the enemy's near edge.
            search.updateCombat(0.f, CombatMode::Auto, {});
            CHECK(search.dragon().skillCast.has_value() == (offset < 0.f));
        }
    }

    World hitFx = setup(9, false);
    hitFx.updateCombat(0.f, CombatMode::Direct, hotkey);
    step(hitFx, firstContact);
    CHECK(!hitFx.dots().empty());
    if (!hitFx.dots().empty()) {
        const Vec2 contact = hitFx.dots().front().pos;
        const auto spawnTarget = [&](Vec2 feet) {
            Enemy& e = hitFx.spawnEnemy(defaultMelee(), 1, feet);
            e.pos.y -= e.radius;
            e.hp = e.maxHp = 2000.f;
            e.stun = 100.f;
        };
        spawnTarget(contact);
        spawnTarget({contact.x + 10.f, contact.y});
        spawnTarget({100.f, contact.y});
        hitFx.dragon().basicCd = 60.f;
        step(hitFx, 0.f);
        CHECK(hitFx.enemies()[0].soulfireHit.intensity == 0.f);
        step(hitFx, 0.02f);
        CHECK(hitFx.enemies()[0].soulfireHit.intensity == 0.5f);
        CHECK(hitFx.enemies()[1].soulfireHit.intensity == 0.5f);
        CHECK(hitFx.enemies()[2].soulfireHit.intensity == 0.f);
        step(hitFx, 0.08f);
        CHECK(hitFx.enemies()[0].soulfireHit.intensity == 1.f);
        hitFx.enemiesMutable()[0].pos.x = 100.f;
        step(hitFx, 0.05f);
        CHECK(std::fabs(hitFx.enemies()[0].soulfireHit.intensity - 0.5f) < 0.001f);
        CHECK(hitFx.enemies()[1].soulfireHit.intensity == 1.f);
        hitFx.enemiesMutable()[0].pos.x = contact.x;
        step(hitFx, 0.02f);
        CHECK(hitFx.enemies()[0].soulfireHit.intensity > 0.5f);
        CHECK(hitFx.enemies()[0].soulfireHit.releaseAge == 0.f);
        hitFx.enemiesMutable()[0].pos.x = 100.f;
        step(hitFx, 0.1f);
        CHECK(hitFx.enemies()[0].soulfireHit.intensity == 0.f);
        hitFx.updateFx(0.f, true);
        CHECK(hitFx.enemies()[1].soulfireHit.intensity == 0.f);
    }

    World automatic = setup(9, true, true);
    automatic.updateCombat(0.f, CombatMode::Auto, {});
    CHECK(automatic.dragon().skillCast && automatic.dragon().skillCast->facingLeft);
    step(automatic, firstContact);
    CHECK(automatic.dragon().pos.x < 600.f && automatic.soulfireBeam() &&
          automatic.soulfireBeam()->ground.x < automatic.soulfireBeam()->mouth.x);
    automatic.clearEnemies(false);
    CHECK(!automatic.dragon().skillCast && automatic.dots().empty() && !automatic.dragon().moving);

    World cancelled = setup();
    cancelled.updateCombat(0.f, CombatMode::SkillManual, hotkey);
    step(cancelled, takeoff - 0.05f);
    PlayerInput dodge{};
    dodge.dodge = true;
    cancelled.updateCombat(0.2f, CombatMode::Direct, dodge);
    CHECK(!cancelled.dragon().skillCast && cancelled.dots().empty());

    World edge = setup(9, false);
    edge.dragon().pos.x = 1160.f;
    edge.updateCombat(0.f, CombatMode::Direct, hotkey);
    step(edge, landing + 0.01f);
    CHECK(edge.dragon().pos.x <= 1200.f - cfg::kDragonRadius && !edge.dragon().skillCast && edge.dots().empty());

    World pinned = setup(9, false);
    pinned.setSizeScale(0.5f);
    pinned.dragon().pos.x = 1120.f;
    pinned.updateCombat(0.f, CombatMode::Direct, hotkey);
    step(pinned, takeoff + 0.8f);
    const auto patchCount = pinned.dots().size();
    CHECK(patchCount > 0 && !pinned.soulfireBeam());
    step(pinned, 0.5f);
    CHECK(pinned.dots().size() == patchCount && !pinned.dragon().moving);

    World died = setup();
    died.updateCombat(0.f, CombatMode::Direct, hotkey);
    step(died, firstContact);
    died.dragon().alive = false;
    step(died, 0.1f);
    CHECK(!died.dragon().skillCast && died.dots().empty() && !died.dragon().moving);
}

static void testSpectralCleave(const std::filesystem::path& assets) {
    loadDragonFolders(assets / "dragons");
    Species revenant{};
    const bool found = speciesFromKey("revenant", revenant);
    CHECK(found);
    if (!found) return;
    constexpr int skillId = 9;
    const auto& skills = speciesDef(revenant).skills;
    CHECK(skills.size() > skillId);
    if (skills.size() <= skillId) return;
    CHECK(skills[skillId].spectralCleave && skills[skillId].unlockLevel == 10);
    CHECK(skills[8].soulfire && skills[7].ultimate);
    for (const float direction : {-1.f, 1.f}) {
        World world;
        world.setBounds({0.f, 0.f, 1200.f, 640.f});
        const float scale = direction < 0.f ? 1.5f : 0.5f;
        world.setSizeScale(scale);
        world.setupDragon(revenant, 10, -1.f, {600.f, 300.f});
        world.dragon().basicCd = 60.f;
        world.setSlots({skillId, -1, -1, -1});
        for (const float offset : {95.f, 160.f, -220.f}) {
            Enemy& enemy = world.spawnEnemy(defaultMelee(), 1, {600.f + direction * offset, 300.f});
            enemy.hp = enemy.maxHp = 10000.f;
            enemy.stun = 100.f;
        }
        PlayerInput cast{};
        cast.skill[0] = direction > 0.f;
        const auto mode = cast.skill[0] ? CombatMode::Direct : CombatMode::Auto;
        world.updateCombat(0.f, mode, cast);
        CHECK(world.dragon().skillCast.has_value());
        PlayerInput move{};
        move.moveX = -direction;
        world.updateCombat(0.59f, CombatMode::Direct, move);
        CHECK(world.projectiles().empty() && world.dragon().pos.x == 600.f);
        CHECK(std::none_of(world.fx().begin(), world.fx().end(),
                           [](const Fx& f) { return f.type == FxType::CleaveImpact; }));
        world.updateCombat(0.02f, CombatMode::Direct, move);
        CHECK(world.projectiles().size() == 1 && world.dragon().pos.x == 600.f);
        for (int i = 0; i < 12; ++i) world.updateCombat(0.05f, CombatMode::Direct, {});
        const float perHit = mitigate(world.dragon().stats.atk * skills[skillId].power, world.enemies()[0].def);
        CHECK(std::fabs(world.enemies()[0].hp - (10000.f - perHit)) < 0.002f);
        CHECK(std::fabs(world.enemies()[1].hp - (10000.f - perHit)) < 0.002f);
        CHECK(world.enemies()[2].hp == 10000.f);
        size_t impacts = 0;
        for (const Fx& fx : world.fx()) {
            if (fx.type != FxType::CleaveImpact) continue;
            CHECK(impacts < 2);
            if (impacts < 2) {
                const Enemy& enemy = world.enemies()[impacts];
                const float targetHeight = enemy.radius * 2.6f * scale;
                CHECK(fx.a.x == enemy.pos.x);
                CHECK(fx.a.y == enemy.pos.y + enemy.radius - (240.f / 256.f - 0.5f) * targetHeight);
                CHECK(fx.radius == targetHeight);
            }
            CHECK(fx.b.x - fx.a.x == direction);
            CHECK(fx.duration == kCleaveImpactDuration && fx.key == skills[skillId].fx);
            ++impacts;
        }
        CHECK(impacts == 2);
        world.updateFx(kCleaveImpactDuration + 0.01f);
        CHECK(std::none_of(world.fx().begin(), world.fx().end(),
                           [](const Fx& f) { return f.type == FxType::CleaveImpact; }));
        CHECK(world.projectiles().size() == 1);
        world.updateCombat(0.25f, CombatMode::Direct, {});
        CHECK(world.projectiles().empty());
    }
}

static void testBoneShot(const std::filesystem::path& assets) {
    loadDragonFolders(assets / "dragons");
    Species revenant{};
    if (!speciesFromKey("revenant", revenant)) {
        CHECK(false);
        return;
    }
    const auto& skills = speciesDef(revenant).skills;
    constexpr int slot = 10;
    CHECK(skills.size() > slot && skills[slot].boneShot);
    if (skills.size() <= slot) return;
    const auto& skill = skills[slot];
    for (const float facing : {-1.f, 1.f}) {
        const Vec2 origin{650.f, 300.f};
        const float scale = 1.5f;
        const float height = cfg::kDragonRadius * 3.f * scale * kAwakenedBodyScale;
        const Vec2 launch = origin + Vec2{facing * kBoneShotLaunchX * height,
            cfg::kDragonRadius - kBoneShotLaunchY * height};
        const Vec2 aim = Vec2{facing, 0.25f}.normalized();
        World world;
        world.setBounds({0.f, 0.f, 1300.f, 640.f});
        world.setSizeScale(scale);
        world.setupDragon(revenant, skill.unlockLevel, -1.f, origin);
        world.dragon().basicCd = 60.f;
        world.setSlots({slot, -1, -1, -1});
        for (const Vec2 at : {launch + aim * 100.f, launch + aim * 220.f, launch + aim * 230.f + Vec2{0.f, 100.f}}) {
            Enemy& enemy = world.spawnEnemy(defaultMelee(), 1, at);
            enemy.hp = enemy.maxHp = 10000.f;
            enemy.stun = 100.f;
        }
        PlayerInput input{};
        input.skill[0] = facing > 0.f;
        world.updateCombat(0.f, input.skill[0] ? CombatMode::SkillManual : CombatMode::Auto, input);
        const auto& cast = world.dragon().skillCast;
        CHECK(cast.has_value());
        if (cast) CHECK(cast->facingLeft == (facing < 0.f));
        world.updateCombat(0.375f, CombatMode::Direct, {});
        CHECK(world.projectiles().empty());
        world.updateCombat(0.0625f, CombatMode::Direct, {});
        CHECK(world.projectiles().size() == 1);
        if (world.projectiles().empty()) continue;
        CHECK(distance(world.projectiles().front().pos, launch) < 0.01f);
        CHECK(world.dragon().awakened);
        for (int i = 0; i < 10; ++i) world.updateCombat(0.05f, CombatMode::Direct, {});
        CHECK(world.projectiles().size() == 1);
        if (!world.projectiles().empty()) {
            CHECK(distance(world.projectiles().front().pos, launch + aim * (kBoneShotSpeed * 0.5f)) < 0.01f);
            const auto& flight = world.projectiles().front().boneShot;
            CHECK(flight.has_value());
            if (flight) CHECK(flight->hitIds.size() == 2);
        }
        int impacts = 0;
        for (const Fx& fx : world.fx()) {
            if (fx.key != skill.fxImpact) continue;
            CHECK(fx.duration == 0.5f && fx.inFront);
            CHECK(distancePointSegment(fx.a, launch, launch + aim * skill.range) < 0.01f);
            ++impacts;
        }
        CHECK(impacts == 2);
        const float hit = mitigate(world.dragon().stats.atk * skill.power, world.enemies()[0].def);
        CHECK(std::fabs(world.enemies()[0].hp - (10000.f - hit)) < 0.002f);
        CHECK(std::fabs(world.enemies()[1].hp - (10000.f - hit)) < 0.002f);
        CHECK(world.enemies()[2].hp == 10000.f);
        if (facing < 0.f) {
            world.setProjectileViewport({launch.x - 320.f, 0.f, 1300.f, 640.f});
            world.updateCombat(0.13f, CombatMode::Direct, {});
            CHECK(world.projectiles().empty());
        } else {
            world.updateCombat(0.35f, CombatMode::Direct, {});
            CHECK(world.projectiles().size() == 1);
            world.updateCombat(0.10f, CombatMode::Direct, {});
            CHECK(world.projectiles().empty());
        }
        world.updateFx(kBoneShotImpactDuration);
        CHECK(world.dragon().awakened);
        world.clearEnemies(false);
        world.updateWander(3.f);
        CHECK(world.dragon().awakened);
        PlayerInput revert{};
        revert.skill[0] = true;
        world.updateCombat(0.f, CombatMode::Direct, revert);
        CHECK(!world.dragon().awakened && world.projectiles().empty());
        CHECK(std::none_of(world.fx().begin(), world.fx().end(),
                           [&](const Fx& f) { return f.key == skill.fxImpact; }));
    }
    World longRange;
    longRange.setBounds({0.f, 0.f, 3840.f, 1080.f});
    longRange.setupDragon(revenant, skill.unlockLevel, -1.f, {400.f, 350.f});
    longRange.dragon().awakened = true;
    longRange.setSlots({slot, -1, -1, -1});
    const float launchY = 350.f + cfg::kDragonRadius -
        kBoneShotLaunchY * cfg::kDragonRadius * 3.f * kAwakenedBodyScale;
    for (const float x : {2800.f, 3300.f}) {
        Enemy& enemy = longRange.spawnEnemy(defaultMelee(), 1, {x, launchY});
        enemy.hp = enemy.maxHp = 10000.f;
        enemy.stun = 100.f;
    }
    longRange.updateCombat(0.f, CombatMode::Auto, {});
    CHECK(longRange.dragon().skillCast.has_value());
    CHECK(longRange.projectiles().empty());
    longRange.updateCombat(0.4375f, CombatMode::Direct, {});
    CHECK(longRange.projectiles().size() == 1);
    if (!longRange.projectiles().empty()) {
        const auto& flight = longRange.projectiles().front().boneShot;
        CHECK(flight.has_value());
        if (flight) CHECK(std::isinf(flight->remaining));
        CHECK(longRange.projectiles().front().speed == kAwakenedBasicBoneShotSpeed);
    }
    CHECK(longRange.dragon().skillCd[slot] == 0.f);
    // 발사 직후에는 기본 공격 대기시간이 남아서 새 투사체가 나오지 않는다.
    longRange.updateCombat(0.1f, CombatMode::Direct, {});
    CHECK(longRange.projectiles().size() == 1);
    longRange.dragon().basicCd = 100.f;
    for (int i = 0; i < 6; ++i) longRange.updateCombat(0.1f, CombatMode::Direct, {});
    CHECK(longRange.projectiles().size() == 1);
    const float basicHit = mitigate(longRange.dragon().stats.atk * cfg::kBasicAttackPower,
                                    longRange.enemies()[0].def);
    CHECK(std::fabs(longRange.enemies()[0].hp - (10000.f - basicHit)) < 0.002f);
    CHECK(longRange.enemies()[1].hp == 10000.f);
    for (int i = 0; i < 2; ++i) longRange.updateCombat(0.1f, CombatMode::Direct, {});
    CHECK(std::fabs(longRange.enemies()[1].hp - (10000.f - basicHit)) < 0.002f);
    CHECK(longRange.projectiles().size() == 1);
    longRange.updateCombat(0.6f, CombatMode::Direct, {});
    CHECK(longRange.projectiles().empty());
    longRange.dragon().basicCd = 0.f;
    longRange.updateCombat(0.f, CombatMode::Direct, {});
    CHECK(longRange.dragon().skillCast.has_value());
    PlayerInput revertBasic{};
    revertBasic.skill[0] = true;
    longRange.updateCombat(0.01f, CombatMode::SkillManual, revertBasic);
    CHECK(!longRange.dragon().awakened && !longRange.dragon().skillCast && longRange.projectiles().empty());

    World crossing;
    crossing.setBounds({0.f, 0.f, 1600.f, 900.f});
    crossing.setupDragon(revenant, skill.unlockLevel, -1.f, {400.f, 350.f});
    crossing.dragon().awakened = true;
    const float launchX = 400.f + kBoneShotLaunchX * cfg::kDragonRadius * 3.f * kAwakenedBodyScale;
    Enemy& runner = crossing.spawnEnemy(defaultMelee(), 1, {launchX + 350.f, launchY});
    runner.hp = runner.maxHp = 10000.f;
    runner.stun = 100.f; // 위치를 일정하게 옮겨 비행선을 가로지르는 적을 재현한다.
    crossing.updateCombat(0.f, CombatMode::Direct, {});
    crossing.updateCombat(0.4375f, CombatMode::Direct, {});
    const float runnerSpeed = runner.radius / 0.46f;
    for (int i = 0; i < 70; ++i) {
        runner.pos.y += runnerSpeed * 0.01f;
        crossing.updateCombat(0.01f, CombatMode::Direct, {});
    }
    CHECK(std::fabs(runner.hp - (10000.f - basicHit)) < 0.002f);
    CHECK(runner.pos.y - launchY > runner.radius);

    for (const float side : {-1.f, 1.f}) {
        World close;
        const Vec2 caster{500.f, 350.f};
        close.setBounds({0.f, 0.f, 1600.f, 900.f});
        close.setupDragon(revenant, skill.unlockLevel, -1.f, caster);
        close.dragon().awakened = true;
        const Vec2 targetStart = caster + Vec2{62.f * side, 0.f};
        Enemy& target = close.spawnEnemy(defaultMelee(), 1, targetStart);
        target.hp = target.maxHp = 10000.f;
        target.stun = 100.f;
        close.updateCombat(0.f, CombatMode::Direct, {});
        target.pos.y += 120.f;  // 준비 중 원래 조준선에서 벗어난다.
        close.updateCombat(0.4375f, CombatMode::Direct, {});
        CHECK(close.projectiles().size() == 1);
        if (close.projectiles().empty()) continue;
        CHECK(distance(close.projectiles().front().pos, caster) < 0.01f);
        CHECK(close.projectiles().front().targetId == target.id);
        target.pos.y += 70.f;  // 발사 뒤에도 목표 방향이 바뀐다.
        for (int i = 0; i < 9; ++i) close.updateCombat(0.01f, CombatMode::Direct, {});
        CHECK(std::fabs(target.hp - (10000.f - basicHit)) < 0.002f);
        CHECK(close.projectiles().size() == 1);
        if (!close.projectiles().empty()) {
            CHECK(close.projectiles().front().targetId == 0);
            const Vec2 from = close.projectiles().front().pos;
            const Vec2 direction = (target.pos - caster).normalized();
            target.pos.x -= 100.f * side;
            close.updateCombat(0.01f, CombatMode::Direct, {});
            CHECK(!close.projectiles().empty());
            if (!close.projectiles().empty())
                CHECK(distance(close.projectiles().front().pos,
                               from + direction * (kAwakenedBasicBoneShotSpeed * 0.01f)) < 0.01f);
        }
    }

    SaveData save;
    save.hasDragon = true;
    save.dragon.species = revenant;
    save.dragon.level = skill.unlockLevel;
    Game game;
    game.init(save, {0.f, 0.f, 1300.f, 640.f});
    CHECK(!game.world().dragon().awakened);
    game.world().dragon().awakened = true;
    SaveData reloaded;
    CHECK(deserializeSave(serializeSave(game.snapshotForSave()), reloaded));
    Game reopened;
    reopened.init(reloaded, {0.f, 0.f, 1300.f, 640.f});
    CHECK(reopened.world().dragon().awakened);
}

static void testSteelBasicAttack(Species steel) {
    World world;
    world.setBounds(kDesk);
    world.setupDragon(steel, 1, 0.f, {300.f, 350.f});
    auto& actor = world.dragon();
    auto& enemy = world.spawnEnemy(defaultMelee(), 1, {470.f, 360.f}, false);
    enemy.stun = 100.f;
    enemy.hp = enemy.maxHp = 1000.f;
    const auto tick = [&](float dt, PlayerInput input = {}) {
        world.updateCombat(dt, CombatMode::Direct, input);
        world.updateFx(dt);
    };
    tick(0.01f);
    CHECK(actor.anim == AnimState::Attack && actor.animTime == 0.f);
    CHECK(actor.basicCd == cfg::kBasicAttackCooldown && world.projectiles().empty());
    PlayerInput moving;
    moving.moveX = 1.f;
    const float start = actor.pos.x;
    tick(0.25f, moving);
    CHECK(actor.pos.x > start && actor.anim == AnimState::Attack && world.projectiles().empty());
    tick(0.0625f, moving);
    CHECK(actor.animTime == kSteelBasicLaunch && world.projectiles().size() == 1);
    if (world.projectiles().size() != 1) return;
    const auto& shot = world.projectiles().front();
    const float bodyHeight = 3.f * cfg::kDragonRadius;
    CHECK(shot.steelBasic && shot.t == 0.f && shot.speed == cfg::kProjectileSpeed);
    CHECK(std::fabs(shot.pos.x - actor.pos.x - kSteelBasicMouthX * bodyHeight) < 0.001f);
    CHECK(std::fabs(shot.pos.y - actor.pos.y - cfg::kDragonRadius + kSteelBasicMouthY * bodyHeight) < 0.001f);
    CHECK(shot.damage == actor.stats.atk * cfg::kBasicAttackPower);
    moving.moveX = -1.f;
    const float fireX = actor.pos.x;
    tick(0.3125f, moving);
    CHECK(actor.pos.x < fireX && actor.facing.x < 0.f && actor.anim == AnimState::Move);
    CHECK(world.projectiles().empty());
    CHECK(std::fabs(enemy.hp - (1000.f - mitigate(actor.stats.atk * cfg::kBasicAttackPower, enemy.def))) < 0.001f);
    const auto hit = [&] { return std::find_if(world.fx().begin(), world.fx().end(),
        [](const Fx& fx) { return fx.key == "basic_hit"; }); };
    CHECK(hit() != world.fx().end());
    if (hit() != world.fx().end()) {
        CHECK(hit()->t == 0.f && hit()->duration == kSteelBasicHitDuration);
        CHECK(std::fabs(distance(hit()->a, enemy.pos) - enemy.radius * 0.6f) < 0.01f);
    }
    world.updateFx(kSteelBasicHitDuration);
    CHECK(hit() == world.fx().end());

    world.clearEnemies(false);
    world.setSizeScale(1.5f);
    actor.basicCd = 0.f;
    auto& movingTarget = world.spawnEnemy(defaultMelee(), 1, {actor.pos.x - 130.f, actor.pos.y}, false);
    movingTarget.stun = 100.f;
    movingTarget.hp = movingTarget.maxHp = 1000.f;
    tick(0.01f);
    tick(0.34f);  // F06 경계를 건너도 발사는 한 번뿐이다.
    CHECK(world.projectiles().size() == 1 && actor.facing.x < 0.f);
    if (world.projectiles().size() != 1 || !world.projectiles().front().steelBasic) return;
    const auto& leftShot = world.projectiles().front();
    CHECK(leftShot.steelBasic->scale == 1.5f && leftShot.steelBasic->aim.x < 0.f);
    const Vec2 leftStart = leftShot.pos - leftShot.steelBasic->aim * (leftShot.t * leftShot.speed);
    CHECK(std::fabs(leftStart.x - actor.pos.x + kSteelBasicMouthX * bodyHeight * 1.5f) < 0.001f);
    movingTarget.pos.y += 100.f;
    tick(0.46f);
    CHECK(world.projectiles().empty() && hit() == world.fx().end());
    CHECK(movingTarget.hp == 1000.f && actor.anim == AnimState::Idle);
    tick(0.11f);
    CHECK(actor.anim == AnimState::Attack && world.projectiles().empty());
    world.clearEnemies(false);
    tick(0.4f);
    CHECK(world.projectiles().empty() && !actor.steelAttack);
}

static void testSteelFurnaceBurst(Species steel) {
    World w;
    w.setBounds(kDesk);
    w.setupDragon(steel, 1, 0.f, {490.f, 430.f});
    w.setSlots({0, -1, -1, -1});
    w.setSizeScale(1.5f);
    auto& dragon = w.dragon();
    dragon.basicCd = 10000.f;
    PlayerInput cast;
    cast.skill[0] = true;
    const SkillDef& skill = speciesDef(steel).skills[0];
    CHECK(skill.furnaceBurst && skill.fxImpactFps == 20.f);
    const auto tick = [&](float dt, PlayerInput input = {}) {
        w.updateCombat(dt, CombatMode::Direct, input);
        w.updateFx(dt);
    };
    tick(0.01f, cast);
    CHECK(w.projectiles().empty() && dragon.skillCd[0] == 0.f);
    const auto spawn = [&](Vec2 pos) {
        auto& e = w.spawnEnemy(defaultMelee(), 1, pos, false);
        e.hp = e.maxHp = 1000.f;
        e.stun = 99.f;
        return e.id;
    };
    const int direct = spawn({270.f, 355.f});
    const int splash = spawn({230.f, 312.f});
    const int outside = spawn({145.f, 350.f});
    const auto enemy = [&](int id) -> const Enemy& {
        return *std::find_if(w.enemies().begin(), w.enemies().end(),
                             [&](const Enemy& e) { return e.id == id; });
    };
    tick(0.01f, cast);
    CHECK(dragon.anim == AnimState::Attack && dragon.animTime == 0.f && dragon.skillCd[0] == skill.cooldown);
    cast.moveX = 1.f;
    const float initialX = dragon.pos.x;
    tick(0.25f, cast);
    CHECK(dragon.pos.x > initialX && dragon.facing.x < 0.f && w.projectiles().empty());
    tick(0.0625f, cast);
    CHECK(w.projectiles().size() == 1);
    if (w.projectiles().size() != 1) return;
    const auto& shot = w.projectiles().front();
    const float raw = shot.damage;
    CHECK(shot.furnace && shot.skill == 0 && shot.targetId == direct && shot.t == 0.f);
    CHECK(std::fabs(shot.pos.x - dragon.pos.x + kSteelBasicMouthX * 3.f * cfg::kDragonRadius * 1.5f) < .001f);
    for (int i = 0; i < 100 && !w.projectiles().empty(); ++i) tick(0.01f);
    CHECK(w.projectiles().empty());
    CHECK(std::fabs(enemy(direct).hp - (1000.f - mitigate(raw, enemy(direct).def))) < .01f);
    CHECK(std::fabs(enemy(splash).hp - (1000.f - mitigate(raw * skill.extra, enemy(splash).def))) < .01f);
    CHECK(enemy(outside).hp == 1000.f);
    const auto impact = [&] {
        return std::find_if(w.fx().begin(), w.fx().end(), [&](const Fx& f) { return f.key == skill.fxImpact; });
    };
    CHECK(impact() != w.fx().end());
    if (impact() != w.fx().end()) {
        CHECK(distance(impact()->a, enemy(direct).pos) == 0.f && impact()->duration == 0.6f && impact()->t == 0.f);
    }
    tick(0.6f);
    CHECK(impact() == w.fx().end() && w.projectiles().empty());
    CHECK(std::fabs(enemy(direct).hp - (1000.f - mitigate(raw, enemy(direct).def))) < .01f);
    w.clearEnemies(false);
    auto& vanishing = w.spawnEnemy(defaultMelee(), 1, {dragon.pos.x - 180.f, dragon.pos.y}, false);
    vanishing.stun = 99.f;
    dragon.skillCd[0] = 0.f;
    tick(.01f, cast);
    tick(.33f);
    CHECK(w.projectiles().size() == 1);
    vanishing.alive = false;
    tick(.02f);
    CHECK(w.projectiles().empty() && impact() == w.fx().end());
}

static void testSteelCoreOverheat(Species steel) {
    World w;
    w.setBounds(kDesk);
    w.setupDragon(steel, 3, 0.f, {450.f, 360.f});
    w.setSlots({1, -1, -1, -1});
    auto& d = w.dragon();
    d.basicCd = 1000.f;
    const auto spawn = [&](Vec2 pos) {
        auto& enemy = w.spawnEnemy(defaultMelee(), 1, pos, false);
        enemy.hp = enemy.maxHp = 1000.f;
        enemy.stun = 99.f;
        return enemy.id;
    };
    const int nearby = spawn({495.f, 405.f});
    const int elevated = spawn({515.f, 280.f});
    const int extended = spawn({750.f, 360.f});
    const int distant = spawn({950.f, 360.f});
    const auto enemy = [&](int id) -> const Enemy& {
        return *std::find_if(w.enemies().begin(), w.enemies().end(),
                             [&](const Enemy& value) { return value.id == id; });
    };
    const SkillDef& skill = speciesDef(steel).skills[1];
    CHECK(skill.coreOverheat && speciesDef(steel).skills[0].furnaceBurst);
    CHECK(skill.radius == 330.f);
    PlayerInput input;
    input.skill[0] = true;
    input.moveX = 1.f;
    const auto tick = [&](float dt) {
        w.updateCombat(dt, CombatMode::Direct, input);
        w.updateFx(dt);
    };
    tick(.10f);
    CHECK(d.overheatSkill == 1 && d.anim == AnimState::Move);
    CHECK(enemy(nearby).hp == 1000.f && enemy(elevated).hp == 1000.f);
    tick(.13f);
    CHECK(d.pos.x > 480.f && d.overheatSkill < 0 && d.overheatResidual > .59f);
    CHECK(enemy(distant).hp == 1000.f);
    const float expected = 1000.f - mitigate(d.attack() * skill.power, enemy(nearby).def);
    CHECK(std::fabs(enemy(nearby).hp - expected) < .01f);
    CHECK(std::fabs(enemy(elevated).hp - expected) < .01f);
    CHECK(std::fabs(enemy(extended).hp - expected) < .01f);
    int layers = 0;
    for (const Fx& fx : w.fx()) {
        if (fx.key != skill.fx && fx.key != skill.fxImpact) continue;
        ++layers;
        CHECK(distance(fx.a, d.pos) == 0.f && fx.t == 0.f && fx.duration == .8f);
        CHECK(std::fabs(fx.radius - 792.f) < .01f);
    }
    CHECK(layers == 2);
    input.moveX = -1.f;
    tick(.3f);
    CHECK(d.facing.x < 0.f && d.anim == AnimState::Move && d.overheatResidual > 0.f);
    tick(.51f);
    CHECK(d.overheatResidual == 0.f);
    CHECK(std::none_of(w.fx().begin(), w.fx().end(), [&](const Fx& fx) {
        return fx.key == skill.fx || fx.key == skill.fxImpact;
    }));
    CHECK(std::fabs(enemy(nearby).hp - expected) < .01f);
}

static void testSteelHeatBladeStorm(Species steel) {
    World w;
    w.setBounds({-1000.f, 0.f, 4000.f, 900.f});
    w.setupDragon(steel, 6, 0.f, {660.f, 360.f});
    w.setSlots({2, -1, -1, -1});
    auto& d = w.dragon();
    d.basicCd = 1000.f;
    const SkillDef& skill = speciesDef(steel).skills[2];
    CHECK(skill.heatBladeStorm && skill.duration == 4.f);
    CHECK(skill.range == 1260.f && skill.radius == 105.f);
    const auto add = [&](Vec2 pos) {
        auto& enemy = w.spawnEnemy(defaultMelee(), 1, pos, false);
        enemy.hp = enemy.maxHp = 1000.f;
        enemy.stun = 99.f;
    };
    add({665.f, 420.f});
    add({660.f, 300.f});
    add({350.f, 375.f});
    PlayerInput input{};
    input.skill[0] = true;
    input.moveX = 1.f;
    const auto tick = [&](float dt) {
        w.updateCombat(dt, CombatMode::Direct, input);
        w.updateFx(dt);
    };
    tick(.10f);
    CHECK(d.heatBladeStorm && d.anim == AnimState::Move && d.pos.x > 660.f);
    CHECK(w.enemies()[0].hp == 1000.f);
    input = {};
    tick(.41f);
    CHECK(d.heatBladeStorm && d.heatBladeStorm->nextPulse == 3 && w.projectiles().empty());
    CHECK(std::fabs(w.enemies()[0].hp - (1000.f - 2.f * mitigate(d.attack() * skill.power, w.enemies()[0].def))) < .01f);
    CHECK(w.enemies()[1].hp < 1000.f && w.enemies()[2].hp == 1000.f);
    tick(.09f);
    CHECK(w.projectiles().size() == 3 && d.heatBladeStorm->nextVolley == 1);
    if (w.projectiles().size() == 3)
        CHECK(distance(w.projectiles()[0].heatBlade->aim, w.projectiles()[1].heatBlade->aim) > .1f);
    tick(1.2f);
    CHECK(d.heatBladeStorm && d.heatBladeStorm->nextVolley == 2);
    tick(1.2f);
    CHECK(d.heatBladeStorm && d.heatBladeStorm->nextVolley == 3);
    tick(1.01f);
    CHECK(!d.heatBladeStorm && !w.projectiles().empty());
    tick(.65f);
    CHECK(w.projectiles().empty());

    // 같은 선상의 적을 관통한 뒤 다시 같은 적과 겹쳐도 각 탄은 한 번만 맞힌다.
    w.clearEnemies(false);
    w.setupDragon(steel, 6, 0.f, {660.f, 360.f});
    w.setSlots({2, -1, -1, -1});
    d.basicCd = 1000.f;
    add({474.f, 368.f});
    add({400.f, 368.f});
    input.skill[0] = true;
    tick(.6f);
    input = {};
    CHECK(w.projectiles().size() == 3);
    for (int i = 0; i < 12; ++i) tick(.04f);
    const float damage = mitigate(d.attack() * skill.extra, w.enemies()[0].def);
    CHECK(std::fabs(w.enemies()[0].hp - (1000.f - 2.f * damage)) < .02f);
    CHECK(std::fabs(w.enemies()[1].hp - (1000.f - 2.f * damage)) < .02f);
    w.updateFx(0.f, true);
    CHECK(!d.heatBladeStorm && w.projectiles().empty());
    CHECK(std::none_of(w.fx().begin(), w.fx().end(), [](const Fx& f) {
        return f.key.rfind("heat_blade_", 0) == 0;
    }));

    w.clearEnemies(false);
    w.setupDragon(steel, 6, 0.f, {660.f, 360.f});
    w.setSlots({2, -1, -1, -1});
    input.skill[0] = true;
    tick(5.f);
    CHECK(!d.heatBladeStorm && w.projectiles().empty());
    CHECK(std::count_if(w.fx().begin(), w.fx().end(), [](const Fx& f) {
        return f.key == "heat_blade_disappear";
    }) == 9);
}

static void testSteelFurnaceBreath(Species steel) {
    World w;
    w.setBounds(kDesk);
    w.setupDragon(steel, 9, 0.f, {350.f, 460.f});
    w.setSlots({3, -1, -1, -1});
    auto& dragon = w.dragon();
    dragon.basicCd = 100.f;
    const SkillDef& skill = speciesDef(steel).skills[3];
    CHECK(skill.usesFurnaceBreath() && skill.sustainedBreath);
    const auto add = [&](Vec2 pos, bool flew) {
        auto& enemy = w.spawnEnemy(defaultMelee(), 1, pos, flew);
        enemy.hp = enemy.maxHp = 1000.f;
        enemy.stun = 100.f;
    };
    add({660.f, 460.f}, false);
    add({800.f, 430.f}, true);
    add({650.f, 650.f}, true);
    add({1150.f, 460.f}, false);
    PlayerInput fire;
    fire.skill[0] = true;
    w.updateCombat(0.f, CombatMode::Direct, fire);
    CHECK(dragon.breath && dragon.breath->inputLock == 0.f && dragon.anim != AnimState::Attack);
    if (!dragon.breath) return;
    CHECK(std::fabs(dragon.breath->length() - 3.46f) < .001f);
    w.updateCombat(.47f, CombatMode::Direct, {});
    CHECK(dragon.breath->ticksApplied == 0 && w.enemies()[0].hp == 1000.f);
    PlayerInput walk;
    walk.moveX = 1.f;
    w.updateCombat(.02f, CombatMode::Direct, walk);
    CHECK(dragon.pos.x > 350.f && dragon.anim == AnimState::Move && dragon.breath->origin.x > dragon.pos.x);
    CHECK(dragon.breath->ticksApplied == 1);
    const float perTick = mitigate(dragon.attack() * skill.power / BreathCast::sustainedTicks, w.enemies()[0].def);
    CHECK(std::fabs(w.enemies()[0].hp - (1000.f - perTick)) < .01f);
    CHECK(w.enemies()[1].hp < 1000.f && w.enemies()[2].hp == 1000.f && w.enemies()[3].hp == 1000.f);

    w.enemiesMutable()[0].pos.y = 690.f;
    w.updateCombat(2.49f, CombatMode::Direct, {});
    CHECK(dragon.breath && dragon.breath->ticksApplied == 10);
    CHECK(std::fabs(w.enemies()[0].hp - (1000.f - perTick)) < .01f);
    CHECK(std::fabs(w.enemies()[1].hp - (1000.f - 10.f * perTick)) < .02f);
    const float afterSustain = w.enemies()[1].hp;
    w.updateCombat(.49f, CombatMode::Direct, fire);
    CHECK(!dragon.breath && w.enemies()[1].hp == afterSustain && dragon.skillCd[3] > 0.f);
}

static void testSteelCarpetBombardment(Species steel) {
    World w;
    w.setBounds(kDesk);
    w.setupDragon(steel, 12, 0.f, {400.f, 460.f});
    const auto& skill = speciesDef(steel).skills[4];
    CHECK(skill.carpetBombardment);
    w.setSlots({4, -1, -1, -1});
    auto& dragon = w.dragon();
    dragon.basicCd = 100.f;
    const auto add = [&](Vec2 pos) {
        auto& e = w.spawnEnemy(defaultMelee(), 1, pos, false);
        e.hp = e.maxHp = 1000.f;
        e.stun = 100.f;
    };
    add({680.f, 460.f});
    add({820.f, 460.f});
    add({680.f, 290.f});
    PlayerInput fire;
    fire.skill[0] = true;
    const auto tick = [&](float dt, PlayerInput input = {}) {
        w.updateCombat(dt, CombatMode::Direct, input);
        w.updateFx(dt);
    };
    tick(0.f, fire);
    CHECK(dragon.carpetCast && dragon.anim == AnimState::Fly && dragon.flying);
    if (!dragon.carpetCast) return;
    const float centerX = dragon.carpetCast->center.x;
    const float impactY = dragon.carpetCast->center.y;
    tick(.24f);
    CHECK(w.projectiles().empty());
    PlayerInput move;
    move.moveX = 1.f;
    tick(.02f, move);
    CHECK(dragon.pos.x > 400.f && dragon.anim == AnimState::Fly);
    CHECK(w.projectiles().size() == 1 && dragon.carpetCast->nextShot == 1);
    if (w.projectiles().empty()) return;
    const auto first = *w.projectiles().front().carpetBomb;
    CHECK(first.origin.y < dragon.pos.y - 100.f && first.target.x < centerX);
    tick(.14f);
    CHECK(w.projectiles().size() == 2 && dragon.carpetCast->nextShot == 2);
    tick(.33f);
    const auto countLayer = [&](int layer) {
        return std::count_if(w.fx().begin(), w.fx().end(), [&](const Fx& f) {
            return f.key == carpet::atlases[layer].key;
        });
    };
    CHECK(countLayer(carpet::flash) == 1 && countLayer(carpet::shockwave) == 1);
    CHECK(std::any_of(w.fx().begin(), w.fx().end(), [](const Fx& fx) {
        return fx.key == carpet::atlases[carpet::explosion].key && fx.t < 0.f;
    }));
    tick(1.35f);
    CHECK(w.projectiles().empty() && dragon.carpetCast && dragon.carpetCast->nextShot == 6);
    CHECK(countLayer(carpet::smoke) >= 3);
    for (int e = 0; e < 3; ++e) {
        const auto& enemy = w.enemies()[e];
        int hits = 0;
        for (int i = 0; i < carpet::shotCount; ++i)
            if (distance(enemy.pos, {centerX + (i - 2.5f) * 56.f, impactY}) <= skill.radius + enemy.radius)
                ++hits;
        const float damage = mitigate(dragon.attack() * skill.power, enemy.def);
        CHECK(std::fabs(enemy.hp - (1000.f - hits * damage)) < .03f);
        if (e < 2) CHECK(hits > 1);
        else CHECK(hits == 0);
    }
    tick(1.4f, fire);
    CHECK(!dragon.carpetCast && !dragon.flying && dragon.skillCd[4] > 0.f);
    CHECK(countLayer(carpet::smoke) == 0);

    w.clearEnemies(false);
    w.setupDragon(steel, 12, 0.f, {500.f, 460.f});
    w.setSlots({4, -1, -1, -1});
    dragon.facing = {-1.f, 0.f};
    tick(0.f, fire);
    w.updateCombat(2.1f, CombatMode::Direct, {});  // Count all impacts before expired visuals are erased.
    CHECK(countLayer(carpet::smoke) == 6 && w.projectiles().empty());
    std::vector<float> impacts;
    for (const Fx& fx : w.fx())
        if (fx.key == carpet::atlases[carpet::smoke].key) impacts.push_back(fx.a.x);
    CHECK(std::is_sorted(impacts.rbegin(), impacts.rend()));
    w.updateFx(0.f, true);
    CHECK(!dragon.carpetCast && countLayer(carpet::smoke) == 0);

    w.setupDragon(steel, 12, 0.f, {500.f, 460.f});
    w.setSlots({4, -1, -1, -1});
    tick(0.f, fire);
    tick(.30f);
    CHECK(w.projectiles().size() == 1);
    dragon.alive = false;
    tick(.40f);
    CHECK(!dragon.carpetCast && w.projectiles().size() == 1);
    tick(.05f);
    CHECK(w.projectiles().empty() && countLayer(carpet::flash) == 1);
}

static void testSteelDragon(const std::filesystem::path& assets) {
    std::printf("[steel dragon registration]\n");
    const DragonScanResult scan = loadDragonFolders(assets / "dragons");
    for (const auto& warning : scan.warnings)
        CHECK(warning.find("steel_dragon") == std::string::npos);

    Species steel{};
    const bool found = speciesFromKey("steel_dragon", steel);
    CHECK(found);
    if (!found) return;
    const auto& def = speciesDef(steel);
    CHECK(def.name == "강철룡" && def.role != "Preview" && def.flipAllowed);
    for (const auto state : {AnimState::Idle, AnimState::Fly}) {
        const auto& anim = def.anims[static_cast<int>(state)];
        CHECK(anim.fps == 12.f && anim.frameW == 256 && anim.loop);
    }
    const auto& move = def.anims[static_cast<int>(AnimState::Move)];
    CHECK(move.fps == 16.f && move.frameW == 256 && move.loop);
    const auto& attack = def.anims[static_cast<int>(AnimState::Attack)];
    CHECK(attack.fps == kSteelBasicFps && attack.frameW == 256 && !attack.loop);
    CHECK(def.skills.size() == 5);

    Game game;
    game.init(SaveData{}, kDesk);
    CHECK(game.screen() == Screen::Select);
    game.chooseSpecies(steel);
    CHECK(game.screen() == Screen::Desktop && game.hasDragon());
    CHECK(game.dragonRecord().species == steel && game.consumeSaveRequest());

    SaveData saved;
    CHECK(deserializeSave(serializeSave(game.snapshotForSave()), saved));
    Game restarted;
    restarted.init(saved, kDesk);
    CHECK(restarted.hasDragon() && restarted.dragonRecord().species == steel);
    testSteelBasicAttack(steel);
    testSteelFurnaceBurst(steel);
    testSteelCoreOverheat(steel);
    testSteelHeatBladeStorm(steel);
    testSteelFurnaceBreath(steel);
    testSteelCarpetBombardment(steel);

    game.placeDragon({700.f, 500.f});
    auto& world = game.world();
    auto& actor = world.dragon();
    actor.behavior = IdleBehavior::Rest;
    actor.behaviorLeft = 3.f;
    world.updateWander(0.01f);
    CHECK(actor.anim == AnimState::Idle);

    actor.behavior = IdleBehavior::Walk;
    actor.wanderTarget = {900.f, actor.groundY};
    world.updateWander(0.01f);
    CHECK(actor.anim == AnimState::Move && actor.animTime == 0.f && actor.facing.x > 0.f);
    const auto start = actor.pos;
    for (int i = 0; i < 16; ++i) world.updateWander(1.f / move.fps);
    CHECK(actor.anim == AnimState::Move && actor.animTime == 1.f);
    CHECK(std::fabs(actor.pos.x - start.x - actor.stats.speed * cfg::kIdleWalkSpeed) < 0.01f);
    CHECK(actor.pos.y == start.y);
    std::printf("  steel desktop walk: %.2f px per 16-frame cycle\n", actor.pos.x - start.x);

    actor.behavior = IdleBehavior::Rest;
    world.updateWander(0.01f);
    CHECK(actor.anim == AnimState::Idle && actor.animTime == 0.f);

    actor.behavior = IdleBehavior::Walk;
    actor.wanderTarget = {650.f, actor.groundY};
    world.updateWander(0.01f);
    CHECK(actor.anim == AnimState::Move && actor.facing.x < 0.f);

    actor.behavior = IdleBehavior::Fly;
    actor.wanderTarget = {900.f, actor.groundY - 100.f};
    world.updateWander(0.01f);
    CHECK(actor.anim == AnimState::Fly && actor.animTime == 0.f);
    actor.behavior = IdleBehavior::Walk;
    actor.wanderTarget = {900.f, actor.groundY};
    world.updateWander(0.01f);
    CHECK(actor.anim == AnimState::Move && actor.animTime == 0.f);
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--bone-shot") {
        testBoneShot(std::filesystem::u8path(argv[2]));
        std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
        return g_failed ? 1 : 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--revenant-procession") {
        testRevenantProcession();
        std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
        return g_failed == 0 ? 0 : 1;
    }
    if (argc == 2 && std::string(argv[1]) == "--revenant-ultimate-charge") {
        testRevenantUltimateCharge();
        std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
        return g_failed == 0 ? 0 : 1;
    }
    if (argc == 2 && std::string(argv[1]) == "--revenant-souls") {
        testRevenantSouls("assets");
        std::printf("%d passed, %d failed\n", g_passed, g_failed);
        return g_failed == 0 ? 0 : 1;
    }
    if (argc == 2 && std::string(argv[1]) == "--revenant-curse") {
        testRevenantCurse("assets");
        std::printf("%d passed, %d failed\n", g_passed, g_failed);
        return g_failed == 0 ? 0 : 1;
    }
    if (argc == 2 && std::string(argv[1]) == "--zone-sprite") {
        testZoneSprite();
        std::printf("%d passed, %d failed\n", g_passed, g_failed);
        return g_failed == 0 ? 0 : 1;
    }
    if (argc == 2 && std::string(argv[1]) == "--layered-breath") {
        testLayeredBreath();
        std::printf("%d passed, %d failed\n", g_passed, g_failed);
        return g_failed == 0 ? 0 : 1;
    }
    // 종을 등록하기 전에 돌아야 하는 것들이 맨 앞이다.
    testEmptySpeciesRegistry();
    testSaveLevelClamp();
    registerTestSpecies();
    testExpCurve();
    testNoAutoCap();
    testAutoCapClamp();
    testMultiLevelUp();
    testSaveRoundTrip();
    testTowerSaveKeys();
    testSelectAndDeath();
    testModeRules();
    testHiddenRules();
    testAutoStopsAtCap();
    testTowerFlow();
    testFrameStats();
    testMissingDragonsDirIsSilent();
    testTowerFloorsBuiltin();
    testTowerTextParse();
    testTowerFloorsFromData();
    testTowerHundredRules();
    testFxPhases();
    testFxShapeUnchanged();
    testFxFromSkillData();
    testDirectDodge();
    testDragonTextParse();
    testDragonTextWarnings();
    testImpactFpsAndCodex();
    testSpeciesRegistry();
    testUnknownSpeciesKeepsIdentity();
    testAnimStates();
    testLoadDragonFolders();
    testIdleBehavior();
    testEnemyRegistry();
    testEnemyTextParse();
    testEnemyFolderScan();
    testBossPhase2Summons();
    testDefenseBattle();
    testBattleWithoutConfine();
    testHuntFloorPool();
    testEconomy();
    testFieldBossParty();
    testEnemySplit();
    testDefenseWaveGrows();
    testAutoBattleNoLongerCapped();
    testUiSettingsSaved();
    testConfineToBand();
    testPlaceAndPanel();
    testAlerts();
    testFirstRunHint();
    testViewSettings();
    testBattleBandHeight();
    testTimedHit();
    testThinBandStanding();
    testStable();
    testStablePermadeath();
    testKeepRange();
    testTinyScreen();
    testSkillUnlockLevels();
    testSkillSlots();
    testSkillSlotsSaved();
    testPassiveSkills();
    testAutoUsesEquippedOnly();
    testSkillMotionParse();
    testSizeScaledHitbox();
    testActiveDragonHpSaved();
    testEnemyArtFromParse();
    testEnemyArtFromScanWarnings();
    // 실제 assets 를 읽는 것은 등록부를 바꾸므로 맨 마지막에 돈다
    if (argc > 1) {
        const std::filesystem::path assets(argv[1]);
        testEliteVariantsSpawn(assets);
        testTowerSelection(assets);
        testRevenantCurse(assets);
        testNetherSoulfire(assets);
        testSpectralCleave(assets);
        testBoneShot(assets);
        testSteelDragon(assets);
    }
    else std::printf("[elite variants spawn] assets 경로 인자가 없어 건너뜀\n");
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
