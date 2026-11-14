// 임시 수치 점검용 시뮬레이션. 결과는 참고용이며 실제 감각은 플레이 테스트로 확인한다.
//
//   balance_sim [assets 폴더] [모드] [옵션...]
//
// 모드
//   (없음)   자동 사냥 · 수동 사냥 · 탑 전체 돌파 (기존 출력)
//   floors   탑 **층별** 표 — 층마다 승률·남은 체력·사망률 (v1.6, 큐 5번)
//
// floors 모드 옵션
//   --species=<키>      그 종만 돈다 (기본: 등록된 종 전부)
//   --levels=6,8,10     드래곤 레벨 목록 (기본: 6,8,10 — 권장 레벨 8을 가운데 둔다)
//   --mode=skill|direct|both   조작 흉내 (기본: both)
//   --skills=0,2        장착 칸에 넣을 스킬 **목록 번호**. 남은 칸은 게임 규칙대로 자동으로 채워진다
//   --seed=900          무작위 시드. 같은 시드·같은 옵션이면 출력이 같다
//   --trials=40         층별 표본 수
//
// 주의: 종과 적은 전부 assets 폴더에서 온다(v1.5에서 내장 4종을 지웠다).
// 폴더를 읽지 않으면 등록된 종이 0개라 아무것도 돌지 않는다 — v1.5 이후 이 도구가
// 빈 표만 찍고 있던 이유다.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "core/Config.h"
#include "core/DragonData.h"
#include "core/EnemyData.h"
#include "core/Game.h"

using namespace dt;

static const RectF kDesk{0.f, 0.f, 1920.f, 1040.f};
static const float kDt = 1.f / 30.f;

// 직접 조작 흉내: 곧 터질 예고 범위 안이면 회피, 아니면 적과 거리를 유지
static PlayerInput directBot(const World& w) {
    PlayerInput in;
    const DragonActor& d = w.dragon();
    Vec2 flee{0.f, 0.f};
    bool danger = false;
    float soonest = 99.f;
    for (const AttackZone& z : w.zones()) {
        float reach = cfg::kDragonRadius * 0.4f + 10.f;
        bool inside = z.shape == ZoneShape::Circle ? distance(d.pos, z.a) <= z.radius + reach
                                                   : distancePointSegment(d.pos, z.a, z.b) <= z.radius + reach;
        if (!inside) continue;
        danger = true;
        soonest = std::min(soonest, z.windup - z.t);
        Vec2 c = z.shape == ZoneShape::Circle ? z.a : (z.a + z.b) * 0.5f;
        Vec2 away = d.pos - c;
        if (z.shape == ZoneShape::Line) {
            Vec2 dir = (z.b - z.a).normalized();
            away = Vec2{-dir.y, dir.x};
            Vec2 rel = d.pos - z.a;
            if (rel.x * away.x + rel.y * away.y < 0.f) away = away * -1.f;
        }
        flee += away.normalized();
    }
    if (danger) {
        in.moveX = flee.x;
        in.moveY = flee.y;
        if (soonest < 0.22f) in.dodge = true;
    } else {
        // 가장 가까운 적을 향해 사거리 근처까지 접근
        const Enemy* best = nullptr;
        float bd = 1e9f;
        for (const Enemy& e : w.enemies()) {
            float dd = distance(e.pos, d.pos);
            if (dd < bd) { bd = dd; best = &e; }
        }
        if (best && bd > cfg::kBasicAttackRange * 0.8f) {
            Vec2 dir = (best->pos - d.pos).normalized();
            in.moveX = dir.x;
            in.moveY = dir.y;
        } else if (best && bd < 120.f) {
            Vec2 dir = (d.pos - best->pos).normalized();
            in.moveX = dir.x;
            in.moveY = dir.y;
        }
    }
    // v1.4: 입력은 장착 칸 기준이다(스킬 목록 번호가 아니다)
    for (int i = 0; i < kSkillSlots; ++i) in.skill[i] = w.slotReady(i);
    return in;
}

static PlayerInput skillBot(const World& w) {
    PlayerInput in;
    // v1.4: 입력은 장착 칸 기준이다(스킬 목록 번호가 아니다)
    for (int i = 0; i < kSkillSlots; ++i) in.skill[i] = w.slotReady(i);
    return in;
}

static PlayerInput botFor(const World& w, CombatMode m) {
    return m == CombatMode::Direct ? directBot(w) : skillBot(w);
}

static const char* modeTag(CombatMode m) { return m == CombatMode::Direct ? "direct" : "skill"; }

static void autoHunt(Species sp) {
    int deaths = 0, capped = 0;
    float capTime = 0.f;
    const int trials = 20;
    for (int t = 0; t < trials; ++t) {
        Game g;
        g.seed(100 + t);
        g.init(SaveData{}, kDesk);
        g.chooseSpecies(sp);
        g.setHuntMode(CombatMode::Auto);
        g.startHunt();
        g.confirmAutoWarning();
        float time = 0.f;
        while (time < 3600.f && g.hunting()) {
            g.update(kDt, {}, true, false);
            time += kDt;
        }
        if (g.screen() == Screen::Death) deaths++;
        else if (g.dragonRecord().autoExp >= autoExpCap()) { capped++; capTime += time; }
    }
    std::printf("  %-12s auto: cap %2d/%d (avg %4.1f min), death %2d/%d\n", speciesDef(sp).name.c_str(), capped, trials,
                capped ? capTime / capped / 60.f : 0.f, deaths, trials);
}

static void manualHunt(Species sp, CombatMode mode, int targetLevel) {
    int deaths = 0, reached = 0;
    float total = 0.f;
    const int trials = 20;
    for (int t = 0; t < trials; ++t) {
        Game g;
        g.seed(500 + t);
        g.init(SaveData{}, kDesk);
        g.chooseSpecies(sp);
        g.setHuntMode(mode);
        g.startHunt();
        float time = 0.f;
        while (time < 3600.f && g.hasDragon() && g.dragonRecord().level < targetLevel) {
            PlayerInput in = botFor(g.world(), mode);
            g.update(kDt, in, false, true);
            time += kDt;
        }
        if (!g.hasDragon()) deaths++;
        else if (g.dragonRecord().level >= targetLevel) { reached++; total += time; }
    }
    std::printf("  %-12s %-6s Lv1→%d: reached %2d/%d (avg %4.1f min), death %2d/%d\n", speciesDef(sp).name.c_str(),
                modeTag(mode), targetLevel, reached, trials, reached ? total / reached / 60.f : 0.f, deaths, trials);
}

// 전체 돌파만 보는 기존 요약(층별은 아래 floors 모드).
static void tower(Species sp, CombatMode mode, int level) {
    int wins = 0;
    int floorSum = 0;
    const int trials = 40;
    for (int t = 0; t < trials; ++t) {
        SaveData s;
        s.hasDragon = true;
        s.nextDragonId = 2;
        s.dragon.id = 1;
        s.dragon.species = sp;
        s.dragon.level = level;
        Game g;
        g.seed(900 + t);
        g.init(s, kDesk);
        g.openTowerWarning();
        g.enterTower(mode);
        float time = 0.f;
        while (time < 900.f) {
            if (g.screen() == Screen::FloorClear) g.nextFloor();
            if (g.screen() != Screen::Tower) break;
            g.update(kDt, botFor(g.world(), mode), false, true);
            time += kDt;
        }
        if (g.screen() == Screen::TowerVictory) wins++;
        floorSum += g.towerFloor();
    }
    std::printf("    Lv%-2d %-6s win %2d/%d  avg floor reached %.1f\n", level, modeTag(mode), wins, trials,
                floorSum / float(trials));
}

// ---------------- 탑: 한 번의 등반 ----------------

// 층마다 모으는 값. 층 번호는 1부터이므로 vector의 0번 자리는 쓰지 않는다.
struct FloorStat {
    int reached = 0;    // 그 층에 발을 들인 횟수
    int cleared = 0;    // 그 층을 넘은 횟수
    int died = 0;       // 그 층에서 죽은 횟수
    int timeout = 0;    // 제한 시간 안에 못 끝낸 횟수
    float hpLeft = 0.f; // 돌파 시점에 남은 체력 비율의 합(회복 전)
    float hpLost = 0.f; // 그 층에서 잃은 체력 비율의 합(돌파한 경우만)
    float time = 0.f;   // 돌파에 걸린 시간의 합
};

// 돌파율은 **그 층에 도달한 경우에 대한 비율**이다(1층부터 올라오므로 위층은 표본이 줄어든다).
// 층을 따로 떼어 "온전한 체력으로 그 층만" 재려면 탑을 N층에서 시작하는 코어 쪽
// 진입점이 필요하다 — 이 항목(도구)의 범위를 넘으므로 하지 않았다.
static void climb(const SaveData& base, CombatMode mode, uint32_t seed, std::vector<FloorStat>& stat,
                  int& fullClears, int& deaths) {
    Game g;
    g.seed(seed);
    g.init(base, kDesk);
    g.openTowerWarning();
    g.enterTower(mode);
    if (g.screen() != Screen::Tower) return;  // 들어가지 못했으면 표본으로 세지 않는다

    auto grow = [&stat](int floor) {
        if (floor >= static_cast<int>(stat.size())) stat.resize(static_cast<size_t>(floor) + 1);
    };
    // 체력은 **비율**로 모은다. 탑에서도 적을 쓰러뜨리면 경험치가 들어와 레벨업으로
    // 최대 체력이 바뀌므로(World::setDragonLevel), 층 시작 때의 최대 체력으로 나누면
    // 100%를 넘는 값이 나온다 — 그 자리의 최대 체력으로 그때그때 나눈다.
    auto hpRatio = [&g]() {
        const DragonActor& d = g.world().dragon();
        return d.hp / std::max(1.f, d.stats.maxHp);
    };
    int floor = g.towerFloor();
    grow(floor);
    stat[static_cast<size_t>(floor)].reached++;
    float hpStart = hpRatio();
    float lastHp = hpStart;
    float floorTime = 0.f;
    float time = 0.f;

    while (time < 900.f) {
        Screen sc = g.screen();
        if (sc == Screen::FloorClear || sc == Screen::TowerVictory) {
            FloorStat& f = stat[static_cast<size_t>(floor)];
            f.cleared++;
            f.hpLeft += lastHp;
            f.hpLost += std::max(0.f, hpStart - lastHp);
            f.time += floorTime;
            if (sc == Screen::TowerVictory) { fullClears++; return; }
            g.nextFloor();
            if (g.screen() != Screen::Tower) return;
            floor = g.towerFloor();
            grow(floor);
            stat[static_cast<size_t>(floor)].reached++;
            hpStart = hpRatio();
            lastHp = hpStart;
            floorTime = 0.f;
            continue;
        }
        if (sc != Screen::Tower) {  // 사망 등
            stat[static_cast<size_t>(floor)].died++;
            deaths++;
            return;
        }
        lastHp = hpRatio();  // 돌파하는 프레임에 층 돌파 회복이 섞이지 않게 갱신 전 값을 둔다
        g.update(kDt, botFor(g.world(), mode), false, true);
        time += kDt;
        floorTime += kDt;
    }
    stat[static_cast<size_t>(floor)].timeout++;
}

// 한 종·한 레벨·한 모드의 층별 표. 전체 돌파율을 돌려준다(레벨 비교에 쓴다).
static float floorsRun(Species sp, int level, CombatMode mode, const std::vector<int>& skills, uint32_t seed,
                       int trials) {
    SaveData base;
    base.hasDragon = true;
    base.nextDragonId = 2;
    base.dragon.id = 1;
    base.dragon.species = sp;
    base.dragon.level = level;
    const SpeciesDef& def = speciesDef(sp);
    for (size_t i = 0; i < skills.size() && i < static_cast<size_t>(kSkillSlots); ++i) {
        int idx = skills[i];
        if (idx < 0 || idx >= static_cast<int>(def.skills.size())) {
            std::printf("  [경고] --skills: %s 에 %d번 스킬이 없습니다 (0~%d). 그 칸은 비워 둡니다.\n",
                        def.key.c_str(), idx, static_cast<int>(def.skills.size()) - 1);
            continue;
        }
        if (skillIsPassive(def.skills[static_cast<size_t>(idx)].kind)) {
            std::printf("  [경고] --skills: %s 의 %d번(%s)은 패시브라 칸에 넣지 않습니다.\n", def.key.c_str(), idx,
                        def.skills[static_cast<size_t>(idx)].name.c_str());
            continue;
        }
        base.dragon.slots[i] = idx;
    }

    std::vector<FloorStat> stat;
    int fullClears = 0, deaths = 0;
    for (int t = 0; t < trials; ++t) climb(base, mode, seed + static_cast<uint32_t>(t), stat, fullClears, deaths);

    std::printf("  Lv%-2d %-6s  전체 돌파 %2d/%d (%5.1f%%)  사망 %2d/%d\n", level, modeTag(mode), fullClears, trials,
                100.f * fullClears / float(trials), deaths, trials);
    std::printf("    층  도달  돌파    돌파율   사망  시간초과   남은체력  층에서 잃은 체력  평균 초\n");
    for (size_t f = 1; f < stat.size(); ++f) {
        const FloorStat& s = stat[f];
        if (s.reached == 0) continue;
        float rate = 100.f * s.cleared / float(s.reached);
        std::printf("    %2d  %4d  %4d   %6.1f%%   %4d      %4d    %6.1f%%            %6.1f%%  %6.1f\n",
                    static_cast<int>(f), s.reached, s.cleared, rate, s.died, s.timeout,
                    s.cleared ? 100.f * s.hpLeft / s.cleared : 0.f, s.cleared ? 100.f * s.hpLost / s.cleared : 0.f,
                    s.cleared ? s.time / s.cleared : 0.f);
    }
    return 100.f * fullClears / float(trials);
}

static bool parseIntList(const std::string& v, std::vector<int>& out) {
    out.clear();
    size_t i = 0;
    while (i <= v.size()) {
        size_t j = v.find(',', i);
        std::string tok = v.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!tok.empty()) out.push_back(std::atoi(tok.c_str()));
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return !out.empty();
}

static int floorsMode(int argc, char** argv, int firstOpt) {
    std::string speciesKey;
    std::vector<int> levels{6, 8, 10};
    std::vector<int> skills;
    std::string modeArg = "both";
    uint32_t seed = 900;
    int trials = 40;
    for (int i = firstOpt; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&a](const char* k) -> const char* {
            size_t n = std::string(k).size();
            return a.compare(0, n, k) == 0 ? a.c_str() + n : nullptr;
        };
        if (const char* v = val("--species=")) speciesKey = v;
        else if (const char* v = val("--levels=")) parseIntList(v, levels);
        else if (const char* v = val("--skills=")) parseIntList(v, skills);
        else if (const char* v = val("--mode=")) modeArg = v;
        else if (const char* v = val("--seed=")) seed = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        else if (const char* v = val("--trials=")) trials = std::max(1, std::atoi(v));
        else { std::printf("모르는 옵션: %s\n", a.c_str()); return 2; }
    }
    std::vector<CombatMode> modes;
    if (modeArg == "skill" || modeArg == "both") modes.push_back(CombatMode::SkillManual);
    if (modeArg == "direct" || modeArg == "both") modes.push_back(CombatMode::Direct);
    if (modes.empty()) { std::printf("--mode= 는 skill|direct|both 중 하나입니다.\n"); return 2; }

    std::vector<Species> list;
    if (speciesKey.empty()) {
        for (int i = 0; i < speciesCount(); ++i) list.push_back(static_cast<Species>(i));
    } else {
        Species s{};
        if (!speciesFromKey(speciesKey, s)) { std::printf("그런 종이 없습니다: %s\n", speciesKey.c_str()); return 2; }
        list.push_back(s);
    }
    if (list.empty()) { std::printf("등록된 종이 없습니다. assets 폴더를 확인하세요.\n"); return 2; }

    std::printf("[탑 층별]  시드 %u · 층마다 %d회 · 돌파율은 그 층에 도달한 경우에 대한 비율\n", seed, trials);
    std::printf("           탑은 현재 %d층까지다(assets/tower/floors.txt 가 들어오면 그만큼 자동으로 늘어난다)\n\n",
                Game::kTowerFloors);
    for (Species sp : list) {
        const SpeciesDef& def = speciesDef(sp);
        std::printf("%s (%s)", def.name.c_str(), def.key.c_str());
        if (!skills.empty()) {
            std::printf("  장착 지정:");
            for (int idx : skills)
                std::printf(" %d(%s)", idx,
                            idx >= 0 && idx < static_cast<int>(def.skills.size())
                                ? def.skills[static_cast<size_t>(idx)].name.c_str()
                                : "?");
        }
        std::printf("\n");
        for (CombatMode m : modes) {
            std::vector<float> byLevel;
            for (int lv : levels) byLevel.push_back(floorsRun(sp, lv, m, skills, seed, trials));
            bool mono = true;
            for (size_t i = 1; i < byLevel.size(); ++i)
                if (byLevel[i] < byLevel[i - 1] - 0.001f) mono = false;
            std::printf("    → %s: 레벨이 오를수록 전체 돌파율이 떨어지지 않는가: %s\n\n", modeTag(m),
                        mono ? "예" : "아니오 (수치를 들여다볼 것)");
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    // v1.5부터 종·적은 전부 폴더에서 온다. 먼저 읽지 않으면 등록된 종이 0개다.
    std::filesystem::path assets =
#ifdef DT_ASSETS_DIR
        DT_ASSETS_DIR;
#else
        "assets";
#endif
    int firstOpt = 1;
    if (argc > 1 && argv[1][0] != '-' && std::string(argv[1]) != "floors") { assets = argv[1]; firstOpt = 2; }
    DragonScanResult d = loadDragonFolders(assets / "dragons");
    EnemyScanResult e = loadEnemyFolders(assets / "enemies");
    std::printf("에셋: %s — 종 %d개, 적 %d개", assets.string().c_str(), speciesCount(), enemyCount());
    if (!d.warnings.empty() || !e.warnings.empty())
        std::printf(" (경고 %d건 — asset_check 로 확인)", static_cast<int>(d.warnings.size() + e.warnings.size()));
    std::printf("\n\n");
    if (speciesCount() == 0) {
        std::printf("등록된 종이 없습니다. 첫 인수로 assets 폴더를 지정하세요.\n");
        return 2;
    }

    if (firstOpt < argc && std::string(argv[firstOpt]) == "floors") return floorsMode(argc, argv, firstOpt + 1);

    std::printf("Auto-exp cap = %d (Lv%d)\n\n[auto hunt from Lv1]\n", autoExpCap(), cfg::kAutoExpCapLevel);
    for (int i = 0; i < speciesCount(); ++i) autoHunt(static_cast<Species>(i));
    std::printf("\n[manual hunt]\n");
    for (int i = 0; i < speciesCount(); ++i) {
        manualHunt(static_cast<Species>(i), CombatMode::SkillManual, 7);
        manualHunt(static_cast<Species>(i), CombatMode::Direct, 7);
    }
    std::printf("\n[tower]\n");
    for (int i = 0; i < speciesCount(); ++i) {
        std::printf("  %s\n", speciesDef(static_cast<Species>(i)).name.c_str());
        for (int lv : {5, 7, 9}) {
            tower(static_cast<Species>(i), CombatMode::SkillManual, lv);
            tower(static_cast<Species>(i), CombatMode::Direct, lv);
        }
    }
    return 0;
}
