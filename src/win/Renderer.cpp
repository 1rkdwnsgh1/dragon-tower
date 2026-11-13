#include "Renderer.h"

#include <cmath>
#include <cstdio>

#include "Platform.h"
#include "core/Config.h"
#include "core/Progression.h"

using namespace Gdiplus;

namespace dt::win {

// dt::RectF(게임 좌표)와 구분: 이 파일의 RectF는 GDI+ 사각형이다.
using RectF = Gdiplus::RectF;

namespace {

Color col(uint32_t argb, float alphaMul = 1.f) {
    int a = static_cast<int>(((argb >> 24) & 0xFF) * alphaMul);
    a = std::clamp(a, 0, 255);
    return Color(static_cast<BYTE>(a), static_cast<BYTE>((argb >> 16) & 0xFF), static_cast<BYTE>((argb >> 8) & 0xFF),
                 static_cast<BYTE>(argb & 0xFF));
}

uint32_t lighten(uint32_t argb, float t) {
    auto ch = [&](int shift) {
        int v = (argb >> shift) & 0xFF;
        return static_cast<uint32_t>(std::clamp(static_cast<int>(v + (255 - v) * t), 0, 255));
    };
    return (argb & 0xFF000000) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

uint32_t darken(uint32_t argb, float t) {
    auto ch = [&](int shift) {
        int v = (argb >> shift) & 0xFF;
        return static_cast<uint32_t>(std::clamp(static_cast<int>(v * (1.f - t)), 0, 255));
    };
    return (argb & 0xFF000000) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

void roundRect(GraphicsPath& p, const RectF& r, float rad) {
    float d = std::min(rad * 2.f, std::min(r.Width, r.Height));
    if (d <= 1.f) {
        p.AddRectangle(r);
        return;
    }
    p.AddArc(r.X, r.Y, d, d, 180.f, 90.f);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270.f, 90.f);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.f, 90.f);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90.f, 90.f);
    p.CloseFigure();
}

void fillRound(Graphics& g, const RectF& r, float rad, const Color& c) {
    GraphicsPath p;
    roundRect(p, r, rad);
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

void strokeRound(Graphics& g, const RectF& r, float rad, const Color& c, float w) {
    GraphicsPath p;
    roundRect(p, r, rad);
    Pen pen(c, w);
    g.DrawPath(&pen, &p);
}

void fillCircle(Graphics& g, float cx, float cy, float r, const Color& c) {
    SolidBrush b(c);
    g.FillEllipse(&b, cx - r, cy - r, r * 2.f, r * 2.f);
}

void strokeCircle(Graphics& g, float cx, float cy, float r, const Color& c, float w) {
    Pen pen(c, w);
    g.DrawEllipse(&pen, cx - r, cy - r, r * 2.f, r * 2.f);
}

std::string fmtInt(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::ceil(v)));
    return buf;
}

std::string fmtPct(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(v * 100.f + 0.5f));
    return buf;
}

std::string dragonName(const DragonRecord& d) {
    return std::string(speciesDef(d.species).name) + " #" + std::to_string(d.id);
}

const uint32_t kText = 0xFFF2F4F8;
const uint32_t kSub = 0xFFAAB2C4;
const uint32_t kWarnC = 0xFFFFD166;
const uint32_t kDanger = 0xFFFF7A7A;
const uint32_t kHpC = 0xFF5BD46B;
const uint32_t kExpC = 0xFF5AA9FF;
const uint32_t kAutoC = 0xFFB98CFF;

}  // namespace

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

Font* Renderer::font(float size, bool bold) {
    int key = static_cast<int>(size * 10.f) * 2 + (bold ? 1 : 0);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second.get();
    auto f = std::make_unique<Font>(family_.get(), size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    Font* raw = f.get();
    fonts_[key] = std::move(f);
    return raw;
}

void Renderer::text(Graphics& g, const std::string& s, float x, float y, float size, uint32_t color, int align,
                    bool bold, bool strongShadow) {
    if (s.empty()) return;
    std::wstring w = widen(s);
    StringFormat fmt;
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    fmt.SetAlignment(align == 0 ? StringAlignmentNear : (align == 1 ? StringAlignmentCenter : StringAlignmentFar));
    const float span = 4000.f;
    RectF r(align == 0 ? x : (align == 1 ? x - span * 0.5f : x - span), y, span, size * 1.8f);
    Font* f = font(size, bold);
    float alpha = ((color >> 24) & 0xFF) / 255.f;
    SolidBrush shadow(Color(static_cast<BYTE>(200 * alpha), 0, 0, 0));
    if (strongShadow) {
        const float o[4][2] = {{-1.5f, 0.f}, {1.5f, 0.f}, {0.f, -1.5f}, {0.f, 1.5f}};
        for (auto& d : o) {
            RectF rr(r.X + d[0], r.Y + d[1], r.Width, r.Height);
            g.DrawString(w.c_str(), static_cast<INT>(w.size()), f, rr, &fmt, &shadow);
        }
    } else {
        RectF rr(r.X + 1.f, r.Y + 1.f, r.Width, r.Height);
        g.DrawString(w.c_str(), static_cast<INT>(w.size()), f, rr, &fmt, &shadow);
    }
    SolidBrush b(col(color));
    g.DrawString(w.c_str(), static_cast<INT>(w.size()), f, r, &fmt, &b);
}

void Renderer::paragraph(Graphics& g, const std::string& s, const RectF& r, float size, uint32_t color, int align,
                         bool bold) {
    if (s.empty()) return;
    std::wstring w = widen(s);
    StringFormat fmt;
    fmt.SetAlignment(align == 0 ? StringAlignmentNear : (align == 1 ? StringAlignmentCenter : StringAlignmentFar));
    SolidBrush b(col(color));
    g.DrawString(w.c_str(), static_cast<INT>(w.size()), font(size, bold), r, &fmt, &b);
}

void Renderer::panel(Graphics& g, const RectF& r, uint8_t alpha) {
    fillRound(g, RectF(r.X + 3.f, r.Y + 5.f, r.Width, r.Height), 14.f, Color(static_cast<BYTE>(alpha / 3), 0, 0, 0));
    fillRound(g, r, 14.f, Color(alpha, 24, 26, 38));
    strokeRound(g, r, 14.f, Color(255, 86, 96, 140), 1.5f);
}

void Renderer::button(Graphics& g, std::vector<UiButton>& out, const RenderContext& ctx, const RectF& r,
                      const std::string& label, UiAction action, int arg, bool enabled, bool selected, bool danger) {
    bool hover = enabled && r.Contains(PointF(ctx.mouseX, ctx.mouseY));
    Color fill = !enabled ? Color(200, 48, 50, 60)
                 : selected ? Color(255, 70, 128, 214)
                 : danger ? (hover ? Color(255, 170, 70, 70) : Color(255, 132, 52, 56))
                          : (hover ? Color(255, 80, 92, 134) : Color(255, 56, 64, 94));
    fillRound(g, r, 8.f, fill);
    if (selected) strokeRound(g, r, 8.f, Color(255, 170, 210, 255), 2.f);
    else if (hover) strokeRound(g, r, 8.f, Color(255, 150, 160, 200), 1.5f);
    float size = r.Height >= 40.f ? 16.f : 14.f;
    text(g, label, r.X + r.Width * 0.5f, r.Y + (r.Height - size * 1.35f) * 0.5f, size, enabled ? kText : 0xFF7C8090, 1,
         true);
    UiButton b;
    b.rect = r;
    b.action = action;
    b.arg = arg;
    b.enabled = enabled;
    out.push_back(b);
}

void Renderer::bar(Graphics& g, const RectF& r, float ratio, uint32_t color, const std::string& label) {
    ratio = std::clamp(ratio, 0.f, 1.f);
    fillRound(g, r, r.Height * 0.5f, Color(220, 12, 14, 20));
    if (ratio > 0.f) {
        RectF f(r.X, r.Y, std::max(r.Height, r.Width * ratio), r.Height);
        fillRound(g, f, r.Height * 0.5f, col(color));
    }
    strokeRound(g, r, r.Height * 0.5f, Color(160, 0, 0, 0), 1.f);
    if (!label.empty()) {
        float size = std::max(10.f, r.Height - 3.f);
        text(g, label, r.X + r.Width * 0.5f, r.Y + (r.Height - size * 1.35f) * 0.5f, size, kText, 1, true);
    }
}

// ---------------------------------------------------------------------------
// 오버레이 전체
// ---------------------------------------------------------------------------

void Renderer::drawOverlay(Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                           std::vector<UiButton>& out) {
    out.clear();
    width_ = game.desktopBounds().w;
    height_ = game.desktopBounds().h;
    Screen sc = game.screen();

    if (ctx.inputEnabled) {
        // 게임 입력이 켜졌다는 표시로 화면을 아주 살짝 어둡게 한다(클릭도 게임이 받는다).
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
    if (game.hasDragon()) drawStatusCard(g, game, sprites, ctx);
    if (sc == Screen::Tower) drawTowerHud(g, game);
    if (sc == Screen::Desktop && ctx.inputEnabled && game.hasDragon()) drawMenu(g, game, ctx, out);

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
        text(g, "오른쪽 아래 설정 버튼을 눌러 게임 입력을 켜 주세요.", r.X + r.Width * 0.5f, r.Y + 10.f, 15.f, kWarnC, 1,
             true);
    }
}

// ---------------------------------------------------------------------------
// 월드
// ---------------------------------------------------------------------------

void Renderer::drawSprite(Graphics& g, Bitmap* bmp, float cx, float cy, float height, bool flip, float flash,
                          float alpha) {
    float bw = static_cast<float>(bmp->GetWidth());
    float bh = static_cast<float>(bmp->GetHeight());
    float h = height;
    float w = h * bw / bh;
    GraphicsState st = g.Save();
    g.TranslateTransform(cx, cy);
    if (flip) g.ScaleTransform(-1.f, 1.f);
    ColorMatrix cm = {{{1.f, 0.f, 0.f, 0.f, 0.f},
                       {0.f, 1.f, 0.f, 0.f, 0.f},
                       {0.f, 0.f, 1.f, 0.f, 0.f},
                       {0.f, 0.f, 0.f, alpha, 0.f},
                       {flash, flash, flash, 0.f, 1.f}}};
    ImageAttributes ia;
    ia.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    g.DrawImage(bmp, RectF(-w * 0.5f, -h * 0.5f, w, h), 0.f, 0.f, bw, bh, UnitPixel, &ia);
    g.Restore(st);
}

void Renderer::drawSheetFrame(Graphics& g, const Sheet& sh, int frame, float cx, float cy, float height, bool flip,
                              float flash, float alpha) {
    if (!sh.bmp || sh.frames <= 0) return;
    int f = std::clamp(frame, 0, sh.frames - 1);
    float fw = static_cast<float>(sh.frameW);
    float fh = static_cast<float>(sh.frameH);
    float h = height;
    float w = h * fw / fh;
    GraphicsState st = g.Save();
    g.TranslateTransform(cx, cy);
    if (flip) g.ScaleTransform(-1.f, 1.f);
    ColorMatrix cm = {{{1.f, 0.f, 0.f, 0.f, 0.f},
                       {0.f, 1.f, 0.f, 0.f, 0.f},
                       {0.f, 0.f, 1.f, 0.f, 0.f},
                       {0.f, 0.f, 0.f, alpha, 0.f},
                       {flash, flash, flash, 0.f, 1.f}}};
    ImageAttributes ia;
    ia.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    ia.SetWrapMode(WrapModeTileFlipXY);  // 칸 경계에서 옆 칸 픽셀이 번지지 않게 한다
    g.DrawImage(sh.bmp, RectF(-w * 0.5f, -h * 0.5f, w, h), fw * f, 0.f, fw, fh, UnitPixel, &ia);
    g.Restore(st);
}

void Renderer::drawDragon(Graphics& g, const DragonActor& d, const Sprites& sprites, float time, float x, float y,
                          float height, bool preview) {
    const SpeciesDef& sp = speciesDef(d.species);
    bool flip = sp.flipAllowed && d.facing.x < 0.f;
    float alpha = (!preview && d.invuln > 0.f) ? 0.55f : 1.f;
    float flash = (!preview && d.hitFlash > 0.f) ? 0.6f : 0.f;

    // 시트 애니메이션이 있으면 그것으로 그린다(v0.2). 미리보기는 항상 대기 동작.
    AnimState state = preview ? AnimState::Idle : d.anim;
    Sheet sh = sprites.sheet(sp.key, state);
    bool animated = sh.bmp != nullptr && sh.frames > 1;

    // 시트가 여러 칸이면 그림 자체가 움직이므로 위아래 흔들기를 하지 않는다.
    float bob = animated ? 0.f : std::sin(time * (d.moving ? 9.f : 3.f)) * (d.moving ? 4.f : 2.5f);
    float cy = y + bob;

    // 그림자. 날고 있으면 작고 옅게 그려 공중에 떠 있는 것을 알린다.
    {
        float shrink = (!preview && d.flying) ? 0.6f : 1.f;
        BYTE a = static_cast<BYTE>(70 * alpha * (shrink < 1.f ? 0.55f : 1.f));
        float w = height * 0.64f * shrink;
        SolidBrush sh2(Color(a, 0, 0, 0));
        g.FillEllipse(&sh2, x - w * 0.5f, y + height * 0.38f, w, height * 0.14f * shrink);
    }

    if (sh.bmp) {
        const AnimDef& ad = sp.anims[static_cast<int>(state)];
        float t = preview ? time : d.animTime;
        // render_scale 로 이 상태만 크게/작게 그린다. 칸 안의 바닥 기준선(240/256)이
        // 제자리에 남도록 그만큼 위로 올려 그려, 발 위치가 흔들리지 않게 한다.
        const float kAnchorFrac = 240.f / 256.f;
        float k = ad.renderScale;
        float drawH = height * k;
        float cyAdj = cy - (kAnchorFrac - 0.5f) * height * (k - 1.f);
        drawSheetFrame(g, sh, animFrame(t, ad.fps, sh.frames, ad.loop), x, cyAdj, drawH, flip, flash, alpha);
        return;
    }

    Bitmap* bmp = sprites.get(std::string("dragon_") + sp.key);
    if (bmp) {
        drawSprite(g, bmp, x, cy, height, flip, flash, alpha);
        return;
    }

    // 임시 도형 드래곤
    float s = flip ? -1.f : 1.f;
    float h = height;
    uint32_t body = flash > 0.f ? lighten(sp.color, 0.6f) : sp.color;
    Color bodyC = col(body, alpha);
    Color wingC = col(lighten(body, 0.35f), alpha * 0.95f);
    Color darkC = col(darken(body, 0.35f), alpha);
    float flap = std::sin(time * 8.f) * h * 0.08f;

    PointF tail[3] = {PointF(x - s * h * 0.30f, cy + h * 0.05f), PointF(x - s * h * 0.62f, cy - h * 0.05f),
                      PointF(x - s * h * 0.28f, cy + h * 0.20f)};
    SolidBrush darkB(darkC);
    g.FillPolygon(&darkB, tail, 3);

    PointF wingBack[3] = {PointF(x - s * h * 0.02f, cy - h * 0.06f), PointF(x - s * h * 0.30f, cy - h * 0.46f + flap),
                          PointF(x - s * h * 0.20f, cy + h * 0.04f)};
    SolidBrush wingB(wingC);
    g.FillPolygon(&wingB, wingBack, 3);

    SolidBrush bodyB(bodyC);
    g.FillEllipse(&bodyB, x - h * 0.36f, cy - h * 0.14f, h * 0.72f, h * 0.46f);
    // 다리
    g.FillEllipse(&darkB, x - h * 0.22f, cy + h * 0.22f, h * 0.14f, h * 0.16f);
    g.FillEllipse(&darkB, x + h * 0.08f, cy + h * 0.22f, h * 0.14f, h * 0.16f);
    // 목과 머리
    PointF neck[4] = {PointF(x + s * h * 0.12f, cy - h * 0.04f), PointF(x + s * h * 0.30f, cy - h * 0.26f),
                      PointF(x + s * h * 0.40f, cy - h * 0.18f), PointF(x + s * h * 0.26f, cy + h * 0.06f)};
    g.FillPolygon(&bodyB, neck, 4);
    float hx = x + s * h * 0.40f;
    float hy = cy - h * 0.26f;
    g.FillEllipse(&bodyB, hx - h * 0.15f, hy - h * 0.12f, h * 0.30f, h * 0.22f);
    PointF horn[3] = {PointF(hx - s * h * 0.06f, hy - h * 0.08f), PointF(hx - s * h * 0.16f, hy - h * 0.26f),
                      PointF(hx + s * h * 0.02f, hy - h * 0.10f)};
    SolidBrush hornB(col(0xFFF4E3C0, alpha));
    g.FillPolygon(&hornB, horn, 3);
    fillCircle(g, hx + s * h * 0.05f, hy - h * 0.02f, h * 0.045f, col(0xFFFFFFFF, alpha));
    fillCircle(g, hx + s * h * 0.065f, hy - h * 0.02f, h * 0.022f, col(0xFF101018, alpha));
    // 앞쪽 날개
    PointF wingFront[3] = {PointF(x + s * h * 0.02f, cy - h * 0.04f), PointF(x - s * h * 0.16f, cy - h * 0.52f - flap),
                           PointF(x - s * h * 0.10f, cy + h * 0.06f)};
    g.FillPolygon(&wingB, wingFront, 3);
    Pen outline(col(0xFF000000, 0.35f * alpha), 1.5f);
    g.DrawEllipse(&outline, x - h * 0.36f, cy - h * 0.14f, h * 0.72f, h * 0.46f);
}

void Renderer::drawEnemy(Graphics& g, const Enemy& e, const Sprites& sprites, float time) {
    bool flip = e.facing < 0.f;
    float flash = e.hitFlash > 0.f ? 0.7f : 0.f;
    float bob = std::sin(time * 6.f + e.id) * 2.f;
    float x = e.pos.x;
    float y = e.pos.y + bob;
    float height = e.kind == EnemyKind::Imp ? e.radius * 2.6f : e.radius * 2.6f;

    SolidBrush sh(Color(70, 0, 0, 0));
    g.FillEllipse(&sh, x - e.radius, e.pos.y + e.radius * 0.9f, e.radius * 2.f, e.radius * 0.5f);

    if (e.kind == EnemyKind::Guardian && e.phase2) {
        float pulse = 0.5f + 0.5f * std::sin(time * 6.f);
        fillCircle(g, x, y, e.radius * 1.45f, Color(static_cast<BYTE>(40 + 40 * pulse), 255, 60, 40));
    }
    if (e.castTimer > 0.f) {
        strokeCircle(g, x, y, e.radius * 1.25f, Color(200, 255, 90, 60), 2.f);
    }

    Bitmap* bmp = sprites.get(e.kind == EnemyKind::Imp ? "enemy_imp" : "boss_guardian");
    if (bmp) {
        drawSprite(g, bmp, x, y, height, flip, flash, 1.f);
    } else if (e.kind == EnemyKind::Imp) {
        float r = e.radius;
        uint32_t c = flash > 0.f ? 0xFFF0E0FF : 0xFF8E5BD8;
        SolidBrush hornB(col(0xFF4A2B78));
        PointF h1[3] = {PointF(x - r * 0.7f, y - r * 0.5f), PointF(x - r * 0.9f, y - r * 1.35f), PointF(x - r * 0.25f, y - r * 0.8f)};
        PointF h2[3] = {PointF(x + r * 0.7f, y - r * 0.5f), PointF(x + r * 0.9f, y - r * 1.35f), PointF(x + r * 0.25f, y - r * 0.8f)};
        g.FillPolygon(&hornB, h1, 3);
        g.FillPolygon(&hornB, h2, 3);
        fillCircle(g, x, y, r, col(c));
        strokeCircle(g, x, y, r, Color(120, 0, 0, 0), 1.5f);
        float s = flip ? -1.f : 1.f;
        fillCircle(g, x + s * r * 0.15f - r * 0.3f, y - r * 0.15f, r * 0.17f, col(0xFFFFE34D));
        fillCircle(g, x + s * r * 0.15f + r * 0.3f, y - r * 0.15f, r * 0.17f, col(0xFFFFE34D));
        Pen mouth(Color(255, 40, 10, 60), 2.f);
        g.DrawArc(&mouth, x - r * 0.35f + s * r * 0.15f, y + r * 0.05f, r * 0.7f, r * 0.45f, 20.f, 140.f);
    } else {
        float r = e.radius;
        uint32_t c = flash > 0.f ? 0xFFFFD0D0 : 0xFF7A1F2B;
        fillRound(g, RectF(x - r, y - r * 0.9f, r * 2.f, r * 1.9f), r * 0.5f, col(c));
        strokeRound(g, RectF(x - r, y - r * 0.9f, r * 2.f, r * 1.9f), r * 0.5f, Color(160, 0, 0, 0), 2.f);
        SolidBrush gold(col(0xFFF2C14E));
        for (int i = 0; i < 5; ++i) {
            float bx = x - r * 0.8f + i * r * 0.4f;
            PointF spike[3] = {PointF(bx - r * 0.14f, y - r * 0.85f), PointF(bx, y - r * 1.35f), PointF(bx + r * 0.14f, y - r * 0.85f)};
            g.FillPolygon(&gold, spike, 3);
        }
        uint32_t eye = e.phase2 ? 0xFFFF4040 : 0xFFFFE070;
        fillCircle(g, x - r * 0.35f, y - r * 0.2f, r * 0.14f, col(eye));
        fillCircle(g, x + r * 0.35f, y - r * 0.2f, r * 0.14f, col(eye));
        SolidBrush armor(col(darken(c, 0.4f)));
        g.FillRectangle(&armor, x - r * 0.6f, y + r * 0.25f, r * 1.2f, r * 0.18f);
    }

    if (e.stun > 0.f) {
        for (int i = 0; i < 3; ++i) {
            float a = time * 4.f + i * 2.094f;
            fillCircle(g, x + std::cos(a) * e.radius * 0.7f, y - e.radius * 1.2f + std::sin(a) * 5.f, 4.f,
                       Color(255, 255, 240, 120));
        }
    }

    // 체력 바(보스는 탑 HUD에 크게 표시)
    if (e.kind == EnemyKind::Imp) {
        float w = 56.f;
        bar(g, RectF(x - w * 0.5f, e.pos.y - e.radius - 18.f, w, 7.f), e.hp / e.maxHp, 0xFFFF6B6B);
        text(g, "Lv." + std::to_string(e.level), x, e.pos.y - e.radius - 36.f, 12.f, 0xFFE0D0FF, 1, false, true);
    }
}

void Renderer::drawWorld(Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx) {
    const World& w = game.world();
    float time = ctx.time;
    uint32_t spColor = speciesDef(w.dragon().species).color;

    // 지속 피해 장판
    for (const DotZone& z : w.dots()) {
        float life = std::clamp(z.remaining / std::max(0.01f, z.duration), 0.f, 1.f);
        fillCircle(g, z.pos.x, z.pos.y, z.radius, col(spColor, 0.22f * (0.4f + 0.6f * life)));
        GraphicsState st = g.Save();
        g.TranslateTransform(z.pos.x, z.pos.y);
        g.RotateTransform(time * 220.f);
        Pen p(col(lighten(spColor, 0.3f), 0.8f * life + 0.2f), 3.f);
        p.SetStartCap(LineCapRound);
        p.SetEndCap(LineCapRound);
        for (int i = 0; i < 3; ++i) {
            g.DrawArc(&p, -z.radius * 0.8f, -z.radius * 0.8f, z.radius * 1.6f, z.radius * 1.6f, i * 120.f, 70.f);
            g.DrawArc(&p, -z.radius * 0.45f, -z.radius * 0.45f, z.radius * 0.9f, z.radius * 0.9f, i * 120.f + 60.f, 60.f);
        }
        g.Restore(st);
    }

    // 적 공격 예고
    for (const AttackZone& z : w.zones()) {
        float p = std::clamp(z.t / std::max(0.01f, z.windup), 0.f, 1.f);
        BYTE fillA = static_cast<BYTE>(35 + 70 * p);
        if (z.shape == ZoneShape::Circle) {
            fillCircle(g, z.a.x, z.a.y, z.radius, Color(fillA, 255, 60, 40));
            fillCircle(g, z.a.x, z.a.y, z.radius * p, Color(80, 255, 40, 20));
            strokeCircle(g, z.a.x, z.a.y, z.radius, Color(230, 255, 80, 60), 2.f);
        } else {
            Vec2 dir = (z.b - z.a).normalized();
            Vec2 n{-dir.y * z.radius, dir.x * z.radius};
            PointF pts[4] = {PointF(z.a.x + n.x, z.a.y + n.y), PointF(z.b.x + n.x, z.b.y + n.y),
                             PointF(z.b.x - n.x, z.b.y - n.y), PointF(z.a.x - n.x, z.a.y - n.y)};
            SolidBrush b(Color(fillA, 255, 60, 40));
            g.FillPolygon(&b, pts, 4);
            Vec2 mid = z.a + (z.b - z.a) * p;
            PointF inner[4] = {PointF(z.a.x + n.x, z.a.y + n.y), PointF(mid.x + n.x, mid.y + n.y),
                               PointF(mid.x - n.x, mid.y - n.y), PointF(z.a.x - n.x, z.a.y - n.y)};
            SolidBrush ib(Color(80, 255, 40, 20));
            g.FillPolygon(&ib, inner, 4);
            Pen pen(Color(230, 255, 80, 60), 2.f);
            g.DrawPolygon(&pen, pts, 4);
        }
    }

    for (const Enemy& e : w.enemies()) drawEnemy(g, e, sprites, time);

    const DragonActor& d = w.dragon();
    if (d.alive && game.hasDragon()) {
        if (d.shieldTime > 0.f) {
            float pulse = 0.5f + 0.5f * std::sin(time * 5.f);
            fillCircle(g, d.pos.x, d.pos.y, 58.f, Color(static_cast<BYTE>(40 + 30 * pulse), 140, 200, 255));
            strokeCircle(g, d.pos.x, d.pos.y, 58.f, Color(220, 160, 215, 255), 2.5f);
        }
        if (d.hasteTime > 0.f) {
            Pen streak(col(spColor, 0.6f), 2.f);
            for (int i = 0; i < 3; ++i) {
                float yy = d.pos.y - 14.f + i * 14.f;
                float sx = d.facing.x >= 0.f ? -1.f : 1.f;
                g.DrawLine(&streak, d.pos.x + sx * 40.f, yy, d.pos.x + sx * (70.f + 10.f * std::sin(time * 20.f + i)), yy);
            }
        }
        drawDragon(g, d, sprites, time, d.pos.x, d.pos.y, cfg::kDragonRadius * 3.f, false);
        float bw = 74.f;
        bar(g, RectF(d.pos.x - bw * 0.5f, d.pos.y - cfg::kDragonRadius * 1.5f - 22.f, bw, 8.f), d.hp / d.stats.maxHp,
            kHpC);
        text(g, "Lv." + std::to_string(game.dragonRecord().level), d.pos.x, d.pos.y - cfg::kDragonRadius * 1.5f - 42.f,
             13.f, 0xFFFFF3C4, 1, true, true);
    }

    // 투사체
    for (const Projectile& p : w.projectiles()) {
        float r = p.big ? 11.f : 6.f;
        fillCircle(g, p.pos.x, p.pos.y, r * 2.f, col(spColor, 0.25f));
        fillCircle(g, p.pos.x, p.pos.y, r, col(lighten(spColor, 0.3f)));
        fillCircle(g, p.pos.x, p.pos.y, r * 0.45f, Color(255, 255, 255, 255));
    }

    drawFx(g, w);

    for (const FloatText& t : w.texts()) {
        float a = 1.f - std::clamp((t.t - t.duration * 0.6f) / (t.duration * 0.4f), 0.f, 1.f);
        uint32_t c = (t.color & 0x00FFFFFF) | (static_cast<uint32_t>(255 * a) << 24);
        text(g, t.text, t.pos.x, t.pos.y, t.size, c, 1, true, true);
    }
}

void Renderer::drawFx(Graphics& g, const World& w) {
    for (const Fx& f : w.fx()) {
        float p = std::clamp(f.t / std::max(0.01f, f.duration), 0.f, 1.f);
        float fade = 1.f - p;
        switch (f.type) {
            case FxType::Ring: {
                float r = f.radius * (0.6f + 0.4f * p);
                fillCircle(g, f.a.x, f.a.y, r, col(f.color, 0.25f * fade));
                strokeCircle(g, f.a.x, f.a.y, r, col(f.color, fade), 4.f * fade + 1.f);
                break;
            }
            case FxType::LineBlast: {
                Pen pen(col(f.color, 0.7f * fade), f.radius * 2.f * (0.7f + 0.3f * fade));
                pen.SetStartCap(LineCapRound);
                pen.SetEndCap(LineCapRound);
                g.DrawLine(&pen, f.a.x, f.a.y, f.b.x, f.b.y);
                Pen core(Color(static_cast<BYTE>(200 * fade), 255, 255, 255), f.radius * 0.5f);
                g.DrawLine(&core, f.a.x, f.a.y, f.b.x, f.b.y);
                break;
            }
            case FxType::Dash: {
                Pen pen(col(f.color, 0.5f * fade), f.radius * 1.2f * fade + 2.f);
                pen.SetStartCap(LineCapRound);
                pen.SetEndCap(LineCapRound);
                g.DrawLine(&pen, f.a.x, f.a.y, f.b.x, f.b.y);
                break;
            }
            case FxType::Heal: {
                for (int i = 0; i < 4; ++i) {
                    float ox = std::cos(i * 1.57f + 0.4f) * f.radius * 0.6f;
                    float oy = -p * 50.f - i * 6.f;
                    SolidBrush b(col(f.color, fade));
                    g.FillRectangle(&b, f.a.x + ox - 2.f, f.a.y + oy - 8.f, 4.f, 16.f);
                    g.FillRectangle(&b, f.a.x + ox - 8.f, f.a.y + oy - 2.f, 16.f, 4.f);
                }
                strokeCircle(g, f.a.x, f.a.y, f.radius * (0.7f + 0.3f * p), col(f.color, 0.6f * fade), 3.f);
                break;
            }
            case FxType::Dodge: {
                Pen pen(col(f.color, fade), 3.f);
                float r = f.radius * (0.8f + 0.5f * p);
                g.DrawArc(&pen, f.a.x - r, f.a.y - r, r * 2.f, r * 2.f, 200.f, 140.f);
                g.DrawArc(&pen, f.a.x - r * 0.8f, f.a.y - r * 0.8f, r * 1.6f, r * 1.6f, 20.f, 140.f);
                break;
            }
            case FxType::Hit: {
                Pen pen(col(f.color, fade), 3.f);
                for (int i = 0; i < 6; ++i) {
                    float a = i * 1.047f + 0.3f;
                    float r1 = f.radius * 0.3f + f.radius * 0.4f * p;
                    float r2 = r1 + f.radius * 0.35f;
                    g.DrawLine(&pen, f.a.x + std::cos(a) * r1, f.a.y + std::sin(a) * r1, f.a.x + std::cos(a) * r2,
                               f.a.y + std::sin(a) * r2);
                }
                break;
            }
            case FxType::Poof: {
                fillCircle(g, f.a.x, f.a.y, f.radius * (0.5f + 0.6f * p), col(f.color, 0.35f * fade));
                break;
            }
            case FxType::Death: {
                fillCircle(g, f.a.x, f.a.y, f.radius * (0.4f + 0.8f * p), col(f.color, 0.4f * fade));
                strokeCircle(g, f.a.x, f.a.y, f.radius * (0.4f + 0.8f * p), col(0xFFFFFFFF, fade), 2.f);
                break;
            }
            case FxType::LevelUp: {
                for (int i = 0; i < 3; ++i) {
                    float q = std::clamp(p * 1.3f - i * 0.15f, 0.f, 1.f);
                    strokeCircle(g, f.a.x, f.a.y, f.radius * (0.3f + q), col(f.color, (1.f - q)), 3.f);
                }
                break;
            }
            case FxType::Shield: {
                strokeCircle(g, f.a.x, f.a.y, f.radius * (1.4f - 0.4f * p), col(f.color, fade), 4.f);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------

void Renderer::drawStatusCard(Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx) {
    const DragonRecord& rec = game.dragonRecord();
    const DragonActor& d = game.world().dragon();
    const SpeciesDef& sp = speciesDef(rec.species);
    RectF r(width_ - 96.f - 270.f, height_ - 16.f - 186.f, 270.f, 186.f);
    panel(g, r, 224);
    float x = r.X + 14.f;
    float y = r.Y + 10.f;
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
    int cap = autoExpCap();
    bar(g, RectF(x, y, w, 12.f), rec.autoExp / static_cast<float>(cap), kAutoC,
        rec.autoExp >= cap ? "자동 경험치 상한 도달" : "자동 경험치 " + std::to_string(rec.autoExp) + " / " + std::to_string(cap));
    y += 20.f;

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
    y += 22.f;

    // 스킬 칸
    float box = 40.f;
    for (int i = 0; i < 3; ++i) {
        const SkillDef& s = sp.skills[i];
        RectF b(x + i * (box + 8.f), y, box, box);
        bool unlocked = rec.level >= s.unlockLevel;
        fillRound(g, b, 6.f, unlocked ? col(darken(sp.color, 0.45f)) : Color(255, 40, 42, 52));
        Bitmap* icon = sprites.get(std::string("skill_") + sp.key + "_" + std::to_string(i + 1));
        if (icon && unlocked) {
            g.DrawImage(icon, RectF(b.X + 3.f, b.Y + 3.f, b.Width - 6.f, b.Height - 6.f));
        } else if (unlocked) {
            std::string first = s.name;
            // 첫 글자(UTF-8 한 글자)를 표시
            size_t len = 1;
            unsigned char c0 = static_cast<unsigned char>(first[0]);
            if (c0 >= 0xF0) len = 4; else if (c0 >= 0xE0) len = 3; else if (c0 >= 0xC0) len = 2;
            text(g, first.substr(0, len), b.X + box * 0.5f, b.Y + 8.f, 17.f, kText, 1, true);
        }
        if (!unlocked) {
            text(g, "Lv." + std::to_string(s.unlockLevel), b.X + box * 0.5f, b.Y + 12.f, 12.f, 0xFF8A8FA0, 1, true);
        } else if (d.skillCd[i] > 0.f) {
            float ratio = d.skillCd[i] / s.cooldown;
            fillRound(g, RectF(b.X, b.Y + box * (1.f - ratio), box, box * ratio), 6.f, Color(170, 0, 0, 0));
            text(g, fmtInt(d.skillCd[i]), b.X + box * 0.5f, b.Y + 11.f, 14.f, kText, 1, true, true);
        }
        strokeRound(g, b, 6.f, Color(200, 120, 130, 170), 1.f);
        text(g, std::to_string(i + 1), b.X + 3.f, b.Y + box - 15.f, 11.f, 0xFFFFE9A8, 0, true, true);
    }
    float hx = x + 3 * (box + 8.f) + 4.f;
    if (!ctx.inputEnabled) {
        paragraph(g, "설정 버튼으로\n게임 입력 켜기", RectF(hx, y + 2.f, r.GetRight() - hx - 8.f, box), 11.f, kSub);
    } else if ((game.screen() == Screen::Tower ? game.towerMode() : game.huntMode()) == CombatMode::Direct) {
        paragraph(g, "WASD 이동\nSpace 회피 · 1~3 스킬", RectF(hx, y + 2.f, r.GetRight() - hx - 8.f, box), 11.f, kSub);
    } else if ((game.screen() == Screen::Tower ? game.towerMode() : game.huntMode()) == CombatMode::SkillManual) {
        paragraph(g, "1~3 스킬\n이동·회피는 자동", RectF(hx, y + 2.f, r.GetRight() - hx - 8.f, box), 11.f, kSub);
    }
}

void Renderer::drawMenu(Graphics& g, const Game& game, const RenderContext& ctx, std::vector<UiButton>& out) {
    const float mw = 340.f;
    const float mh = 336.f;
    RectF r(width_ - 96.f - mw, height_ - 16.f - 186.f - 12.f - mh, mw, mh);
    panel(g, r, 236);
    float x = r.X + 16.f;
    float w = r.Width - 32.f;
    float y = r.Y + 12.f;
    text(g, "메뉴", x, y, 17.f, kText, 0, true);
    y += 32.f;

    if (game.hunting()) button(g, out, ctx, RectF(x, y, w, 42.f), "사냥 중단", UiAction::StopHunt, 0, true, false, true);
    else button(g, out, ctx, RectF(x, y, w, 42.f), "사냥 시작", UiAction::StartHunt);
    y += 52.f;

    text(g, "사냥 모드", x, y, 13.f, kSub, 0, true);
    if (!game.canChangeMode()) text(g, "전투 중에는 바꿀 수 없음", r.GetRight() - 16.f, y, 12.f, kWarnC, 2);
    y += 22.f;
    float bw = (w - 16.f) / 3.f;
    const CombatMode modes[3] = {CombatMode::Direct, CombatMode::SkillManual, CombatMode::Auto};
    bool autoAvailable = autoExpRemaining(game.dragonRecord()) > 0;
    for (int i = 0; i < 3; ++i) {
        bool en = game.canChangeMode() && (modes[i] != CombatMode::Auto || autoAvailable || game.huntMode() == CombatMode::Auto);
        button(g, out, ctx, RectF(x + i * (bw + 8.f), y, bw, 38.f), combatModeName(modes[i]), UiAction::SetMode,
               static_cast<int>(modes[i]), en, game.huntMode() == modes[i]);
    }
    y += 44.f;
    if (!autoAvailable) text(g, "이 드래곤은 자동 사냥 상한에 도달했습니다.", x, y, 12.f, kAutoC);
    y += 22.f;

    float hw = (w - 8.f) * 0.5f;
    bool towerOk = !game.waveActive();
    button(g, out, ctx, RectF(x, y, hw, 40.f), "탑 도전", UiAction::OpenTower, 0, towerOk);
    button(g, out, ctx, RectF(x + hw + 8.f, y, hw, 40.f), "기록·도감", UiAction::OpenCodex);
    y += 48.f;
    button(g, out, ctx, RectF(x, y, w, 36.f), "저장하고 게임 종료", UiAction::ExitGame);
    y += 44.f;
    paragraph(g, "설정 버튼을 다시 누르면 게임 입력이 꺼지고 마우스·키보드가 다른 프로그램으로 돌아갑니다.",
              RectF(x, y, w, 40.f), 11.f, kSub);
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
        if (e.kind != EnemyKind::Guardian) continue;
        RectF br(a.X + a.Width * 0.2f, a.Y + 14.f, a.Width * 0.6f, 16.f);
        bar(g, br, e.hp / e.maxHp, e.phase2 ? 0xFFFF4D4D : 0xFFE0703A, e.phase2 ? "탑의 수호자 (분노)" : "탑의 수호자");
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
    text(g, "드래곤 선택", r.X + 24.f, r.Y + 18.f, 22.f, kText, 0, true);
    std::string sub = "함께 탑을 오를 드래곤 한 마리를 고르세요. 이름과 수치는 임시안입니다.";
    if (shown < total) sub += "  (" + std::to_string(total) + "종 중 " + std::to_string(shown) + "종 표시)";
    text(g, sub, r.X + 24.f, r.Y + 54.f, 14.f, kSub);
    if (!game.save().graves.empty() || game.save().bestFloor > 0) {
        button(g, out, ctx, RectF(r.GetRight() - 24.f - 130.f, r.Y + 18.f, 130.f, 34.f), "기록·도감", UiAction::OpenCodex);
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
    text(g, s.impSeen ? "임프 (일반 적)" : "???", x, y, 13.f, s.impSeen ? kText : 0xFF7C8090);
    text(g, s.guardianSeen ? "탑의 수호자 (보스)" : "???", x + 220.f, y, 13.f, s.guardianSeen ? kText : 0xFF7C8090);
    y += 34.f;
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

// ---------------------------------------------------------------------------
// 설정 버튼
// ---------------------------------------------------------------------------

void Renderer::drawSettingsButton(Graphics& g, float size, const Sprites& sprites, bool inputOn, bool attention,
                                  bool hover, float time) {
    float c = size * 0.5f;
    float r = size * 0.5f - 4.f;
    float pulse = attention ? 0.5f + 0.5f * std::sin(time * 6.f) : 0.f;
    fillCircle(g, c, c + 2.f, r, Color(90, 0, 0, 0));
    fillCircle(g, c, c, r, hover ? Color(240, 46, 50, 70) : Color(232, 30, 32, 46));
    Color ring = attention ? Color(255, 255, static_cast<BYTE>(170 + 60 * pulse), 60)
                 : inputOn ? Color(255, 90, 220, 120)
                           : Color(255, 130, 136, 150);
    strokeCircle(g, c, c, r - 1.f, ring, attention ? 3.f + 2.f * pulse : 3.f);

    Bitmap* icon = sprites.get("button_settings");
    if (icon) {
        float s = r * 1.2f;
        g.DrawImage(icon, RectF(c - s * 0.5f, c - s * 0.5f - 4.f, s, s));
    } else {
        // 톱니바퀴 모양
        Color gear = inputOn ? Color(255, 200, 255, 210) : Color(255, 210, 214, 224);
        SolidBrush gb(gear);
        float gc = c - 4.f;
        GraphicsState st = g.Save();
        g.TranslateTransform(c, gc);
        for (int i = 0; i < 8; ++i) {
            g.FillRectangle(&gb, -3.5f, -r * 0.58f, 7.f, 8.f);
            g.RotateTransform(45.f);
        }
        g.Restore(st);
        fillCircle(g, c, gc, r * 0.42f, gear);
        fillCircle(g, c, gc, r * 0.18f, Color(255, 30, 32, 46));
    }
    text(g, inputOn ? "ON" : "OFF", c, size - 22.f, 10.f, inputOn ? 0xFF8CFFA0 : 0xFFB0B4C0, 1, true);
}

}  // namespace dt::win
