// HUD 그리기: 상태창(스킬 칸 포함)·알림·탑 표시.
#include "RendererInternal.h"

namespace dt::win {

// 스킬 한 칸. 상태창(장착 4칸)과 스킬 관리창(목록)이 함께 쓴다.
// skillIndex < 0 이면 빈 칸, 해제 전이면 필요한 레벨을 보여 준다.
void Renderer::drawSkillBox(Graphics& g, const Sprites& sprites, const SpeciesDef& sp,
                            const DragonActor& d, int skillIndex,
                            const RectF& b, int level, const std::string& cornerLabel) {
    const bool empty = skillIndex < 0 || skillIndex >= static_cast<int>(sp.skills.size());
    if (empty) {
        fillRound(g, b, 6.f, Color(150, 30, 32, 40));
        strokeRound(g, b, 6.f, Color(120, 90, 96, 120), 1.f);
        // 폰트에 없는 글자를 쓰면 깨져 보인다(v1.4에서 겪음). 평범한 더하기 기호를 쓴다.
        text(g, "+", b.X + b.Width * 0.5f, b.Y + b.Height * 0.5f - 11.f, 17.f, 0xFF6E7380, 1, true);
        if (!cornerLabel.empty())
            text(g, cornerLabel, b.X + 3.f, b.Y + b.Height - 15.f, 11.f, 0xFF8A8FA0, 0, true, true);
        return;
    }
    const SkillDef& s = sp.skills[static_cast<size_t>(skillIndex)];
    const bool unlocked = level >= s.unlockLevel;
    const bool passive = skillIsPassive(s.kind);
    fillRound(g, b, 6.f, unlocked ? col(darken(sp.color, passive ? 0.60f : 0.45f)) : Color(255, 40, 42, 52));
    if (unlocked) {
        // v1.5: 종 폴더의 icon_<번호>.png 가 있으면 그림으로, 없으면 이름 첫 글자로 그린다.
        Bitmap* icon = sprites.get("skillicon_" + sp.key + "_" + std::to_string(skillIndex + 1));
        if (icon) {
            // 비율을 지켜 칸 안에 넣는다(아이콘은 정사각이지만 늘리지 않는다).
            // 대기시간·테두리·번호는 아래에서 이 위에 덧그려진다.
            float side = std::min(b.Width, b.Height) - 4.f;
            g.DrawImage(icon, RectF(b.X + (b.Width - side) * 0.5f, b.Y + (b.Height - side) * 0.5f, side, side));
        } else {
            std::string first = s.name;
            size_t len = 1;
            if (!first.empty()) {
                unsigned char c0 = static_cast<unsigned char>(first[0]);
                if (c0 >= 0xF0) len = 4; else if (c0 >= 0xE0) len = 3; else if (c0 >= 0xC0) len = 2;
            }
            text(g, first.substr(0, len), b.X + b.Width * 0.5f, b.Y + b.Height * 0.5f - 12.f, 17.f, kText, 1, true);
        }
    } else {
        text(g, "Lv." + std::to_string(s.unlockLevel), b.X + b.Width * 0.5f, b.Y + b.Height * 0.5f - 8.f, 12.f,
             0xFF8A8FA0, 1, true);
    }
    // 패시브는 대기시간이 없다. 액티브만 남은 시간을 덮어 그린다.
    if (unlocked && !passive && skillIndex < static_cast<int>(d.skillCd.size())) {
        float cd = d.skillCd[static_cast<size_t>(skillIndex)];
        if (cd > 0.f && s.cooldown > 0.f) {
            float ratio = std::min(1.f, cd / s.cooldown);
            fillRound(g, RectF(b.X, b.Y + b.Height * (1.f - ratio), b.Width, b.Height * ratio), 6.f,
                      Color(170, 0, 0, 0));
            text(g, fmtInt(cd), b.X + b.Width * 0.5f, b.Y + b.Height * 0.5f - 9.f, 14.f, kText, 1, true, true);
        }
    }
    strokeRound(g, b, 6.f, Color(200, 120, 130, 170), 1.f);
    if (passive) text(g, "P", b.GetRight() - 11.f, b.Y + 2.f, 11.f, 0xFF9CE8C0, 0, true, true);
    if (!cornerLabel.empty())
        text(g, cornerLabel, b.X + 3.f, b.Y + b.Height - 15.f, 11.f, 0xFFFFE9A8, 0, true, true);
}

void Renderer::drawStatusCard(Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                              std::vector<UiButton>& out) {
    const DragonRecord& rec = game.dragonRecord();
    const DragonActor& d = game.world().dragon();
    const SpeciesDef& sp = speciesDef(rec.species);
    const float cardW = 270.f;
    float cardH = game.panelCollapsed() ? 44.f : 292.f;
    Vec2 saved = game.panelPos();
    float rx = saved.x >= 0.f ? saved.x : width_ - 96.f - cardW;
    float ry = saved.y >= 0.f ? saved.y : height_ - 16.f - 292.f;
    // 화면 밖으로 나가지 않게 맞춘다(해상도가 바뀌어도 사라지지 않게)
    rx = std::clamp(rx, 0.f, std::max(0.f, width_ - cardW));
    ry = std::clamp(ry, 0.f, std::max(0.f, height_ - cardH));
    RectF r(rx, ry, cardW, cardH);
    panel(g, r, 224);

    // 제목 줄은 끌기 손잡이다. 오른쪽 끝에 접기 버튼이 있다.
    // v1.4: 전에는 ▦·≡ 같은 글자를 썼는데 폰트에 없어 깨져 보였다. 한글로 적는다.
    RectF handle(r.X, r.Y, r.Width - 126.f, 30.f);
    out.push_back({handle, UiAction::DragPanel, 0, true});
    markRegion(handle);
    button(g, out, ctx, RectF(r.GetRight() - 122.f, r.Y + 4.f, 56.f, 22.f), "관리", UiAction::OpenStable);
    button(g, out, ctx, RectF(r.GetRight() - 62.f, r.Y + 4.f, 56.f, 22.f), "메뉴", UiAction::ToggleMenu, 0, true,
           game.menuOpen());
    {
        bool hov = handle.Contains(PointF(ctx.mouseX, ctx.mouseY));
        for (int i = 0; i < 3; ++i)
            fillRound(g, RectF(r.X + 10.f, r.Y + 9.f + i * 4.f, 14.f, 2.f), 1.f,
                      Color(hov ? 220 : 120, 180, 190, 220));
    }
    button(g, out, ctx, RectF(r.GetRight() - 30.f, r.Y + 4.f, 26.f, 22.f),
           game.panelCollapsed() ? "▴" : "▾", UiAction::TogglePanel);
    if (game.panelCollapsed()) {
        text(g, dragonName(rec) + "  Lv." + std::to_string(rec.level), r.X + 30.f, r.Y + 13.f, 14.f, kText, 0, true);
        bar(g, RectF(r.X + 14.f, r.Y + 32.f, r.Width - 28.f, 8.f), d.hp / std::max(1.f, d.stats.maxHp), kHpC);
        return;
    }
    float x = r.X + 14.f;
    float y = r.Y + 32.f;  // 제목 줄(끌기 손잡이) 아래부터
    float w = r.Width - 28.f;

    fillCircle(g, x + 6.f, y + 10.f, 6.f, col(sp.color));
    text(g, dragonName(rec), x + 18.f, y, 15.f, kText, 0, true);
    text(g, "Lv." + std::to_string(rec.level), r.GetRight() - 14.f, y, 15.f, 0xFFFFE9A8, 2, true);
    y += 26.f;
    bar(g, RectF(x, y, w, 14.f), d.hp / std::max(1.f, d.stats.maxHp), kHpC,
        "체력 " + fmtInt(d.hp) + " / " + fmtInt(d.stats.maxHp));
    y += 19.f;
    if (rec.level >= cfg::kMaxLevel) {
        bar(g, RectF(x, y, w, 12.f), 1.f, kExpC, "최고 레벨");
    } else {
        bar(g, RectF(x, y, w, 12.f), rec.exp / static_cast<float>(expToNext(rec.level)), kExpC,
            "EXP " + std::to_string(rec.exp) + " / " + std::to_string(expToNext(rec.level)));
    }
    y += 17.f;
    if (cfg::kAutoExpCapEnabled) {
        int cap = autoExpCap();
        bar(g, RectF(x, y, w, 12.f), rec.autoExp / static_cast<float>(cap), kAutoC,
            rec.autoExp >= cap ? "자동 경험치 상한 도달" : "자동 경험치 " + std::to_string(rec.autoExp) + " / " + std::to_string(cap));
        y += 20.f;
    } else {
        y += 3.f;
    }

    std::string state;
    if (game.screen() == Screen::Tower || game.screen() == Screen::FloorClear) {
        state = std::string("탑 ") + std::to_string(game.towerFloor()) + "층 · " + combatModeName(game.towerMode());
    } else if (game.hunting()) {
        state = std::string(combatModeName(game.huntMode())) + " · ";
        if (game.combatPaused()) state += "일시정지";
        else if (game.waveActive()) state += "전투 중";
        else state += "다음 무리 " + fmtInt(game.waveBreakLeft()) + "초";
    } else {
        state = std::string("대기 중 · 모드: ") + combatModeName(game.huntMode());
    }
    text(g, state, x, y, 13.f, kSub);
    y += 20.f;

    // 바탕화면 전투 토글과 모드 선택 (v0.5)
    bool inTower = game.screen() == Screen::Tower || game.screen() == Screen::FloorClear;
    bool on = game.hunting();
    toggle(g, out, ctx, RectF(x, y, w, 26.f), "바탕화면 전투", on,
           on ? UiAction::StopHunt : UiAction::StartHunt, 0, !inTower);
    y += 30.f;

    // 모드는 무리와 무리 사이에만 바꿀 수 있다(전투 도중 교체 방지)
    const CombatMode modes[3] = {CombatMode::Direct, CombatMode::SkillManual, CombatMode::Auto};
    const char* shortName[3] = {"직접", "스킬", "자동"};
    float bw = (w - 8.f) / 3.f;
    for (int i = 0; i < 3; ++i) {
        button(g, out, ctx, RectF(x + i * (bw + 4.f), y, bw, 24.f), shortName[i], UiAction::SetMode,
               static_cast<int>(modes[i]), game.canChangeMode(), game.huntMode() == modes[i]);
    }
    y += 28.f;

    // 드래곤을 화면 아래쪽 띠에만 둘지 (v0.6)
    toggle(g, out, ctx, RectF(x, y, w, 24.f), "아래쪽에만", game.confineToBand(), UiAction::ToggleConfine, 0, true);
    y += 28.f;

    // 스킬 칸 (v1.4: 장착한 4칸. 키 1~4와 짝이다)
    const float box = 40.f;
    for (int slot = 0; slot < kSkillSlots; ++slot) {
        RectF b(x + slot * (box + 6.f), y, box, box);
        drawSkillBox(g, sprites, sp, d, game.skillSlots()[static_cast<size_t>(slot)], b, rec.level,
                     std::to_string(slot + 1));
    }
    // 관리창 열기. 쓰지 않고 남은 스킬이 있으면 눈에 띄게 한다.
    // v1.4: 전에는 "스킬 ●" 였는데 ● 가 폰트에 없어 깨져 보였다(토글 스위치처럼 보인다는 제보).
    float hx = x + kSkillSlots * (box + 6.f) + 2.f;
    float hw = r.GetRight() - hx - 8.f;
    if (hw > 40.f) {
        // 탑에서는 열 수 없다(Game::openSkills 참고). 눌리지 않는다는 것을 보이게 둔다.
        button(g, out, ctx, RectF(hx, y, hw, 20.f), game.hasUnequippedSkill() ? "스킬 새것" : "스킬",
               UiAction::OpenSkills, 0, game.screen() == Screen::Desktop);
        const char* hint = nullptr;
        if (!ctx.inputEnabled) hint = "싸우기 시작하면 키보드를 받습니다";
        else if ((game.screen() == Screen::Tower ? game.towerMode() : game.huntMode()) == CombatMode::Direct)
            hint = "WASD 이동 · Space 회피 · 1~4 스킬";
        else if ((game.screen() == Screen::Tower ? game.towerMode() : game.huntMode()) == CombatMode::SkillManual)
            hint = "1~4 스킬 · 이동·회피는 자동";
        if (hint) paragraph(g, hint, RectF(hx, y + 22.f, hw, box - 20.f), 10.f, kSub);
    }
}

void Renderer::drawNotices(Graphics& g, const Game& game) {
    float y = 18.f;
    for (const Notice& n : game.notices()) {
        float a = 1.f - std::clamp((n.t - (n.duration - 0.6f)) / 0.6f, 0.f, 1.f);
        a *= std::clamp(n.t / 0.2f, 0.f, 1.f);
        RectF r(width_ * 0.5f - 330.f, y, 660.f, 36.f);
        fillRound(g, r, 10.f, Color(static_cast<BYTE>(215 * a), 20, 22, 32));
        strokeRound(g, r, 10.f, col(n.color, 0.6f * a), 1.5f);
        uint32_t c = (n.color & 0x00FFFFFF) | (static_cast<uint32_t>(255 * a) << 24);
        text(g, n.text, r.X + r.Width * 0.5f, r.Y + 8.f, 15.f, c, 1, true);
        y += 42.f;
    }
}

void Renderer::drawTowerHud(Graphics& g, const Game& game) {
    RectF a(game.arena().x, game.arena().y, game.arena().w, game.arena().h);
    text(g, "탑 " + std::to_string(game.towerFloor()) + "층 / " + std::to_string(Game::kTowerFloors), a.X + 16.f,
         a.Y - 34.f, 18.f, kText, 0, true, true);
    text(g, std::string(combatModeName(game.towerMode())) + " · 자동 사냥 불가", a.GetRight() - 16.f, a.Y - 30.f, 14.f,
         kSub, 2, false, true);
    for (const Enemy& e : game.world().enemies()) {
        if (enemyDef(e.kind).role != EnemyRole::Boss) continue;
        RectF br(a.X + a.Width * 0.2f, a.Y + 14.f, a.Width * 0.6f, 16.f);
        const std::string& bn = enemyDef(e.kind).name;
        bar(g, br, e.hp / e.maxHp, e.phase2 ? 0xFFFF4D4D : 0xFFE0703A, e.phase2 ? bn + " (분노)" : bn);
    }
}

// ---------------------------------------------------------------------------
// 큐 11번: 프레임 측정 표시 (기본 꺼짐, 트레이 메뉴로 켠다)
// ---------------------------------------------------------------------------
// 재기만 한다 — 어떤 그리기도 고치지 않는다. 숫자는 평균·최대(ms)다.
// 자리는 왼쪽 위 고정. 상태창(오른쪽)·알림(가운데 위)과 겹치지 않는 자리를 골랐다.
void Renderer::drawFrameStats(Graphics& g, const FrameStats& st) {
    struct Line {
        const char* label;
        FrameStage stage;
    };
    static const Line kLines[] = {
        {"지우기", FrameStage::Clear},
        {"스프라이트", FrameStage::World},
        {"UI", FrameStage::Ui},
        {"측정 표시", FrameStage::Debug},
        {"ULW(합성)", FrameStage::Present},
    };
    const float rowH = 20.f;
    const int rows = static_cast<int>(sizeof(kLines) / sizeof(kLines[0]));
    RectF r(16.f, 16.f, 268.f, 66.f + rowH * (rows + 1));
    panel(g, r, 232);
    markRegion(r);  // 클릭 통과 규칙: 우리가 그린 것 위에서는 마우스를 받는다

    float x0 = r.X + 14.f;
    float xAvg = r.GetRight() - 86.f;
    float xMax = r.GetRight() - 14.f;
    text(g, "프레임 측정", x0, r.Y + 10.f, 15.f, kWarnC, 0, true);

    char buf[128];
    if (st.hasAverage()) {
        std::snprintf(buf, sizeof(buf), "%.1f fps · %.2f ms", st.fps(), st.avgTotalMs());
    } else {
        std::snprintf(buf, sizeof(buf), "모으는 중 (%d 프레임)", st.window());
    }
    text(g, buf, xMax, r.Y + 12.f, 13.f, kSub, 2);

    float y = r.Y + 36.f;
    text(g, "단계", x0, y, 12.f, kSub, 0);
    text(g, "평균", xAvg, y, 12.f, kSub, 2);
    text(g, "최대", xMax, y, 12.f, kSub, 2);
    y += rowH;

    double total = st.avgTotalMs();
    for (const Line& ln : kLines) {
        FrameStats::Row row = st.row(ln.stage);
        // 전체의 40%를 넘게 먹는 단계를 눈에 띄게 한다 — "어디가 범인인가"가 이 표의 목적이다.
        bool hot = total > 0.0 && row.avgMs > total * 0.4;
        uint32_t c = hot ? kDanger : kText;
        text(g, ln.label, x0, y, 13.f, c, 0, hot);
        std::snprintf(buf, sizeof(buf), "%.2f", row.avgMs);
        text(g, buf, xAvg, y, 13.f, c, 2, hot);
        std::snprintf(buf, sizeof(buf), "%.2f", row.maxMs);
        text(g, buf, xMax, y, 13.f, kSub, 2);
        y += rowH;
    }

    std::snprintf(buf, sizeof(buf), "%.0f x %.0f · 프레임 %lld", width_, height_, st.totalFrames());
    text(g, buf, x0, y + 4.f, 12.f, kSub, 0);
}

}  // namespace dt::win
