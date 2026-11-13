// 핵심 규칙 검증. Windows 없이도 빌드·실행된다.
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>

#include "core/Config.h"
#include "core/DragonData.h"
#include "core/EnemyData.h"
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

    // 등록이 더 일어나도 내장 종 참조가 깨지지 않는다(deque)
    const SpeciesDef& fireRef = speciesDef(Species::Fire);
    SpeciesDef e;
    e.key = "another";
    registerOrUpdateSpecies(e);
    CHECK(fireRef.key == "fire");

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
    fs::create_directories(root / "fire", ec);
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
        std::ofstream f(root / "fire" / "dragon.txt");
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
    CHECK(speciesDef(Species::Fire).role == "덮어쓴 역할");
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

int main() {
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
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
