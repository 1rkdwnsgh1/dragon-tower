// 임시 수치 점검용 시뮬레이션. 결과는 참고용이며 실제 감각은 플레이 테스트로 확인한다.
#include <cstdio>

#include "core/Config.h"
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
            PlayerInput in = mode == CombatMode::Direct ? directBot(g.world()) : skillBot(g.world());
            g.update(kDt, in, false, true);
            time += kDt;
        }
        if (!g.hasDragon()) deaths++;
        else if (g.dragonRecord().level >= targetLevel) { reached++; total += time; }
    }
    std::printf("  %-12s %-6s Lv1→%d: reached %2d/%d (avg %4.1f min), death %2d/%d\n", speciesDef(sp).name.c_str(),
                mode == CombatMode::Direct ? "direct" : "skill", targetLevel, reached, trials,
                reached ? total / reached / 60.f : 0.f, deaths, trials);
}

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
            PlayerInput in = mode == CombatMode::Direct ? directBot(g.world()) : skillBot(g.world());
            g.update(kDt, in, false, true);
            time += kDt;
        }
        if (g.screen() == Screen::TowerVictory) wins++;
        floorSum += g.towerFloor();
    }
    std::printf("    Lv%-2d %-6s win %2d/%d  avg floor reached %.1f\n", level,
                mode == CombatMode::Direct ? "direct" : "skill", wins, trials, floorSum / float(trials));
}

int main() {
    std::printf("Auto-exp cap = %d (Lv%d)\n\n[auto hunt from Lv1]\n", autoExpCap(), cfg::kAutoExpCapLevel);
    for (int i = 0; i < kBuiltinSpeciesCount; ++i) autoHunt(static_cast<Species>(i));
    std::printf("\n[manual hunt]\n");
    for (int i = 0; i < kBuiltinSpeciesCount; ++i) {
        manualHunt(static_cast<Species>(i), CombatMode::SkillManual, 7);
        manualHunt(static_cast<Species>(i), CombatMode::Direct, 7);
    }
    std::printf("\n[tower]\n");
    for (int i = 0; i < kBuiltinSpeciesCount; ++i) {
        std::printf("  %s\n", speciesDef(static_cast<Species>(i)).name.c_str());
        for (int lv : {5, 7, 9}) {
            tower(static_cast<Species>(i), CombatMode::SkillManual, lv);
            tower(static_cast<Species>(i), CombatMode::Direct, lv);
        }
    }
    return 0;
}
