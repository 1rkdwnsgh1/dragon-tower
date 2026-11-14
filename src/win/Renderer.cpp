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

// 이번 프레임에 모은 영역. drawOverlay 동안만 유효하다.
static FrameRegions* g_regions = nullptr;
static void markRegion(const RectF& r, float pad = 6.f) {
    if (!g_regions) return;
    RectF padded(r.X - pad, r.Y - pad, r.Width + pad * 2.f, r.Height + pad * 2.f);
    g_regions->add(padded);
}

// 장식이 목적지에 비해 너무 크면 그림 대신 도형으로 그린다. 1109px짜리 창틀을 270px 상태창에
// 9조각으로 욱여넣으면 테두리가 가운데를 다 먹어 장식만 남는다(v0.9에서 실제로 그랬다).
// 테두리가 목적지의 이 비율을 넘지 않도록 줄이고, 그래도 너무 작아지면 포기한다.
static const float kBorderShare = 0.35f;  // 테두리가 차지해도 되는 최대 비율
static const float kMinArtScale = 0.55f;  // 이보다 더 줄여야 하면 그림을 쓰지 않는다

// 한 조각을 목적지에 맞춰 그린다. content 에는 글자를 놓아도 되는 안쪽 영역을 돌려준다.
// 그리지 않았으면 false (부르는 쪽에서 도형으로 그린다).
static bool drawPiece(Graphics& g, const UiPiece& p, const RectF& dst, float alpha, RectF* content) {
    ImageAttributes ia;
    ColorMatrix cm = {{{1.f, 0.f, 0.f, 0.f, 0.f},
                       {0.f, 1.f, 0.f, 0.f, 0.f},
                       {0.f, 0.f, 1.f, 0.f, 0.f},
                       {0.f, 0.f, 0.f, alpha, 0.f},
                       {0.f, 0.f, 0.f, 0.f, 1.f}}};
    ia.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    ia.SetWrapMode(WrapModeTileFlipXY);  // 가장자리에서 바깥 픽셀이 번지지 않게 한다
    float sw = static_cast<float>(p.bmp->GetWidth());
    float sh = static_cast<float>(p.bmp->GetHeight());
    if (sw <= 0.f || sh <= 0.f || dst.Width <= 0.f || dst.Height <= 0.f) return false;
    if (content) *content = dst;

    // 모양이 정해진 것(토글·체크)은 비율을 지켜 가운데에 넣는다.
    if (p.fit == UiFit::Contain) {
        float s = std::min(dst.Width / sw, dst.Height / sh);
        float w = sw * s, h = sh * s;
        RectF d(dst.X + (dst.Width - w) * 0.5f, dst.Y + (dst.Height - h) * 0.5f, w, h);
        g.DrawImage(p.bmp, d, 0.f, 0.f, sw, sh, UnitPixel, &ia);
        return true;
    }
    if (!p.stretch()) {
        g.DrawImage(p.bmp, dst, 0.f, 0.f, sw, sh, UnitPixel, &ia);
        return true;
    }
    float l = static_cast<float>(p.left), tp = static_cast<float>(p.top);
    float rt = static_cast<float>(p.right), bt = static_cast<float>(p.bottom);

    // 높이 맞춤: 세로로는 절대 늘리거나 줄이지 않고 통째로 축소한 뒤 가로만 늘린다.
    // 버튼·막대처럼 낮고 가로로 긴 것에 쓴다. 세로 눌림이 생기지 않는다.
    if (p.fit == UiFit::Height) {
        float s = dst.Height / sh;
        float dl = l * s, dr = rt * s;
        if (dl + dr > dst.Width) {  // 아주 좁은 자리면 양끝 장식만 줄인다
            float k = dst.Width / (dl + dr);
            dl *= k;
            dr *= k;
        }
        float sxs[4] = {0.f, l, sw - rt, sw};
        float dxs[4] = {dst.X, dst.X + dl, dst.GetRight() - dr, dst.GetRight()};
        for (int ix = 0; ix < 3; ++ix) {
            float dw = dxs[ix + 1] - dxs[ix], sw2 = sxs[ix + 1] - sxs[ix];
            if (dw <= 0.f || sw2 <= 0.f) continue;
            g.DrawImage(p.bmp, RectF(dxs[ix], dst.Y, dw, dst.Height), sxs[ix], 0.f, sw2, sh, UnitPixel, &ia);
        }
        if (content) *content = RectF(dst.X + dl, dst.Y, std::max(0.f, dst.Width - dl - dr), dst.Height);
        return true;
    }

    // 9조각: 테두리가 목적지를 다 먹지 않도록 가로·세로 같은 배율로 줄인다.
    float s = 1.f;
    if (l + rt > 0.f) s = std::min(s, dst.Width * kBorderShare / (l + rt));
    if (tp + bt > 0.f) s = std::min(s, dst.Height * kBorderShare / (tp + bt));
    if (s < kMinArtScale) return false;  // 이 자리에 쓰기엔 장식이 너무 큰 그림이다
    float dl = l * s, dr = rt * s, dt_ = tp * s, db = bt * s;
    float sxs[4] = {0.f, l, sw - rt, sw};
    float sys[4] = {0.f, tp, sh - bt, sh};
    float dxs[4] = {dst.X, dst.X + dl, dst.GetRight() - dr, dst.GetRight()};
    float dys[4] = {dst.Y, dst.Y + dt_, dst.GetBottom() - db, dst.GetBottom()};
    for (int iy = 0; iy < 3; ++iy) {
        for (int ix = 0; ix < 3; ++ix) {
            float dw = dxs[ix + 1] - dxs[ix], dh = dys[iy + 1] - dys[iy];
            float sw2 = sxs[ix + 1] - sxs[ix], sh2 = sys[iy + 1] - sys[iy];
            if (dw <= 0.f || dh <= 0.f || sw2 <= 0.f || sh2 <= 0.f) continue;
            g.DrawImage(p.bmp, RectF(dxs[ix], dys[iy], dw, dh), sxs[ix], sys[iy], sw2, sh2, UnitPixel, &ia);
        }
    }
    if (content)
        *content = RectF(dst.X + dl, dst.Y + dt_, std::max(0.f, dst.Width - dl - dr),
                         std::max(0.f, dst.Height - dt_ - db));
    return true;
}

bool Renderer::piece(Graphics& g, const char* name, const RectF& r, float alpha, RectF* content) {
    if (!sprites_) return false;
    UiPiece p = sprites_->ui(skin_, name);
    if (!p.valid()) return false;
    return drawPiece(g, p, r, alpha, content);
}

void Renderer::panel(Graphics& g, const RectF& r, uint8_t alpha) {
    markRegion(r);
    if (piece(g, "panel", r, alpha / 255.f)) return;
    fillRound(g, RectF(r.X + 3.f, r.Y + 5.f, r.Width, r.Height), 14.f, Color(static_cast<BYTE>(alpha / 3), 0, 0, 0));
    fillRound(g, r, 14.f, Color(alpha, 24, 26, 38));
    strokeRound(g, r, 14.f, Color(255, 86, 96, 140), 1.5f);
}

void Renderer::button(Graphics& g, std::vector<UiButton>& out, const RenderContext& ctx, const RectF& r,
                      const std::string& label, UiAction action, int arg, bool enabled, bool selected, bool danger) {
    bool hover = enabled && r.Contains(PointF(ctx.mouseX, ctx.mouseY));
    // 그림이 있으면 그림으로. 눌린 느낌(selected)과 hover 는 같은 "켜진" 그림을 쓴다.
    const char* art = !enabled ? "button_off" : (selected || hover) ? "button_hover" : "button";
    RectF content = r;
    if (!piece(g, art, r, 1.f, &content)) {
        content = r;
        Color fill = !enabled ? Color(200, 48, 50, 60)
                     : selected ? Color(255, 70, 128, 214)
                     : danger ? (hover ? Color(255, 170, 70, 70) : Color(255, 132, 52, 56))
                              : (hover ? Color(255, 80, 92, 134) : Color(255, 56, 64, 94));
        fillRound(g, r, 8.f, fill);
        if (selected) strokeRound(g, r, 8.f, Color(255, 170, 210, 255), 2.f);
        else if (hover) strokeRound(g, r, 8.f, Color(255, 150, 160, 200), 1.5f);
    } else if (danger) {
        // 위험한 버튼은 그림 위에 붉은 기를 덧입혀 구분한다
        fillRound(g, r, 8.f, Color(70, 200, 60, 60));
    }
    // 글자는 장식 안쪽(content)에 놓는다. 밝은 뼈 장식 위에 글자가 올라가지 않게 한다.
    float size = r.Height >= 40.f ? 16.f : 14.f;
    if (content.Width < size * 2.f) content = r;  // 안쪽이 너무 좁으면 그냥 가운데
    text(g, label, content.X + content.Width * 0.5f, content.Y + (content.Height - size * 1.35f) * 0.5f, size,
         enabled ? kText : 0xFF7C8090, 1, true);
    UiButton b;
    b.rect = r;
    b.action = action;
    b.arg = arg;
    b.enabled = enabled;
    out.push_back(b);
    markRegion(r);
}

void Renderer::bar(Graphics& g, const RectF& r, float ratio, uint32_t color, const std::string& label) {
    ratio = std::clamp(ratio, 0.f, 1.f);
    // 채움 그림은 색으로 고른다. 체력은 적게 남으면 빨간 그림으로 바꾼다.
    const char* fillArt = color == kHpC ? (ratio <= cfg::kLowHpRatio   ? "bar_fill_low"
                                           : ratio <= cfg::kMidHpRatio ? "bar_fill_hp_mid"
                                                                       : "bar_fill_hp")
                          : color == kExpC ? "bar_fill_exp"
                          : color == kAutoC ? "bar_fill_auto"
                                            : nullptr;
    RectF track = r;
    bool framed = piece(g, "bar_frame", r, 1.f, &track);
    if (!framed) {
        fillRound(g, r, r.Height * 0.5f, Color(220, 12, 14, 20));
        track = r;
    }
    if (ratio > 0.f) {
        float visible = std::max(1.f, track.Width * ratio);
        bool drawn = false;
        if (fillArt) {
            // 값마다 텍스처를 짧게 눌러 그리면 무늬와 양끝 장식이 찌그러진다.
            // 꽉 찬 길이로 그려 놓고 보이는 만큼만 잘라서 보여 준다.
            GraphicsState st = g.Save();
            g.SetClip(RectF(track.X, track.Y, visible, track.Height), CombineModeIntersect);
            drawn = piece(g, fillArt, track);
            g.Restore(st);
        }
        if (!drawn) fillRound(g, RectF(track.X, track.Y, std::max(track.Height, visible), track.Height),
                              track.Height * 0.5f, col(color));
    }
    if (!framed) strokeRound(g, r, r.Height * 0.5f, Color(160, 0, 0, 0), 1.f);
    if (!label.empty()) {
        float size = std::max(10.f, r.Height - 3.f);
        text(g, label, r.X + r.Width * 0.5f, r.Y + (r.Height - size * 1.35f) * 0.5f, size, kText, 1, true);
    }
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
    if (game.hasDragon()) drawStatusCard(g, game, sprites, ctx, out);
    if (sc == Screen::Tower) drawTowerHud(g, game);
    // v1.0: 메뉴는 키보드 조작과 상관없이 열려 있으면 그린다. 마우스로 언제든 쓸 수 있다.
    if (sc == Screen::Desktop && game.menuOpen() && game.hasDragon()) drawMenu(g, game, ctx, out);

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
    if (!preview) markRegion(RectF(x - height * 0.5f, y - height * 0.5f, height, height), 4.f);
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

void Renderer::drawEnemy(Graphics& g, const Enemy& e, const Sprites& sprites, float time, float charScale) {
    bool flip = e.facing < 0.f;
    float flash = e.hitFlash > 0.f ? 0.7f : 0.f;
    float bob = std::sin(time * 6.f + e.id) * 2.f;
    float x = e.pos.x;
    float y = e.pos.y + bob;
    const EnemyDef& ed = enemyDef(e.kind);
    bool isBoss = ed.role == EnemyRole::Boss;
    float height = e.radius * 2.6f * charScale;

    SolidBrush sh(Color(70, 0, 0, 0));
    g.FillEllipse(&sh, x - e.radius, e.pos.y + e.radius * 0.9f, e.radius * 2.f, e.radius * 0.5f);

    if (isBoss && e.phase2) {
        float pulse = 0.5f + 0.5f * std::sin(time * 6.f);
        fillCircle(g, x, y, e.radius * 1.45f, Color(static_cast<BYTE>(40 + 40 * pulse), 255, 60, 40));
    }
    if (e.castTimer > 0.f) {
        strokeCircle(g, x, y, e.radius * 1.25f, Color(200, 255, 90, 60), 2.f);
    }

    bool drawnBySheet = false;
    // v0.4: 적도 시트 애니메이션을 쓴다. 상태는 움직임·예고·피격으로 정한다.
    AnimState st = AnimState::Idle;
    if (e.hitFlash > 0.f) st = AnimState::Hit;
    else if (e.castTimer > 0.f) st = AnimState::Attack;
    else if (e.stun <= 0.f) st = AnimState::Move;
    Sheet sh2 = sprites.sheet("enemy:" + ed.key, st);
    if (sh2.bmp) {
        const AnimDef& ad = ed.anims[static_cast<int>(st)];
        const float kAnchorFrac = 240.f / 256.f;
        float k = ad.renderScale;
        float cyAdj = y - (kAnchorFrac - 0.5f) * height * (k - 1.f);
        drawSheetFrame(g, sh2, animFrame(time, ad.fps, sh2.frames, ad.loop), x, cyAdj, height * k, flip, flash, 1.f);
        drawnBySheet = true;
    }
    Bitmap* bmp = drawnBySheet ? nullptr : sprites.get(isBoss ? "boss_guardian" : "enemy_imp");
    if (drawnBySheet) {
        // 시트로 이미 그렸다
    } else if (bmp) {
        drawSprite(g, bmp, x, y, height, flip, flash, 1.f);
    } else if (!isBoss) {
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
    if (!isBoss) {
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

    // v0.8: 캐릭터 보기 크기. 그림만 커지고 충돌·사거리 판정은 그대로다.
    const float cs = game.charScale();
    for (const Enemy& e : w.enemies()) drawEnemy(g, e, sprites, time, cs);

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
        drawDragon(g, d, sprites, time, d.pos.x, d.pos.y, cfg::kDragonRadius * 3.f * cs, false);
        float bw = 74.f;
        bar(g, RectF(d.pos.x - bw * 0.5f, d.pos.y - cfg::kDragonRadius * 1.5f * cs - 22.f, bw, 8.f),
            d.hp / d.stats.maxHp, kHpC);
        text(g, "Lv." + std::to_string(game.dragonRecord().level), d.pos.x,
             d.pos.y - cfg::kDragonRadius * 1.5f * cs - 42.f,
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

void Renderer::toggle(Graphics& g, std::vector<UiButton>& out, const RenderContext& ctx, const RectF& r,
                      const std::string& label, bool on, UiAction action, int arg, bool enabled) {
    bool hover = enabled && r.Contains(PointF(ctx.mouseX, ctx.mouseY));
    // 글자 + 스위치. 스위치는 오른쪽 끝에 둔다.
    float sw = 42.f, sh = 22.f;
    RectF knobArea(r.GetRight() - sw, r.Y + (r.Height - sh) * 0.5f, sw, sh);
    // 그림이 있으면 켜짐/꺼짐 그림을 통째로 쓴다(손잡이까지 그려져 있다).
    const char* art = !enabled ? "toggle_disabled" : on ? "toggle_on" : "toggle_off";
    if (!piece(g, art, knobArea, hover ? 1.f : 0.92f)) {
        Color track = !enabled ? Color(200, 48, 50, 60)
                               : on ? Color(255, 70, 190, 120) : Color(235, 60, 64, 80);
        auto lift = [](BYTE v) { return static_cast<BYTE>(v > 230 ? 255 : v + 25); };
        if (hover) track = Color(255, lift(track.GetR()), lift(track.GetG()), lift(track.GetB()));
        fillRound(g, knobArea, sh * 0.5f, track);
        strokeRound(g, knobArea, sh * 0.5f, Color(255, 20, 22, 30), 1.f);
        float kr = sh * 0.5f - 3.f;
        float kx = on ? knobArea.GetRight() - kr - 3.f : knobArea.X + kr + 3.f;
        fillCircle(g, kx, knobArea.Y + sh * 0.5f, kr, Color(255, 245, 248, 255));
    }
    text(g, label, r.X, r.Y + (r.Height - 16.f) * 0.5f, 14.f, enabled ? kText : 0xFF7C8090, 0, true);
    text(g, on ? "ON" : "OFF", knobArea.X - 8.f, r.Y + (r.Height - 14.f) * 0.5f + 1.f, 12.f,
         on ? 0xFF8CFFA0 : 0xFF9AA2B4, 2);
    if (enabled) out.push_back({r, action, arg, true});
    markRegion(r);
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
        paragraph(g, "싸우기 시작하면\n키보드를 받습니다", RectF(hx, y + 2.f, r.GetRight() - hx - 8.f, box), 11.f, kSub);
    } else if ((game.screen() == Screen::Tower ? game.towerMode() : game.huntMode()) == CombatMode::Direct) {
        paragraph(g, "WASD 이동\nSpace 회피 · 1~3 스킬", RectF(hx, y + 2.f, r.GetRight() - hx - 8.f, box), 11.f, kSub);
    } else if ((game.screen() == Screen::Tower ? game.towerMode() : game.huntMode()) == CombatMode::SkillManual) {
        paragraph(g, "1~3 스킬\n이동·회피는 자동", RectF(hx, y + 2.f, r.GetRight() - hx - 8.f, box), 11.f, kSub);
    }
}

// 값을 끌어서 고르는 가로 막대. 오른쪽에 지금 값을 글자로 보여 준다.
// rect 에는 막대의 자리를, action 에는 끌기 동작을 담아 보낸다(App 이 rect 로 비율을 계산한다).
void Renderer::slider(Graphics& g, std::vector<UiButton>& out, const RenderContext& ctx, const RectF& r,
                      float value, float lo, float hi, UiAction action) {
    float ratio = std::clamp((value - lo) / std::max(0.0001f, hi - lo), 0.f, 1.f);
    float h = 6.f;
    RectF track(r.X, r.Y + (r.Height - h) * 0.5f, r.Width, h);
    fillRound(g, track, h * 0.5f, Color(255, 28, 30, 42));
    fillRound(g, RectF(track.X, track.Y, track.Width * ratio, h), h * 0.5f, Color(255, 96, 170, 240));
    float kx = track.X + track.Width * ratio;
    bool hover = r.Contains(PointF(ctx.mouseX, ctx.mouseY));
    fillCircle(g, kx, r.Y + r.Height * 0.5f, hover ? 9.f : 8.f, Color(255, 240, 246, 255));
    strokeRound(g, RectF(kx - 9.f, r.Y + r.Height * 0.5f - 9.f, 18.f, 18.f), 9.f, Color(160, 20, 24, 34), 1.f);
    out.push_back({r, action, 0, true});
    markRegion(r);
}

void Renderer::drawMenu(Graphics& g, const Game& game, const RenderContext& ctx, std::vector<UiButton>& out) {
    // 위치와 크기는 저장해 둔 값으로 그린다. 글자·버튼·여백을 모두 같은 배수로 키운다.
    const float s = game.menuScale();
    const float mw = 340.f * s;
    const float mh = 470.f * s;
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
        button(g, out, ctx, RectF(x, y, hw, 38.f * s), "탑 도전", UiAction::OpenTower, 0, towerOk);
        button(g, out, ctx, RectF(x + hw + 8.f * s, y, hw, 38.f * s), "기록·도감", UiAction::OpenCodex);
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
