// 창(모달) 그리기: 모션 목록·메뉴·확인창·종 선택·드래곤 관리·스킬 관리·도감.
#include "RendererInternal.h"

namespace dt::win {

// 드래곤을 오른쪽 클릭하면 뜨는 모션 목록. 그림이 있는 모션만 보여 준다.
void Renderer::drawMotionMenu(Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                              std::vector<UiButton>& out) {
    if (!game.motionMenuOpen() || !game.hasDragon()) return;
    const SpeciesDef& sp = speciesDef(game.dragonRecord().species);
    struct Row { AnimState st; const char* label; };
    const Row rows[] = {{AnimState::Idle, "대기"},   {AnimState::Move, "걷기"},  {AnimState::Fly, "날기"},
                        {AnimState::Rest, "엎드리기"}, {AnimState::Attack, "공격"}};
    // 시트가 있는 것만 고를 수 있게 한다(없으면 대체 그림이 나와 헷갈린다)
    std::vector<Row> avail;
    for (const Row& rw : rows) {
        Sheet sh = sprites.sheet(sp.key, rw.st);
        if (sh.bmp && sh.frames > 0) avail.push_back(rw);
    }
    if (avail.empty()) return;

    const float rowH = 30.f, wdt = 150.f;
    float hcount = static_cast<float>(avail.size()) + (game.motionForced() ? 1.f : 0.f);
    RectF r(game.motionMenuAt().x, game.motionMenuAt().y, wdt, 34.f + rowH * hcount + 8.f);
    r.X = std::clamp(r.X, 0.f, std::max(0.f, width_ - r.Width));
    r.Y = std::clamp(r.Y, 0.f, std::max(0.f, height_ - r.Height));
    panel(g, r, 242);
    text(g, "모션 고르기", r.X + 12.f, r.Y + 9.f, 13.f, kSub, 0, true);
    float y = r.Y + 30.f;
    for (const Row& rw : avail) {
        bool on = game.motionForced() && game.forcedMotion() == rw.st;
        button(g, out, ctx, RectF(r.X + 8.f, y, r.Width - 16.f, rowH - 4.f), rw.label, UiAction::SetMotion,
               static_cast<int>(rw.st), true, on);
        y += rowH;
    }
    if (game.motionForced()) {
        button(g, out, ctx, RectF(r.X + 8.f, y, r.Width - 16.f, rowH - 4.f), "자동으로 되돌리기",
               UiAction::ClearMotion);
    }
}

void Renderer::drawMenu(Graphics& g, const Game& game, const RenderContext& ctx, std::vector<UiButton>& out) {
    // 위치와 크기는 저장해 둔 값으로 그린다. 글자·버튼·여백을 모두 같은 배수로 키운다.
    const float s = game.menuScale();
    const float mw = 340.f * s;
    const float mh = 510.f * s;
    Vec2 saved = game.menuPos();
    float mx = saved.x >= 0.f ? saved.x : width_ - 96.f - mw;
    float my = saved.y >= 0.f ? saved.y : height_ - 16.f - 186.f - 12.f - mh;
    // 화면 밖으로 나가지 않게 맞춘다(크기를 키우거나 해상도가 바뀌어도 사라지지 않게)
    mx = std::clamp(mx, 0.f, std::max(0.f, width_ - mw));
    my = std::clamp(my, 0.f, std::max(0.f, height_ - mh));
    RectF r(mx, my, mw, mh);
    panel(g, r, 236);
    float x = r.X + 16.f * s;
    float w = r.Width - 32.f * s;
    float y = r.Y + 12.f * s;

    // 제목 줄: 끌어서 옮기고, 오른쪽 끝은 닫기
    {
        float btn = 26.f * s;
        RectF handle(r.X, r.Y, r.Width - (btn + 14.f * s), 30.f * s);
        out.push_back({handle, UiAction::DragMenu, 0, true});
        markRegion(handle);
        bool hov = handle.Contains(PointF(ctx.mouseX, ctx.mouseY));
        for (int i = 0; i < 3; ++i)
            fillRound(g, RectF(r.X + 10.f * s, r.Y + 9.f * s + i * 4.f * s, 14.f * s, 2.f * s), 1.f,
                      Color(hov ? 220 : 120, 180, 190, 220));
        text(g, "메뉴", x + 22.f * s, y, 17.f * s, kText, 0, true);
        button(g, out, ctx, RectF(r.GetRight() - btn - 8.f * s, r.Y + 4.f * s, btn, 22.f * s), "닫기",
               UiAction::CloseMenu);
    }
    y += 34.f * s;

    // 탭: 전투 설정 / 환경 설정
    int tab = game.menuTab();
    {
        float tw = (w - 6.f * s) * 0.5f;
        button(g, out, ctx, RectF(x, y, tw, 30.f * s), "전투 설정", UiAction::SetMenuTab, 0, true, tab == 0);
        button(g, out, ctx, RectF(x + tw + 6.f * s, y, tw, 30.f * s), "환경 설정", UiAction::SetMenuTab, 1, true,
               tab == 1);
    }
    y += 40.f * s;

    if (tab == 0) {
        // ---------------- 전투 설정 ----------------
        if (game.hunting())
            button(g, out, ctx, RectF(x, y, w, 42.f * s), "사냥 중단", UiAction::StopHunt, 0, true, false, true);
        else
            button(g, out, ctx, RectF(x, y, w, 42.f * s), "사냥 시작", UiAction::StartHunt);
        y += 52.f * s;

        text(g, "사냥 모드", x, y, 13.f * s, kSub, 0, true);
        if (!game.canChangeMode()) text(g, "전투 중에는 바꿀 수 없음", r.GetRight() - 16.f * s, y, 12.f * s, kWarnC, 2);
        y += 22.f * s;
        float bw = (w - 16.f * s) / 3.f;
        const CombatMode modes[3] = {CombatMode::Direct, CombatMode::SkillManual, CombatMode::Auto};
        bool autoAvailable = autoExpRemaining(game.dragonRecord()) > 0;
        for (int i = 0; i < 3; ++i) {
            bool en = game.canChangeMode() &&
                      (modes[i] != CombatMode::Auto || autoAvailable || game.huntMode() == CombatMode::Auto);
            button(g, out, ctx, RectF(x + i * (bw + 8.f * s), y, bw, 38.f * s), combatModeName(modes[i]),
                   UiAction::SetMode, static_cast<int>(modes[i]), en, game.huntMode() == modes[i]);
        }
        y += 44.f * s;
        if (!autoAvailable) text(g, "이 드래곤은 자동 사냥 상한에 도달했습니다.", x, y, 12.f * s, kAutoC);
        y += 20.f * s;

        text(g, "싸울 때 유지할 간격", x, y, 13.f * s, kSub, 0, true);
        y += 22.f * s;
        const char* keepName[cfg::kKeepRangeCount] = {"근접", "중간", "바깥"};
        float kw = (w - 16.f * s) / 3.f;
        for (int i = 0; i < cfg::kKeepRangeCount; ++i) {
            button(g, out, ctx, RectF(x + i * (kw + 8.f * s), y, kw, 34.f * s), keepName[i], UiAction::SetKeepRange, i,
                   true, game.keepRange() == i);
        }
        y += 42.f * s;
        paragraph(g, "스킬 수동에서는 방향키를 누르는 동안 직접 밀 수 있습니다. 키보드는 싸우는 중에만 받습니다.",
                  RectF(x, y, w, 40.f * s), 11.f * s, kSub);
        y += 44.f * s;

        float hw = (w - 8.f * s) * 0.5f;
        bool towerOk = !game.waveActive();
        // 스킬 관리는 전투에 속하므로 이 탭에 둔다. 남은 스킬이 있으면 글자로 알린다
        // (전에는 ● 를 찍었는데 폰트에 없어 깨져 보였다).
        button(g, out, ctx, RectF(x, y, hw, 38.f * s), game.hasUnequippedSkill() ? "스킬 관리 (새것)" : "스킬 관리",
               UiAction::OpenSkills, 0, game.hasDragon() && game.screen() == Screen::Desktop);
        button(g, out, ctx, RectF(x + hw + 8.f * s, y, hw, 38.f * s), "탑 도전", UiAction::OpenTower, 0, towerOk);
        y += 46.f * s;
        button(g, out, ctx, RectF(x, y, w, 38.f * s), "기록·도감", UiAction::OpenCodex);
    } else {
        // ---------------- 환경 설정 ----------------
        text(g, "캐릭터 크기", x, y, 13.f * s, kSub, 0, true);
        int pct = static_cast<int>(game.charScale() * 100.f + 0.5f);
        text(g, std::to_string(pct) + "%", r.GetRight() - 16.f * s, y, 13.f * s, kText, 2, true);
        y += 20.f * s;
        slider(g, out, ctx, RectF(x, y, w, 24.f * s), game.charScale(), cfg::kCharScaleMin, cfg::kCharScaleMax,
               UiAction::DragCharSlider);
        y += 32.f * s;
        text(g, "그림만 커지고 맞는 거리·공격 범위는 그대로입니다.", x, y, 11.f * s, kSub);
        y += 26.f * s;

        toggle(g, out, ctx, RectF(x, y, w, 26.f * s), "드래곤을 아래쪽에만 두기", game.confineToBand(),
               UiAction::ToggleConfine, 0, true);
        y += 34.f * s;
        toggle(g, out, ctx, RectF(x, y, w, 26.f * s), "절전(조용할 때 프레임 줄이기)", game.powerSave(),
               UiAction::TogglePowerSave, 0, true);
        y += 32.f * s;
        paragraph(g, "노트북에서 배터리가 걱정되면 켜세요. 드래곤이 움직이는 동안에는 줄이지 않습니다.",
                  RectF(x, y, w, 34.f * s), 11.f * s, kSub);
        y += 40.f * s;

        button(g, out, ctx, RectF(x, y, w, 38.f * s),
               "드래곤 관리 (" + std::to_string(game.stable().size()) + "/" + std::to_string(cfg::kMaxStable) + ")",
               UiAction::OpenStable);
        y += 46.f * s;
        button(g, out, ctx, RectF(x, y, w, 36.f * s), "저장하고 게임 종료", UiAction::ExitGame);
    }

    // 오른쪽 아래 모서리: 끌어서 창 크기 바꾸기
    {
        float gsz = 16.f * s;
        RectF grip(r.GetRight() - gsz - 4.f, r.GetBottom() - gsz - 4.f, gsz, gsz);
        bool hov = grip.Contains(PointF(ctx.mouseX, ctx.mouseY));
        for (int i = 0; i < 3; ++i) {
            float o = i * 5.f * s;
            Pen pen(Color(hov ? 230 : 130, 180, 190, 220), 1.5f);
            g.DrawLine(&pen, grip.GetRight() - o, grip.GetBottom(), grip.GetRight(), grip.GetBottom() - o);
        }
        out.push_back({grip, UiAction::DragMenuSize, 0, true});
        markRegion(grip);
    }
}

// ---------------------------------------------------------------------------
// 창(모달)
// ---------------------------------------------------------------------------

void Renderer::drawModal(Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                         std::vector<UiButton>& out) {
    Screen sc = game.screen();
    float cx = width_ * 0.5f;
    float cy = height_ * 0.5f;
    const DragonRecord& rec = game.dragonRecord();

    switch (sc) {
        case Screen::Select:
            drawSelect(g, game, sprites, ctx, out);
            break;
        case Screen::Codex:
            drawCodex(g, game, ctx, out);
            break;
        case Screen::Stable:
            drawStable(g, game, sprites, ctx, out);
            break;
        case Screen::Skills:
            drawSkills(g, game, sprites, ctx, out);
            break;
        case Screen::AutoWarning: {
            RectF r(cx - 290.f, cy - 160.f, 580.f, 320.f);
            panel(g, r);
            text(g, "자동 사냥을 시작할까요?", r.X + 24.f, r.Y + 20.f, 20.f, kText, 0, true);
            std::string body =
                "• 드래곤이 스스로 이동·공격·스킬을 사용합니다.\n"
                "• 자동 사냥에서도 피해를 입습니다. 체력이 0이 되면 이 드래곤은 영구히 사라집니다.\n"
                "• 다른 앱이 전체화면이어도 자동 사냥은 계속됩니다(숨김은 일시정지가 아닙니다).\n"
                "• 자동으로 얻을 수 있는 경험치는 남은 " + std::to_string(autoExpRemaining(rec)) +
                "까지이며, 상한에 도달하면 자동으로 멈춥니다.";
            paragraph(g, body, RectF(r.X + 24.f, r.Y + 62.f, r.Width - 48.f, 150.f), 14.f, kText);
            text(g, "현재 체력 " + fmtInt(game.world().dragon().hp) + " / " + fmtInt(game.world().dragon().stats.maxHp),
                 r.X + 24.f, r.Y + 216.f, 14.f, kWarnC, 0, true);
            button(g, out, ctx, RectF(r.X + 24.f, r.GetBottom() - 64.f, 250.f, 44.f), "자동 사냥 시작", UiAction::ConfirmAuto);
            button(g, out, ctx, RectF(r.GetRight() - 24.f - 180.f, r.GetBottom() - 64.f, 180.f, 44.f), "취소",
                   UiAction::CloseModal);
            break;
        }
        case Screen::TowerWarning: {
            RectF r(cx - 310.f, cy - 200.f, 620.f, 400.f);
            panel(g, r);
            text(g, "탑에 도전할까요?", r.X + 24.f, r.Y + 20.f, 20.f, kText, 0, true);
            text(g, "3층 구성 · 마지막 층에는 탑의 수호자가 기다립니다.", r.X + 24.f, r.Y + 56.f, 14.f, kSub);
            bool low = rec.level < cfg::kTowerRecommendedLevel;
            text(g, "현재 Lv." + std::to_string(rec.level) + "  ·  권장 Lv." + std::to_string(cfg::kTowerRecommendedLevel),
                 r.X + 24.f, r.Y + 84.f, 16.f, low ? kWarnC : kHpC, 0, true);
            RectF danger(r.X + 24.f, r.Y + 116.f, r.Width - 48.f, 92.f);
            fillRound(g, danger, 8.f, Color(120, 120, 20, 24));
            paragraph(g,
                      "탑에서 체력이 0이 되면 " + dragonName(rec) + "은(는) 영구히 사라지고 다시 사용할 수 없습니다. "
                      "레벨·경험치·스킬은 모두 사라지며, 탑 최고 기록과 도감만 남습니다.",
                      RectF(danger.X + 12.f, danger.Y + 10.f, danger.Width - 24.f, danger.Height - 16.f), 14.f, 0xFFFFC2C2,
                      0, true);
            paragraph(g,
                      "탑에서는 자동 사냥을 쓸 수 없습니다. 들어가면 체력이 모두 회복되고, 층을 돌파할 때마다 30% 회복합니다. "
                      "층 사이에서 탑을 나가면 드래곤은 살아서 돌아옵니다.",
                      RectF(r.X + 24.f, r.Y + 220.f, r.Width - 48.f, 60.f), 13.f, kSub);
            float bw = (r.Width - 48.f - 16.f) / 3.f;
            float by = r.GetBottom() - 68.f;
            button(g, out, ctx, RectF(r.X + 24.f, by, bw, 46.f), "직접 조작으로 도전", UiAction::EnterTower,
                   static_cast<int>(CombatMode::Direct));
            button(g, out, ctx, RectF(r.X + 24.f + bw + 8.f, by, bw, 46.f), "스킬 수동으로 도전", UiAction::EnterTower,
                   static_cast<int>(CombatMode::SkillManual));
            button(g, out, ctx, RectF(r.X + 24.f + (bw + 8.f) * 2.f, by, bw, 46.f), "취소", UiAction::CloseModal);
            break;
        }
        case Screen::FloorClear: {
            RectF r(cx - 260.f, cy - 130.f, 520.f, 260.f);
            panel(g, r);
            text(g, std::to_string(game.towerFloor()) + "층 돌파!", r.X + r.Width * 0.5f, r.Y + 24.f, 24.f, 0xFFFFE9A8, 1,
                 true);
            text(g, "체력이 30% 회복되었습니다.", r.X + r.Width * 0.5f, r.Y + 66.f, 15.f, kText, 1);
            const DragonActor& d = game.world().dragon();
            bar(g, RectF(r.X + 60.f, r.Y + 100.f, r.Width - 120.f, 16.f), d.hp / d.stats.maxHp, kHpC,
                "체력 " + fmtInt(d.hp) + " / " + fmtInt(d.stats.maxHp));
            std::string next = game.towerFloor() + 1 == Game::kTowerFloors ? "다음은 수호자의 층입니다." : "다음 층으로 갈까요?";
            text(g, next, r.X + r.Width * 0.5f, r.Y + 130.f, 14.f, kSub, 1);
            button(g, out, ctx, RectF(r.X + 24.f, r.GetBottom() - 66.f, 220.f, 46.f), "다음 층", UiAction::NextFloor);
            button(g, out, ctx, RectF(r.GetRight() - 24.f - 220.f, r.GetBottom() - 66.f, 220.f, 46.f), "탑에서 나가기",
                   UiAction::LeaveTower);
            break;
        }
        case Screen::TowerVictory: {
            RectF r(cx - 260.f, cy - 130.f, 520.f, 260.f);
            panel(g, r);
            text(g, "탑 정복!", r.X + r.Width * 0.5f, r.Y + 26.f, 28.f, 0xFFFFD86B, 1, true);
            text(g, dragonName(rec) + "이(가) 탑의 수호자를 쓰러뜨렸습니다.", r.X + r.Width * 0.5f, r.Y + 76.f, 15.f, kText,
                 1);
            text(g, "보상: +" + std::to_string(cfg::kTowerClearExp) + " EXP  ·  정복 횟수 " +
                        std::to_string(game.save().towerClears),
                 r.X + r.Width * 0.5f, r.Y + 106.f, 15.f, kExpC, 1, true);
            button(g, out, ctx, RectF(r.X + r.Width * 0.5f - 120.f, r.GetBottom() - 70.f, 240.f, 46.f), "바탕화면으로",
                   UiAction::LeaveTower);
            break;
        }
        case Screen::Death: {
            RectF r(cx - 290.f, cy - 150.f, 580.f, 300.f);
            panel(g, r, 245);
            const GraveEntry* gr = game.lastGrave();
            text(g, "드래곤이 쓰러졌습니다", r.X + r.Width * 0.5f, r.Y + 24.f, 24.f, kDanger, 1, true);
            if (gr) {
                std::string who = std::string(speciesDef(gr->species).name) + " #" + std::to_string(gr->id) + " · Lv." +
                                  std::to_string(gr->level);
                text(g, who, r.X + r.Width * 0.5f, r.Y + 70.f, 17.f, kText, 1, true);
                text(g, "쓰러진 곳: " + placeLabel(gr->place), r.X + r.Width * 0.5f, r.Y + 100.f, 14.f, kSub, 1);
            }
            paragraph(g, "이 드래곤은 다시 사용할 수 없습니다. 탑 최고 기록(" + std::to_string(game.save().bestFloor) +
                             "층)과 도감은 남아 있습니다. 새 드래곤으로 다시 도전하세요.",
                      RectF(r.X + 40.f, r.Y + 130.f, r.Width - 80.f, 60.f), 14.f, kText, 1);
            button(g, out, ctx, RectF(r.X + r.Width * 0.5f - 130.f, r.GetBottom() - 70.f, 260.f, 46.f), "새 드래곤 선택",
                   UiAction::AckDeath);
            break;
        }
        default:
            break;
    }
}

void Renderer::drawSelect(Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                          std::vector<UiButton>& out) {
    // v0.2: 종이 폴더로 늘어날 수 있으므로 화면 너비에 맞춰 카드 크기를 줄이고,
    // 그래도 넘치면 들어가는 만큼만 보여 준다.
    const float gap = 14.f;
    const float minCardW = 150.f;
    const float maxCardW = 222.f;
    const float availW = width_ - 80.f - 48.f;  // 양옆 여백과 패널 안쪽 여백
    int total = speciesCount();
    int shown = std::max(1, total);
    float cardW = maxCardW;
    while (shown > 1) {
        cardW = (availW - gap * (shown - 1)) / shown;
        if (cardW >= minCardW) break;
        --shown;
    }
    cardW = std::clamp(cardW, minCardW, maxCardW);

    const float pw = cardW * shown + gap * (shown - 1) + 48.f;
    const float ph = 600.f;
    RectF r(width_ * 0.5f - pw * 0.5f, height_ * 0.5f - ph * 0.5f, pw, ph);
    panel(g, r, 242);
    // v1.3: 관리창에서 "새 드래곤"으로 들어왔으면 돌아갈 수 있어야 한다.
    bool adding = game.addingDragon();
    text(g, adding ? "새 드래곤 데려오기" : "드래곤 선택", r.X + 24.f, r.Y + 18.f, 22.f, kText, 0, true);
    std::string sub = adding ? "보유 목록에 더합니다. 지금 띄워 둔 드래곤은 그대로 있습니다."
                             : "함께 탑을 오를 드래곤 한 마리를 고르세요. 이름과 수치는 임시안입니다.";
    if (shown < total) sub += "  (" + std::to_string(total) + "종 중 " + std::to_string(shown) + "종 표시)";
    text(g, sub, r.X + 24.f, r.Y + 54.f, 14.f, kSub);
    float hx = r.GetRight() - 24.f - 130.f;
    if (adding) {
        button(g, out, ctx, RectF(hx, r.Y + 18.f, 130.f, 34.f), "관리창으로", UiAction::OpenStable);
        hx -= 138.f;
    }
    if (!game.save().graves.empty() || game.save().bestFloor > 0) {
        button(g, out, ctx, RectF(hx, r.Y + 18.f, 130.f, 34.f), "기록·도감", UiAction::OpenCodex);
    }

    float y0 = r.Y + 88.f;
    for (int i = 0; i < shown; ++i) {
        const SpeciesDef& sp = speciesDef(static_cast<Species>(i));
        RectF c(r.X + 24.f + i * (cardW + gap), y0, cardW, ph - 88.f - 72.f);
        bool hover = c.Contains(PointF(ctx.mouseX, ctx.mouseY));
        fillRound(g, c, 10.f, hover ? Color(255, 44, 48, 68) : Color(255, 34, 37, 52));
        strokeRound(g, c, 10.f, col(sp.color, hover ? 1.f : 0.6f), hover ? 2.5f : 1.5f);

        DragonActor preview;
        preview.species = sp.id;
        preview.facing = Vec2{1.f, 0.f};
        preview.anim = AnimState::Idle;
        drawDragon(g, preview, sprites, ctx.time + i, c.X + c.Width * 0.5f, c.Y + 62.f, 96.f, true);

        float x = c.X + 14.f;
        float y = c.Y + 128.f;
        text(g, sp.name, x, y, 17.f, kText, 0, true);
        text(g, sp.role, c.GetRight() - 14.f, y + 2.f, 13.f, sp.color, 2, true);
        y += 28.f;
        paragraph(g, sp.desc, RectF(x, y, c.Width - 28.f, 40.f), 12.f, kSub);
        y += 42.f;
        DragonStats st = statsFor(sp.id, 1);
        text(g, "체력 " + fmtInt(st.maxHp) + " · 공격 " + fmtInt(st.atk) + " · 방어 " + fmtInt(st.def), x, y, 12.f, kText);
        y += 18.f;
        std::string line2 = "속도 " + fmtInt(st.speed) + " · 회피 " + fmtPct(st.dodge);
        if (st.regen > 0.f) line2 += " · 재생";
        text(g, line2, x, y, 12.f, kText);
        y += 26.f;
        for (const SkillDef& s : sp.skills) {
            text(g, s.name, x, y, 12.f, 0xFFE8EAF2, 0, true);
            text(g, "Lv." + std::to_string(s.unlockLevel), c.GetRight() - 14.f, y, 12.f, kSub, 2);
            y += 17.f;
            paragraph(g, s.desc, RectF(x, y, c.Width - 28.f, 30.f), 11.f, 0xFF9AA2B4);
            y += 30.f;
        }
        button(g, out, ctx, RectF(c.X + 14.f, c.GetBottom() - 52.f, c.Width - 28.f, 40.f), "이 드래곤으로 시작",
               UiAction::ChooseSpecies, i);
    }
    RectF warn(r.X + 24.f, r.GetBottom() - 60.f, r.Width - 48.f, 40.f);
    fillRound(g, warn, 8.f, Color(110, 120, 20, 24));
    text(g, "드래곤이 쓰러지면 그 개체는 되살릴 수 없습니다. 탑 최고 기록과 도감은 남습니다.", warn.X + warn.Width * 0.5f,
         warn.Y + 10.f, 14.f, 0xFFFFC2C2, 1, true);
}

void Renderer::drawStable(Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                          std::vector<UiButton>& out) {
    const std::vector<DragonRecord>& list = game.stable();
    const SaveData& s = game.save();
    const int rows = cfg::kMaxStable;
    RectF r(width_ * 0.5f - 350.f, height_ * 0.5f - 270.f, 700.f, 540.f);
    panel(g, r, 244);
    text(g, "드래곤 관리", r.X + 24.f, r.Y + 18.f, 22.f, kText, 0, true);
    text(g, std::to_string(list.size()) + " / " + std::to_string(cfg::kMaxStable) + " 마리",
         r.GetRight() - 24.f, r.Y + 24.f, 14.f, kSub, 2);

    const char* why = game.switchBlockedReason();
    text(g, why ? why : "바탕화면에 띄운 드래곤이 지금 플레이하는 드래곤입니다.", r.X + 24.f, r.Y + 48.f, 13.f,
         why ? kWarnC : kSub);

    float y = r.Y + 76.f;
    const float rowH = 84.f;
    for (int i = 0; i < rows; ++i) {
        RectF row(r.X + 20.f, y, r.Width - 40.f, rowH - 8.f);
        bool filled = i < static_cast<int>(list.size());
        if (!filled) {
            // 빈 자리: 새 드래곤을 데려온다
            fillRound(g, row, 10.f, Color(90, 40, 44, 58));
            strokeRound(g, row, 10.f, Color(120, 90, 100, 130), 1.f);
            text(g, "비어 있음", row.X + 18.f, row.Y + 26.f, 14.f, 0xFF7C8090, 0, true);
            button(g, out, ctx, RectF(row.GetRight() - 170.f, row.Y + 18.f, 150.f, 40.f), "새 드래곤",
                   UiAction::AddDragon, 0, true);
            y += rowH;
            continue;
        }
        const DragonRecord& d = list[static_cast<size_t>(i)];
        const SpeciesDef& sp = speciesDef(d.species);
        bool active = i == game.activeIndex() && game.hasDragon();
        fillRound(g, row, 10.f, active ? Color(235, 42, 60, 96) : Color(215, 32, 34, 46));
        strokeRound(g, row, 10.f, active ? Color(255, 120, 180, 255) : Color(120, 80, 88, 120), active ? 2.f : 1.f);

        // 작은 미리보기
        DragonActor preview;
        preview.species = d.species;
        preview.alive = true;
        preview.anim = AnimState::Idle;
        drawDragon(g, preview, sprites, ctx.time + i * 0.7f, row.X + 48.f, row.Y + 40.f, 66.f, true);

        float tx = row.X + 96.f;
        fillCircle(g, tx + 6.f, row.Y + 18.f, 6.f, col(sp.color));
        text(g, dragonName(d), tx + 18.f, row.Y + 9.f, 16.f, kText, 0, true);
        text(g, "Lv." + std::to_string(d.level), tx + 18.f + 150.f, row.Y + 10.f, 15.f, 0xFFFFE9A8, 0, true);

        float maxHp = game.maxHpOf(d);
        float hp = active ? game.world().dragon().hp : (d.hp < 0.f ? maxHp : d.hp);
        bar(g, RectF(tx + 18.f, row.Y + 34.f, 240.f, 14.f), hp / std::max(1.f, maxHp), kHpC,
            "체력 " + fmtInt(hp) + " / " + fmtInt(maxHp));
        if (!active) {
            bool full = hp >= maxHp - 0.5f;
            text(g, full ? "쉬는 중 · 체력 가득" : "쉬는 중 · 회복하고 있습니다", tx + 18.f, row.Y + 54.f, 12.f,
                 full ? 0xFF8CFFA0 : kSub);
        } else {
            text(g, "바탕화면에 떠 있음 · 플레이 중", tx + 18.f, row.Y + 54.f, 12.f, 0xFFBFD4FF);
        }

        RectF btn(row.GetRight() - 170.f, row.Y + 18.f, 150.f, 40.f);
        if (active) {
            button(g, out, ctx, btn, "표시 중", UiAction::None, 0, false, true);
        } else {
            button(g, out, ctx, btn, "바탕화면에 표시", UiAction::ShowDragon, i, game.canSwitchDragon());
        }
        y += rowH;
    }

    // 묘비
    y += 6.f;
    text(g, "묘비 (" + std::to_string(s.graves.size()) + ")", r.X + 24.f, y, 14.f, kSub, 0, true);
    y += 22.f;
    if (s.graves.empty()) {
        text(g, "아직 없습니다.", r.X + 24.f, y, 13.f, 0xFF7C8090);
    } else {
        // 최근 것부터 한 줄에 둘씩
        int shown = 0;
        for (auto it = s.graves.rbegin(); it != s.graves.rend() && shown < 4; ++it, ++shown) {
            float bx = r.X + 24.f + (shown % 2) * 330.f;
            float by = y + (shown / 2) * 20.f;
            text(g, "✝ " + speciesDef(it->species).name + " Lv." + std::to_string(it->level) + " · " +
                        placeLabel(it->place),
                 bx, by, 13.f, 0xFFB9A8C8);
        }
        if (s.graves.size() > 4)
            text(g, "그 밖 " + std::to_string(s.graves.size() - 4) + "마리는 기록·도감에서 볼 수 있습니다.",
                 r.X + 24.f, y + 44.f, 12.f, 0xFF7C8090);
    }

    // 닫기 — 띄운 개체가 없으면 닫을 수 없다(드래곤 없이 바탕화면만 남지 않게)
    bool canClose = game.hasDragon();
    button(g, out, ctx, RectF(r.GetRight() - 150.f, r.GetBottom() - 54.f, 130.f, 40.f),
           canClose ? "닫기" : "띄울 드래곤을 고르세요", UiAction::CloseStable, 0, canClose);
}
// ---------------------------------------------------------------------------
// v1.4 스킬 관리창
// ---------------------------------------------------------------------------
// 위쪽에 장착 4칸, 아래에 그 종의 스킬 전부. 칸을 고르고 스킬을 누르면 그 칸에 들어간다.
// 패시브는 칸을 쓰지 않으므로 목록에서 "항상 적용"으로만 보여 준다.
void Renderer::drawSkills(Graphics& g, const Game& game, const Sprites& sprites,
                          const RenderContext& ctx, std::vector<UiButton>& out) {
    const DragonRecord& rec = game.dragonRecord();
    const SpeciesDef& sp = speciesDef(rec.species);
    const DragonActor& d = game.world().dragon();
    const int sel = game.selectedSlot();

    // 창 높이는 스킬 수에 맞춘다. 화면이 작으면 들어가는 만큼만 보여 주고 쪽으로 넘긴다.
    const int total = static_cast<int>(sp.skills.size());
    const float rowH = 48.f;
    const float headTop = 76.f;    // 제목·안내
    const float slotsH = 56.f + 44.f;  // 장착 칸 + 이름 줄
    const float listHeadH = 28.f;
    const float footerH = 66.f;
    const float chromeH = headTop + slotsH + listHeadH + footerH;
    float maxH = std::max(320.f, height_ - 60.f);
    float ph = std::min(chromeH + rowH * static_cast<float>(std::max(1, total)), maxH);
    int rowsPerPage = static_cast<int>((ph - chromeH) / rowH);
    if (rowsPerPage < 1) rowsPerPage = 1;
    const int pages = (total + rowsPerPage - 1) / rowsPerPage;
    const int page = std::clamp(game.skillPage(), 0, std::max(0, pages - 1));

    const float pw = std::min(760.f, std::max(560.f, width_ - 60.f));
    RectF r(width_ * 0.5f - pw * 0.5f, std::max(10.f, height_ * 0.5f - ph * 0.5f), pw, ph);
    panel(g, r, 244);
    text(g, "스킬 관리", r.X + 24.f, r.Y + 18.f, 22.f, kText, 0, true);
    text(g, std::string(sp.name) + " Lv." + std::to_string(rec.level), r.GetRight() - 24.f, r.Y + 24.f, 14.f, kSub, 2);

    const char* why = game.switchBlockedReason();
    text(g, why ? why : "칸을 고른 뒤 아래 스킬을 누르면 그 칸에 들어갑니다. 레벨이 오를 때마다 하나씩 해제됩니다.",
         r.X + 24.f, r.Y + 48.f, 13.f, why ? kWarnC : kSub);
    const bool canEdit = game.canSwitchDragon();

    // ---- 장착 4칸 ----
    float y = r.Y + headTop;
    const float box = 56.f;
    for (int slot = 0; slot < kSkillSlots; ++slot) {
        RectF b(r.X + 24.f + slot * (box + 14.f), y, box, box);
        int idx = game.skillSlots()[static_cast<size_t>(slot)];
        drawSkillBox(g, sprites, sp, d, idx, b, rec.level, std::to_string(slot + 1));
        if (slot == sel) strokeRound(g, b, 6.f, Color(255, 255, 220, 140), 2.5f);
        // 칸 전체가 "이 칸 고르기" 버튼이다(그림은 drawSkillBox가 이미 그렸다)
        out.push_back({b, UiAction::SelectSlot, slot, true});
        markRegion(b);
        // 이름과 비우기
        float tx = b.X;
        if (idx >= 0 && idx < static_cast<int>(sp.skills.size())) {
            text(g, sp.skills[static_cast<size_t>(idx)].name, tx + box * 0.5f, b.GetBottom() + 4.f, 11.f, kSub, 1);
        } else {
            text(g, "비어 있음", tx + box * 0.5f, b.GetBottom() + 4.f, 11.f, 0xFF7C8090, 1);
        }
    }
    {
        float bx = r.X + 24.f + kSkillSlots * (box + 14.f) + 6.f;
        int cur = game.skillSlots()[static_cast<size_t>(sel)];
        button(g, out, ctx, RectF(bx, y + 8.f, 110.f, 36.f), std::to_string(sel + 1) + "번 비우기",
               UiAction::ClearSlot, sel, canEdit && cur >= 0);
        text(g, "키 1~4가 이 칸입니다.", bx, y + 50.f, 11.f, kSub);
    }

    // ---- 스킬 목록 ----
    y = r.Y + headTop + slotsH;
    {
        std::string head = "이 드래곤의 스킬 " + std::to_string(total) + "개";
        if (pages > 1) head += "  (" + std::to_string(page + 1) + "/" + std::to_string(pages) + " 쪽)";
        text(g, head, r.X + 24.f, y, 14.f, kSub, 0, true);
        if (pages > 1) {
            button(g, out, ctx, RectF(r.GetRight() - 92.f, y - 6.f, 32.f, 24.f), "◀", UiAction::SkillPage,
                   page - 1, page > 0);
            button(g, out, ctx, RectF(r.GetRight() - 56.f, y - 6.f, 32.f, 24.f), "▶", UiAction::SkillPage,
                   page + 1, page < pages - 1);
        }
    }
    y += listHeadH;

    const int first = page * rowsPerPage;
    const int last = std::min(total, first + rowsPerPage);
    for (int i = first; i < last; ++i) {
        const SkillDef& s = sp.skills[static_cast<size_t>(i)];
        const bool unlocked = rec.level >= s.unlockLevel;
        const bool passive = skillIsPassive(s.kind);
        bool equipped = false;
        int inSlot = -1;
        for (int k = 0; k < kSkillSlots; ++k)
            if (game.skillSlots()[static_cast<size_t>(k)] == i) { equipped = true; inSlot = k; }

        RectF row(r.X + 24.f, y, r.Width - 48.f, rowH - 6.f);
        const float rx = row.GetRight();
        fillRound(g, row, 8.f, equipped ? Color(225, 42, 60, 96) : Color(200, 32, 34, 46));
        strokeRound(g, row, 8.f, equipped ? Color(255, 120, 180, 255) : Color(110, 80, 88, 120), equipped ? 2.f : 1.f);

        RectF icon(row.X + 7.f, row.Y + 5.f, 32.f, 32.f);
        drawSkillBox(g, sprites, sp, d, i, icon, rec.level, "");

        float tx = row.X + 48.f;
        // 이름 길이에 상관없이 겹치지 않도록, 오른쪽 글자는 버튼 왼쪽 끝에 맞춰 오른쪽 정렬한다.
        const float labelRight = rx - 136.f;
        uint32_t nameColor = unlocked ? kText : 0xFF8A8FA0;
        text(g, s.name, tx, row.Y + 5.f, 15.f, nameColor, 0, true);
        text(g, "Lv." + std::to_string(s.unlockLevel), labelRight, row.Y + 6.f, 13.f,
             unlocked ? 0xFFFFE9A8 : 0xFF8A8FA0, 2, true);
        if (passive) text(g, "패시브 · 항상 적용", labelRight, row.Y + 25.f, 12.f, 0xFF9CE8C0, 2, true);
        else if (inSlot >= 0)
            text(g, std::to_string(inSlot + 1) + "번 칸", labelRight, row.Y + 25.f, 12.f, 0xFFBFD4FF, 2, true);

        std::string desc = s.desc;
        if (passive && s.kind == SkillKind::PassiveStat) {
            // hp/atk/def/speed 는 비율, dodge/regen 은 더하기다(Species.cpp statsFor 참고).
            bool ratio = s.stat != StatKind::Dodge && s.stat != StatKind::Regen;
            std::string amount = ratio ? "+" + fmtPct(s.power)
                                 : s.stat == StatKind::Dodge ? "+" + fmtPct(s.power) + "p"
                                                             : "+" + fmtInt(s.power);
            desc += "  (" + std::string(statKindLabel(s.stat)) + " " + amount + ")";
        } else if (!passive) {
            desc += "  (재사용 " + fmtInt(s.cooldown) + "초)";
        }
        text(g, desc, tx, row.Y + 24.f, 12.f, kSub);

        RectF btn(rx - 126.f, row.Y + 6.f, 116.f, 30.f);
        if (passive) {
            button(g, out, ctx, btn, unlocked ? "적용 중" : "잠김", UiAction::None, 0, false, unlocked);
        } else if (!unlocked) {
            button(g, out, ctx, btn, "Lv." + std::to_string(s.unlockLevel) + " 필요", UiAction::None, 0, false);
        } else {
            button(g, out, ctx, btn, equipped ? (inSlot == sel ? "장착됨" : "이 칸으로") : std::to_string(sel + 1) + "번에 장착",
                   UiAction::EquipSkill, i, canEdit && inSlot != sel, equipped);
        }
        y += rowH;
    }
    if (total == 0) text(g, "이 드래곤에게는 아직 스킬이 없습니다.", r.X + 24.f, y, 13.f, 0xFF7C8090);

    button(g, out, ctx, RectF(r.GetRight() - 150.f, r.GetBottom() - 52.f, 130.f, 38.f), "닫기",
           UiAction::CloseSkills);
}


void Renderer::drawCodex(Graphics& g, const Game& game, const RenderContext& ctx, std::vector<UiButton>& out) {
    const SaveData& s = game.save();
    RectF r(width_ * 0.5f - 330.f, height_ * 0.5f - 250.f, 660.f, 500.f);
    panel(g, r, 244);
    text(g, "기록·도감", r.X + 24.f, r.Y + 18.f, 22.f, kText, 0, true);
    float x = r.X + 24.f;
    float y = r.Y + 62.f;
    text(g, "탑 최고 도달 층: " + std::to_string(s.bestFloor) + " / " + std::to_string(Game::kTowerFloors) +
                "   ·   탑 정복 " + std::to_string(s.towerClears) + "회",
         x, y, 15.f, 0xFFFFE9A8, 0, true);
    y += 34.f;
    int total = speciesCount();
    int seenCount = 0;
    for (int i = 0; i < total; ++i)
        if (s.sawSpecies(static_cast<Species>(i))) ++seenCount;
    text(g, "드래곤 도감 (" + std::to_string(seenCount) + " / " + std::to_string(total) + ")", x, y, 14.f, kSub, 0, true);
    y += 24.f;
    // 종이 늘어날 수 있으므로 한 줄에 4개씩 줄바꿈한다.
    const int perRow = 4;
    for (int i = 0; i < total; ++i) {
        const SpeciesDef& sp = speciesDef(static_cast<Species>(i));
        bool seen = s.sawSpecies(sp.id);
        float bx = x + (i % perRow) * 150.f;
        float by = y + (i / perRow) * 22.f;
        fillCircle(g, bx + 8.f, by + 9.f, 7.f, seen ? col(sp.color) : Color(255, 70, 72, 80));
        text(g, seen ? sp.name : std::string("???"), bx + 22.f, by, 13.f, seen ? kText : 0xFF7C8090);
    }
    y += ((total + perRow - 1) / perRow) * 22.f + 10.f;
    text(g, "만난 적", x, y, 14.f, kSub, 0, true);
    y += 24.f;
    {
        int et = enemyCount();
        for (int i = 0; i < et; ++i) {
            EnemyKind k = static_cast<EnemyKind>(i);
            const EnemyDef& d = enemyDef(k);
            bool seen = s.sawEnemy(k);
            float bx = x + (i % perRow) * 150.f;
            float by = y + (i / perRow) * 22.f;
            fillCircle(g, bx + 8.f, by + 9.f, 7.f, seen ? col(d.color) : Color(255, 70, 72, 80));
            std::string label = seen ? d.name + (d.role == EnemyRole::Boss ? " (보스)" : "") : std::string("???");
            text(g, label, bx + 22.f, by, 13.f, seen ? kText : 0xFF7C8090);
        }
        y += ((et + perRow - 1) / perRow) * 22.f + 12.f;
    }
    text(g, "쓰러진 드래곤 (" + std::to_string(s.graves.size()) + ")", x, y, 14.f, kSub, 0, true);
    y += 24.f;
    if (s.graves.empty()) {
        text(g, "아직 없습니다.", x, y, 13.f, 0xFF7C8090);
    } else {
        // 도감 줄 수에 따라 남는 높이만큼만 보여 준다(종이 늘어나도 패널을 넘지 않게).
        int room = static_cast<int>((r.GetBottom() - 74.f - y) / 22.f);
        int limit = std::clamp(room, 1, 8);
        int shown = 0;
        for (auto it = s.graves.rbegin(); it != s.graves.rend() && shown < limit; ++it, ++shown) {
            const SpeciesDef& sp = speciesDef(it->species);
            std::string line = std::string(sp.name) + " #" + std::to_string(it->id) + "  ·  Lv." +
                               std::to_string(it->level) + "  ·  " + placeLabel(it->place);
            fillCircle(g, x + 6.f, y + 9.f, 5.f, col(sp.color, 0.8f));
            text(g, line, x + 18.f, y, 13.f, kText);
            y += 22.f;
        }
    }
    button(g, out, ctx, RectF(r.GetRight() - 24.f - 160.f, r.GetBottom() - 62.f, 160.f, 42.f), "닫기", UiAction::CloseModal);
}

}  // namespace dt::win
