// 핵심 규칙 검증. Windows 없이도 빌드·실행된다.
#include <cstdio>
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

// v1.6: 탑 층을 데이터로 뺐다. 파일이 없으면 v1.5와 똑같이 동작해야 한다 — 그것을 여기서 고정한다.
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
    DragonScanResult none = loadDragonFolders(root / "없음");
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

    EnemyScanResult none = loadEnemyFolders(root / "없음");
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

    // 전투를 끄면 다시 바탕화면 전체를 돌아다닌다
    g.stopHunt("테스트");
    CHECK(!g.hunting());
    CHECK(g.world().bounds().h > cfg::kBandHeight + 1.f);
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

    // 사냥을 시작해도(띠로 제한) 마찬가지, 적도 띠 안에 들어온다
    g.setConfineToBand(false);
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
        "dragon.level=34\n"
        "stable.count=1\n"
        "stable.0.level=34\n"
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

int main() {
    testEmptySpeciesRegistry();
    testSaveLevelClamp();
    registerTestSpecies();
    testExpCurve();
    testNoAutoCap();
    testAutoCapClamp();
    testMultiLevelUp();
    testSaveRoundTrip();
    testSelectAndDeath();
    testModeRules();
    testHiddenRules();
    testAutoStopsAtCap();
    testTowerFlow();
    testTowerFloorsBuiltin();
    testTowerTextParse();
    testTowerFloorsFromData();
    testDirectDodge();
    testDragonTextParse();
    testDragonTextWarnings();
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
    testDefenseWaveGrows();
    testAutoBattleNoLongerCapped();
    testUiSettingsSaved();
    testConfineToBand();
    testPlaceAndPanel();
    testAlerts();
    testFirstRunHint();
    testViewSettings();
    testBattleBandHeight();
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
    testActiveDragonHpSaved();
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
