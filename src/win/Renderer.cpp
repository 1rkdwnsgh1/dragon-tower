// 오버레이 한 프레임을 그리는 진입점. 실제 그리기는 화면별 파일에 있다(큐 10번에서 쪼갰다).
//   RendererCommon.cpp — 글자·창틀·버튼·막대 같은 공용 도구
//   RendererWorld.cpp  — 드래곤·적·효과
//   RendererHud.cpp    — 상태창·알림·탑 표시
//   RendererModal.cpp  — 메뉴·선택창·관리창·스킬창·도감
// 이미지가 없으면 임시 도형으로 대신한다.
#include "RendererInternal.h"

#include "FrameProfiler.h"

namespace dt::win {

bool Renderer::init() {
    family_ = std::make_unique<FontFamily>(L"Malgun Gothic");
    if (!family_->IsAvailable()) {
        family_.reset(FontFamily::GenericSansSerif()->Clone());
    }
    return family_ != nullptr;
}

void Renderer::shutdown() {
    fonts_.clear();
    family_.reset();
}

// 이번 프레임에 모은 영역. drawOverlay 동안만 유효하다.
static FrameRegions* g_regions = nullptr;
void markRegion(const RectF& r, float pad) {  // 기본값은 선언(RendererInternal.h)에만 둔다
    if (!g_regions) return;
    RectF padded(r.X - pad, r.Y - pad, r.Width + pad * 2.f, r.Height + pad * 2.f);
    g_regions->add(padded);
}

// ---------------------------------------------------------------------------
// 오버레이 전체
// ---------------------------------------------------------------------------

void Renderer::drawOverlay(Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                           std::vector<UiButton>& out, FrameRegions* outRegions) {
    // v0.9: 이번 프레임에 쓸 UI 스킨을 정한다(드래곤마다 다를 수 있다).
    sprites_ = &sprites;
    skin_ = game.uiSkin();
    g_regions = outRegions;
    if (outRegions) {
        outRegions->hit.clear();
        // 선택 화면·탑·모달이 떠 있으면 화면 전체에서 입력을 받는다(배경 클릭 방지).
        outRegions->fullScreen = game.screen() != Screen::Desktop;
    }
    struct RegionGuard {
        ~RegionGuard() { g_regions = nullptr; }
    } guard;
    out.clear();
    // 큐 11번: 프레임 측정. 꺼져 있으면 아래 stage() 는 분기 하나로 끝난다.
    // 켜져 있을 때만 단계마다 Flush 한다 — GDI+ 가 모아서 처리하면 단계별 숫자가 섞인다.
    FrameProfiler& prof = frameProfiler();
    auto stage = [&](FrameStage s) {
        if (!prof.enabled()) return;
        g.Flush(FlushIntentionSync);
        prof.mark(s);
    };
    width_ = game.desktopBounds().w;
    height_ = game.desktopBounds().h;
    Screen sc = game.screen();

    if (ctx.inputEnabled) {
        // 키보드를 받는 중이라는 표시로 화면을 아주 살짝 어둡게 한다(싸우는 중이고 자동이 아닐 때).
        SolidBrush tint(Color(30, 0, 0, 0));
        g.FillRectangle(&tint, 0.f, 0.f, width_, height_);
    }

    if (game.inTowerFlow()) {
        RectF a(game.arena().x, game.arena().y, game.arena().w, game.arena().h);
        fillRound(g, RectF(a.X - 6.f, a.Y - 6.f, a.Width + 12.f, a.Height + 12.f), 18.f, Color(150, 0, 0, 0));
        fillRound(g, a, 14.f, Color(225, 26, 22, 36));
        Pen grid(Color(40, 140, 120, 200), 1.f);
        for (float x = a.X + 60.f; x < a.GetRight(); x += 60.f) g.DrawLine(&grid, x, a.Y, x, a.GetBottom());
        for (float y = a.Y + 60.f; y < a.GetBottom(); y += 60.f) g.DrawLine(&grid, a.X, y, a.GetRight(), y);
        strokeRound(g, a, 14.f, Color(255, 150, 110, 220), 2.f);
    }

    if (game.hasDragon() || sc == Screen::Death) drawWorld(g, game, sprites, ctx);
    stage(FrameStage::World);  // 여기까지가 스프라이트(배경 띠·드래곤·적·효과)

    if (game.hasDragon()) drawStatusCard(g, game, sprites, ctx, out);
    if (sc == Screen::Tower) drawTowerHud(g, game);
    // v1.0: 메뉴는 키보드 조작과 상관없이 열려 있으면 그린다. 마우스로 언제든 쓸 수 있다.
    if (sc == Screen::Desktop && game.menuOpen() && game.hasDragon()) drawMenu(g, game, ctx, out);
    drawMotionMenu(g, game, sprites, ctx, out);

    drawNotices(g, game);

    if (game.combatPaused() && !game.pauseReason().empty()) {
        RectF r(width_ * 0.5f - 300.f, height_ * 0.5f - 30.f, 600.f, 60.f);
        if (game.inTowerFlow()) r.Y = game.arena().y + game.arena().h * 0.5f - 30.f;
        panel(g, r, 220);
        text(g, "일시정지", r.X + r.Width * 0.5f, r.Y + 6.f, 16.f, kWarnC, 1, true);
        text(g, game.pauseReason(), r.X + r.Width * 0.5f, r.Y + 30.f, 14.f, kText, 1);
    }

    drawModal(g, game, sprites, ctx, out);

    if (!ctx.inputEnabled && game.needsInput()) {
        RectF r(width_ * 0.5f - 280.f, 70.f, 560.f, 40.f);
        panel(g, r, 230);
        text(g, "창 안을 눌러 고르세요.", r.X + r.Width * 0.5f, r.Y + 10.f, 15.f, kWarnC, 1,
             true);
    }
    stage(FrameStage::Ui);  // 여기까지가 UI(상태창·알림·메뉴·모달)

    // 측정 표시는 맨 마지막에, 자기 비용을 따로 떼어 센다.
    if (prof.enabled()) {
        drawFrameStats(g, prof.stats());
        stage(FrameStage::Debug);
    }
}

}  // namespace dt::win
