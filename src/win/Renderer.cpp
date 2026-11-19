#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "FrameProfiler.h"
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

Renderer::~Renderer() {
    stopBreathWorker();
}

void Renderer::shutdown() {
    stopBreathWorker();  // 뒤 스레드가 GDI+ 를 쓰는 중에 GDI+ 를 끄면 안 된다
    for (auto& f : breathCache_.frames) f.reset();
    breathCache_.pack = nullptr;
    curseBadges_.clear();
    curseFlashes_.clear();
    pieceCache_.clear();  // GDI+ 를 끄기 전에 합성해 둔 그림을 먼저 놓는다
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
static bool drawPieceRaw(Graphics& g, const UiPiece& p, const RectF& dst, float alpha, RectF* content) {
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


// UI 조각 캐시 (큐 12번). 선언과 이유는 Renderer.h 를 볼 것.
//
// 하는 일: 목적지 크기를 **장치 픽셀**로 바꿔 그 크기의 비트맵에 조각을 한 번 그려 두고,
// 다음부터는 변환을 끄고 1:1 로 붙인다. 1:1 이고 ColorMatrix 가 없으면 GDI+ 가 빠른 경로를 쓴다.
//
// 장치 픽셀로 캐시하는 이유: Graphics 에 ScaleTransform(dpi) 이 걸려 있다. 논리 크기로 캐시하면
// 붙일 때 또 배율이 먹어 다시 리샘플되고(느리고 흐리다) 캐시한 의미가 없다.
bool Renderer::drawPieceCached(Graphics& g, const UiPiece& p, const RectF& dst, float alpha, float scale,
                               RectF* content) {
    if (!cfg::kUiPieceCache || scale <= 0.f || !p.bmp) return drawPieceRaw(g, p, dst, alpha, content);
    if (dst.Width <= 0.f || dst.Height <= 0.f) return false;

    // 왼쪽 위와 오른쪽 아래를 각각 반올림해서 폭을 낸다. 가장자리가 어긋나지 않는다.
    const int devX = static_cast<int>(std::floor(dst.X * scale + 0.5f));
    const int devY = static_cast<int>(std::floor(dst.Y * scale + 0.5f));
    const int devW = static_cast<int>(std::floor(dst.GetRight() * scale + 0.5f)) - devX;
    const int devH = static_cast<int>(std::floor(dst.GetBottom() * scale + 0.5f)) - devY;
    if (devW <= 0 || devH <= 0) return false;
    // 화면만 한 조각은 캐시해 봐야 이득이 없고 메모리만 먹는다. 그냥 그린다.
    if (static_cast<size_t>(devW) * devH > cfg::kUiPieceCacheMaxPixels)
        return drawPieceRaw(g, p, dst, alpha, content);

    PieceKey key;
    key.bmp = p.bmp;
    key.fit = static_cast<int>(p.fit);
    key.l = p.left;
    key.t = p.top;
    key.r = p.right;
    key.b = p.bottom;
    key.devW = devW;
    key.devH = devH;
    key.alphaQ = static_cast<int>(alpha * 255.f + 0.5f);

    auto it = pieceCache_.find(key);
    if (it == pieceCache_.end()) {
        // 창 크기를 바꾸거나 스킨을 오가면 항목이 늘어난다. 한도를 넘으면 통째로 비운다
        // (LRU 를 두는 것보다 단순하고, 다시 채우는 값이 한 프레임이면 끝난다).
        if (pieceCache_.size() >= cfg::kUiPieceCacheMaxEntries ||
            pieceCacheBytes_ >= cfg::kUiPieceCacheMaxBytes) {
            pieceCache_.clear();
            pieceCacheBytes_ = 0;
        }
        PieceEntry e;
        auto cached = std::make_unique<Bitmap>(devW, devH, PixelFormat32bppPARGB);
        if (cached->GetLastStatus() != Ok) return drawPieceRaw(g, p, dst, alpha, content);
        {
            Graphics cg(cached.get());
            // 목적지와 같은 품질로 그려야 결과가 같다.
            cg.SetCompositingMode(CompositingModeSourceCopy);
            cg.Clear(Color(0, 0, 0, 0));
            cg.SetCompositingMode(CompositingModeSourceOver);
            cg.SetSmoothingMode(SmoothingModeAntiAlias);
            cg.SetInterpolationMode(InterpolationModeBilinear);
            RectF local(0.f, 0.f, static_cast<float>(devW), static_cast<float>(devH));
            RectF contentDev = local;
            e.drawn = drawPieceRaw(cg, p, local, alpha, &contentDev);
            e.contentDev = contentDev;
        }
        if (e.drawn) {
            e.bmp = std::move(cached);
            pieceCacheBytes_ += static_cast<size_t>(devW) * devH * 4;
        }
        it = pieceCache_.emplace(key, std::move(e)).first;
    }

    const PieceEntry& e = it->second;
    if (!e.drawn) {
        if (content) *content = dst;
        return false;  // 이 자리엔 그림을 쓰지 않는다(부르는 쪽이 도형으로 그린다)
    }

    // 변환을 끄고 장치 좌표에 1:1 로 붙인다.
    Matrix saved;
    g.GetTransform(&saved);
    g.ResetTransform();
    g.DrawImage(e.bmp.get(), devX, devY);
    g.SetTransform(&saved);

    if (content) {
        const float inv = 1.f / scale;
        *content = RectF(dst.X + e.contentDev.X * inv, dst.Y + e.contentDev.Y * inv,
                         e.contentDev.Width * inv, e.contentDev.Height * inv);
    }
    return true;
}

bool Renderer::piece(Graphics& g, const char* name, const RectF& r, float alpha, RectF* content) {
    if (!sprites_) return false;
    UiPiece p = sprites_->ui(skin_, name);
    if (!p.valid()) return false;
    return drawPieceCached(g, p, r, alpha, scale_, content);
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
    scale_ = ctx.scale > 0.f ? ctx.scale : 1.f;
    g_regions = outRegions;
    // 큐 11번: 프레임 측정. 꺼져 있으면 아래 stage() 는 분기 하나로 끝난다.
    // 켜져 있을 때만 단계마다 Flush 한다 — GDI+ 가 모아서 처리하면 단계별 숫자가 섞인다.
    FrameProfiler& prof = frameProfiler();
    auto stage = [&](FrameStage st) {
        if (!prof.enabled()) return;
        g.Flush(Gdiplus::FlushIntentionSync);
        prof.mark(st);
    };
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

    // #48: 키보드를 받는 동안 화면 전체를 살짝 어둡게 하던 표시를 없앴다. 바탕화면 전투를 시작하면
    // 키보드가 저절로 켜져서(syncKeyboard) 화면 전체에 필터를 씌운 것처럼 보였다.

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
    RectF r(16.f, 16.f, 268.f, 82.f + rowH * (rows + 1));  // +16: UI 캐시 줄
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
    // UI 조각 캐시가 실제로 쌓이는지 사람이 볼 수 있게 한 줄 더 (큐 12번).
    // 숫자가 늘기만 하고 멈추지 않으면 키가 매 프레임 달라지고 있다는 뜻이다 — 그때는 버그다.
    std::snprintf(buf, sizeof(buf), "UI 캐시 %zu개 · %.1f MB%s", uiCacheCount(),
                  uiCacheBytes() / (1024.0 * 1024.0), cfg::kUiPieceCache ? "" : " (꺼짐)");
    text(g, buf, x0, y + 20.f, 12.f, kSub, 0);
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
    const int columns = sh.columns > 0 ? sh.columns : sh.frames;
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
    g.DrawImage(sh.bmp, RectF(-w * 0.5f, -h * 0.5f, w, h), fw * (f % columns), fh * (f / columns),
                fw, fh, UnitPixel, &ia);
    g.Restore(st);
}

void Renderer::drawDragon(Graphics& g, const DragonActor& d, const Sprites& sprites, float time, float x, float y,
                          float height, bool preview) {
    if (!preview) markRegion(RectF(x - height * 0.5f, y - height * 0.5f, height, height), 4.f);
    const SpeciesDef& sp = speciesDef(d.species);
    bool flip = sp.flipAllowed && d.facing.x < 0.f;
    float alpha = (!preview && d.invuln > 0.f) ? 0.55f : 1.f;
    // v1.5: 맞았을 때 번쩍임을 낮췄다(0.6 -> 0.25). 전에는 그림이 안 보일 만큼 밝았다.
    float flash = (!preview && d.hitFlash > 0.f) ? 0.25f : 0.f;

    // 시트 애니메이션이 있으면 그것으로 그린다(v0.2). 미리보기는 항상 대기 동작.
    AnimState state = preview ? AnimState::Idle : d.anim;
    const SkillDef* breath = !preview && d.breath ? &sp.skills[static_cast<size_t>(d.breath->skill)] : nullptr;
    const Sheet breathBody = breath ? sprites.breathBody(sp, *breath) : Sheet{};
    const bool breathing = breathBody.bmp != nullptr;
    if (breathing) state = AnimState::Attack;
    Sheet sh = breathing ? breathBody : sprites.sheet(sp.key, state);
    // v1.4: 스킬 전용 본체 모션이 재생 중이고 그 시트가 있으면 공용 attack 대신 그것을 쓴다.
    // 시트가 없으면 아래에서 공용 attack(sh)으로 그려진다 — 데이터만 있고 그림이 없어도 괜찮다.
    const AnimDef* skillAnim = nullptr;
    if (breathing) {
        flip = sp.flipAllowed && d.breath->facingLeft;
        if (breath->usesUmbralBreath()) skillAnim = &breath->motionAnim;
    }
    if (!preview && !breathing && d.skillMotion >= 0 && d.skillMotion < static_cast<int>(sp.skills.size())) {
        const SkillDef& sk = sp.skills[static_cast<size_t>(d.skillMotion)];
        Sheet ms = sprites.skillSheet(sp.key, sk.motion);
        if (ms.bmp) {
            sh = ms;
            skillAnim = &sk.motionAnim;
        }
    }
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
        const AnimDef& ad = skillAnim ? *skillAnim : sp.anims[static_cast<int>(state)];
        float t = preview ? time : d.animTime;
        // render_scale 로 이 상태만 크게/작게 그린다. 칸 안의 바닥 기준선(240/256)이
        // 제자리에 남도록 그만큼 위로 올려 그려, 발 위치가 흔들리지 않게 한다.
        const float kAnchorFrac = 240.f / 256.f;
        float k = ad.renderScale;
        float drawH = height * k;
        float cyAdj = cy - (kAnchorFrac - 0.5f) * height * (k - 1.f);
        const int frame = breathing ? d.breath->bodyFrame(*breath) : animFrame(t, ad.fps, sh.frames, ad.loop);
        drawSheetFrame(g, sh, frame, x, cyAdj, drawH, flip, flash, alpha);
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

void Renderer::drawEnemy(Graphics& g, const Enemy& e, const Sprites& sprites, float time, float charScale,
                         const AnimState* forcedState) {
    const EnemyDef& edef = enemyDef(e.kind);
    // v1.5: 적 그림은 왼쪽을 보고 그려져 있다. 드래곤(오른쪽 기준)과 반대라 전에는 거꾸로 걸었다.
    bool flip = edef.artFacesRight ? (e.facing < 0.f) : (e.facing > 0.f);
    // v1.5: 맞았을 때 번쩍임이 너무 밝다는 이야기가 있어 낮췄다(0.7 -> 0.3).
    float flash = e.hitFlash > 0.f ? 0.3f : 0.f;
    float bob = std::sin(time * 6.f + e.id) * 2.f;
    float x = e.pos.x;
    float y = e.pos.y + bob;
    const EnemyDef& ed = edef;
    bool isBoss = ed.role == EnemyRole::Boss;
    float height = e.radius * 2.6f * charScale;
    // #49: 그림(시트·한 장 그림)은 발을 판정 원의 아래 끝(pos.y + 반지름)에 맞춘다. 드래곤과 같은 까닭이다.
    const float artY = e.pos.y + e.radius + bob - (240.f / 256.f - 0.5f) * height;

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
    if (forcedState) st = *forcedState;
    Sheet sh2 = sprites.sheet("enemy:" + ed.key, st);
    if (sh2.bmp) {
        const AnimDef& ad = ed.anims[static_cast<int>(st)];
        const float kAnchorFrac = 240.f / 256.f;
        float k = ad.renderScale;
        float cyAdj = artY - (kAnchorFrac - 0.5f) * height * (k - 1.f);
        drawSheetFrame(g, sh2, animFrame(time, ad.fps, sh2.frames, ad.loop), x, cyAdj, height * k, flip, flash, 1.f);
        drawnBySheet = true;
    }
    Bitmap* bmp = drawnBySheet ? nullptr : sprites.get(isBoss ? "boss_guardian" : "enemy_imp");
    if (drawnBySheet) {
        // 시트로 이미 그렸다
    } else if (bmp) {
        drawSprite(g, bmp, x, artY, height, flip, flash, 1.f);
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

    // 그림이 있으면 그림 위에, 도형이면 예전 자리에
    const float barY = (drawnBySheet || bmp) ? std::min(e.pos.y - e.radius - 18.f, artY - height * 0.5f - 10.f)
                                            : e.pos.y - e.radius - 18.f;
    // 체력 바(보스는 탑 HUD에 크게 표시)
    if (!isBoss) {
        float w = 56.f;
        bar(g, RectF(x - w * 0.5f, barY, w, 7.f), e.hp / e.maxHp, 0xFFFF6B6B);
        text(g, "Lv." + std::to_string(e.level), x, barY - 18.f, 12.f, 0xFFE0D0FF, 1, false, true);
    }
    if (e.alive && e.curseStacks > 0 && e.curseRemaining > 0.0) {
        const float spriteTop = drawnBySheet ? artY + height * (240.f / 256.f - 0.5f) -
            height * ed.anims[static_cast<int>(st)].renderScale * (240.f / 256.f) : barY;
        const CurseBadge badge{&e, x, std::min(barY - 40.f, spriteTop - 23.f)};
        if (forcedState) drawCurseBadge(g, badge, time);
        else curseBadges_.push_back(badge);
    }
}

void Renderer::drawCurseBadge(Graphics& g, const CurseBadge& badge, float time) {
    const Enemy& e = *badge.enemy;
    auto& flash = curseFlashes_[e.id];
    const bool maxStacks = e.curseStacks == kCurseMaxStacks;
    const bool reappliedAfterExpiry = e.curseStacks == 1 && e.curseRemaining > flash.remainingAtLastDraw;
    if (e.curseStacks > flash.stackAtLastDraw || reappliedAfterExpiry)
        flash.started = time - static_cast<float>(std::max(0.0, kCurseDuration - e.curseRemaining));
    flash.stackAtLastDraw = e.curseStacks;
    flash.remainingAtLastDraw = e.curseRemaining;
    flash.seen = true;
    const float duration = maxStacks ? 0.25f : 0.16f;
    const float pulse = std::clamp(1.f - (time - flash.started) / duration, 0.f, 1.f);

    constexpr float width = 36.f, height = 17.f;
    const float center = width_ >= width ? std::clamp(badge.x, width * 0.5f, width_ - width * 0.5f) : badge.x;
    const float x = center - width * 0.5f;
    const float y = std::max(3.f, badge.top);
    fillRound(g, RectF(x, y, width, height), 4.f, Color(228, 24, 12, 34));

    const float iconX = x + 8.f, iconY = y + 7.f;
    const Color ink(255, static_cast<BYTE>(170 + 62 * pulse),
                    static_cast<BYTE>(100 + 90 * pulse), static_cast<BYTE>(220 + 35 * pulse));
    if (maxStacks && pulse > 0.f) {
        Pen halo(Color(static_cast<BYTE>(200 * pulse), 200, 125, 255), 1.f);
        const float radius = 5.8f + pulse;
        g.DrawEllipse(&halo, iconX - radius, iconY - radius, radius * 2.f, radius * 2.f);
    }
    const PointF rune[] = {{iconX, iconY - 4.4f}, {iconX + 4.3f, iconY},
                           {iconX, iconY + 4.4f}, {iconX - 4.3f, iconY}};
    Pen outline(ink, 1.3f);
    g.DrawPolygon(&outline, rune, 4);
    g.DrawLine(&outline, iconX, iconY - 2.f, iconX, iconY + 2.f);

    constexpr wchar_t counts[][3] = {L"", L"×1", L"×2", L"×3", L"×4", L"×5"};
    SolidBrush label(Color(255, 249, 235, 255));
    g.DrawString(counts[e.curseStacks], 2, font(11.f, true), PointF(x + 14.f, y - 0.5f), &label);

    const float fraction = static_cast<float>(std::clamp(e.curseRemaining / kCurseDuration, 0.0, 1.0));
    SolidBrush track(Color(255, 65, 43, 80));
    SolidBrush gauge(ink);
    g.FillRectangle(&track, x + 3.f, y + 13.f, width - 6.f, 2.f);
    g.FillRectangle(&gauge, x + 3.f, y + 13.f, (width - 6.f) * fraction, 2.f);
}

void Renderer::drawWorld(Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx) {
    const World& w = game.world();
    float time = ctx.time;
    const SpeciesDef& dragonSp = speciesDef(w.dragon().species);
    uint32_t spColor = dragonSp.color;

    drawDotZones(g, w, sprites, time);

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
    curseBadges_.clear();
    for (auto& entry : curseFlashes_) entry.second.seen = false;
    for (const Enemy& e : w.enemies()) drawEnemy(g, e, sprites, time, cs);

    // v1.6: 본체 뒤 효과(fx_front=0)를 드래곤보다 먼저 그린다.
    // 지금은 fx= 를 적은 스킬이 하나도 없어서 이 층에 걸리는 효과가 없다 — 화면은 그대로다.
    drawFx(g, w, sprites, dragonSp, cs, false);
    drawBreath(g, w.dragon(), sprites, cs);
    if (game.hasDragon()) warmBreathCache(g, w, sprites, cs);

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
        // #49: 시트의 발(240/256 줄)을 판정 원의 아래 끝(pos.y + 반지름)에 맞춘다. 전에는 그림 가운데를
        // pos 에 두어 발이 그보다 아래로 내려갔고(캐릭터를 키울수록 더), 맨 아래 띠에서는 작업 표시줄
        // 밑으로 잘렸다. 판정·이동은 그대로이고 그림만 발 기준으로 선다.
        const float dh = cfg::kDragonRadius * 3.f * cs;
        const float dcy = d.pos.y + cfg::kDragonRadius - (240.f / 256.f - 0.5f) * dh;
        drawDragon(g, d, sprites, time, d.pos.x, dcy, dh, false);
        const float dTop = dcy - dh * 0.5f;
        float bw = 74.f;
        bar(g, RectF(d.pos.x - bw * 0.5f, dTop - 22.f, bw, 8.f), d.hp / d.stats.maxHp, kHpC);
        text(g, "Lv." + std::to_string(game.dragonRecord().level), d.pos.x, dTop - 42.f,
             13.f, 0xFFFFF3C4, 1, true, true);
    }

    drawProjectiles(g, w, sprites, dragonSp, cs);

    drawFx(g, w, sprites, dragonSp, cs, true);

    // FX 위에 배지를 그리고 피해 숫자는 기존처럼 가장 뒤에 올린다.
    for (const CurseBadge& badge : curseBadges_) drawCurseBadge(g, badge, time);
    curseBadges_.clear();
    for (auto it = curseFlashes_.begin(); it != curseFlashes_.end();) {
        if (!it->second.seen) it = curseFlashes_.erase(it);
        else ++it;
    }

    for (const FloatText& t : w.texts()) {
        float a = 1.f - std::clamp((t.t - t.duration * 0.6f) / (t.duration * 0.4f), 0.f, 1.f);
        uint32_t c = (t.color & 0x00FFFFFF) | (static_cast<uint32_t>(255 * a) << 24);
        text(g, t.text, t.pos.x, t.pos.y, t.size, c, 1, true, true);
    }
}

void Renderer::drawDotZones(Graphics& g, const World& w, const Sprites& sprites, float time) {
    const SpeciesDef& species = speciesDef(w.dragon().species);
    const uint32_t spColor = species.color;
    // 지속 피해 장판
    for (const DotZone& z : w.dots()) {
        if (z.spriteSkill >= 0 && z.spriteSkill < static_cast<int>(species.skills.size())) {
            const SkillDef& skill = species.skills[static_cast<size_t>(z.spriteSkill)];
            const Sheet sheet = sprites.fxSheet(species.key, skill.fx);
            if (sheet.bmp && sheet.frames > 0 && sheet.frameH > 0 && skill.radius > 0.f) {
                // Use the cast's stored radius so later changes to character size
                // cannot move the art away from the active damage zone.
                const float scale = z.radius / skill.radius;
                const float h = cfg::kDragonRadius * 3.f * scale * skill.fxScale * skill.fxAnim.renderScale;
                const float x = z.pos.x + skill.fxOffsetX * scale;
                const float y = z.pos.y + skill.fxOffsetY * scale;
                const float width = h * sheet.frameW / sheet.frameH;
                markRegion(RectF(x - width * 0.5f, y - h * 0.5f, width, h));
                drawSheetFrame(g, sheet, animFrame(z.duration - z.remaining, skill.fxAnim.fps, sheet.frames, false),
                               x, y, h, false, 0.f, skill.fxAlpha);
                continue;
            }
        }
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
}

void Renderer::drawProjectiles(Graphics& g, const World& w, const Sprites& sprites,
                               const SpeciesDef& dragonSp, float cs) {
    const uint32_t spColor = dragonSp.color;
    // 투사체
    for (const Projectile& p : w.projectiles()) {
        // 2026-11-17: fx= 를 적은 스킬이 쏜 발사체는 그 시트를 **현재 위치에** 그린다.
        // Fx 를 쓰지 않는 이유는 Fx 의 시작점·끝점이 고정이라 날아가는 것을 못 따라가서다.
        // 시트가 없거나 스킬을 못 찾으면 전과 똑같이 동그라미로 그린다.
        const SkillDef* ps = (p.skill >= 0 && p.skill < static_cast<int>(dragonSp.skills.size()))
                                 ? &dragonSp.skills[static_cast<size_t>(p.skill)] : nullptr;
        if (ps && !ps->fx.empty()) {
            Sheet sh = sprites.fxSheet(dragonSp.key, ps->fx);
            if (sh.bmp && sh.frames > 0 && sh.frameH > 0) {
                float h = cfg::kDragonRadius * 3.f * cs * ps->fxScale * ps->fxAnim.renderScale;
                if (h > 0.5f) {
                    // 날아가는 쪽을 본다. 목표가 사라졌으면 그냥 오른쪽으로 둔다.
                    bool flip = false;
                    for (const Enemy& e : w.enemies())
                        if (e.id == p.targetId) { flip = e.pos.x < p.pos.x; break; }
                    float wpx = h * static_cast<float>(sh.frameW) / static_cast<float>(sh.frameH);
                    float half = std::max(wpx, h) * 0.5f;
                    markRegion(RectF(p.pos.x - half, p.pos.y - half, half * 2.f, half * 2.f), 4.f);
                    // 날아가는 그림은 **반복 재생**한다(효과 시트의 1회 재생 규칙과 다르다).
                    drawSheetFrame(g, sh, animFrame(p.t, ps->fxAnim.fps, sh.frames, true), p.pos.x, p.pos.y,
                                   h, flip, 0.f, 1.f);
                    continue;
                }
            }
        }
        float r = p.big ? 11.f : 6.f;
        fillCircle(g, p.pos.x, p.pos.y, r * 2.f, col(spColor, 0.25f));
        fillCircle(g, p.pos.x, p.pos.y, r, col(lighten(spColor, 0.3f)));
        fillCircle(g, p.pos.x, p.pos.y, r * 0.45f, Color(255, 255, 255, 255));
    }
}

void Renderer::drawEnemyPreview(Graphics& g, const Enemy& enemy, const Sprites& sprites,
                                AnimState state, float time, float charScale) {
    drawEnemy(g, enemy, sprites, time, charScale, &state);
}

void Renderer::drawAnimationPreview(Graphics& g, const World& w, const Sprites& sprites,
                                    float time, bool body, bool effects, float charScale) {
    const DragonActor& d = w.dragon();
    const SpeciesDef& species = speciesDef(d.species);
    if (effects) {
        drawDotZones(g, w, sprites, time);
        drawFx(g, w, sprites, species, charScale, false);
        drawBreath(g, d, sprites, charScale);
    }
    if (body) {
        const float height = cfg::kDragonRadius * 3.f * charScale;
        const float cy = d.pos.y + cfg::kDragonRadius - (240.f / 256.f - 0.5f) * height;
        drawDragon(g, d, sprites, time, d.pos.x, cy, height, false);
    }
    if (effects) {
        drawProjectiles(g, w, sprites, species, charScale);
        drawFx(g, w, sprites, species, charScale, true);
    }
}

// Fx::key 와 같은 fx= 를 적은 스킬을 찾는다. 시트의 재생 설정(fxAnim)이 거기에 있다.
// 같은 fx 이름을 여러 스킬이 쓰면 먼저 적힌 쪽의 설정을 쓴다 — 시트도 한 장만 읽으므로 같다.
static const SkillDef* findSkillByFx(const SpeciesDef& sp, const std::string& fxKey) {
    for (const SkillDef& s : sp.skills) {
        if (s.fx == fxKey || s.fxImpact == fxKey) return &s;
    }
    return nullptr;
}

// v1.6 스프라이트 효과 한 장. 시트가 없으면 false 를 돌려주고, 부르는 쪽이 도형으로 그린다.
//
// 크기·위치 기준 (이 회차가 정했다 — PR 본문 참고)
//  - 기준 높이는 **드래곤 본체와 같다**: cfg::kDragonRadius * 3 * charScale.
//    거기에 fx_scale(본체 대비 배율) 과 [fx.anim] 의 render_scale 을 곱한다.
//    그래서 fx_scale=1 이면 효과가 본체와 같은 높이로 그려진다.
//  - 중심은 Fx::a (발동 지점)이고 fx_offset_y 만큼 세로로 옮긴다(위가 음수, px).
//  - 좌우 뒤집기는 하지 않는다. 효과는 본체 방향을 따르지 않는다(아래·좌우 대칭을 가정).
//
// 단계별 보이기 — 시간을 직접 나누지 않고 Fx::phase()/phaseProgress() 를 쓴다.
//  발동: 0.55 → 1.0 배로 커지며 옅은 데서 또렷해진다
//  지속: 1.0 배, 그대로
//  잔류: 1.0 → 1.18 배로 조금 더 퍼지며 사라진다
static void fxPhaseLook(const Fx& f, float& k, float& alpha) {
    const float q = f.phaseProgress();
    switch (f.phase()) {
        case FxPhase::Windup:
            k = 0.55f + 0.45f * q;
            alpha = 0.35f + 0.65f * q;
            break;
        case FxPhase::Hold:
            k = 1.f;
            alpha = 1.f;
            break;
        case FxPhase::Tail:
            k = 1.f + 0.18f * q;
            alpha = 1.f - q;
            break;
    }
}

bool Renderer::drawFxSheet(Graphics& g, const Fx& f, const Sprites& sprites, const SpeciesDef& sp, float charScale) {
    Sheet sh = sprites.fxSheet(sp.key, f.key);
    if (!sh.bmp || sh.frames <= 0 || sh.frameH <= 0) return false;  // 시트가 없다 — 도형으로 그린다

    const SkillDef* sk = findSkillByFx(sp, f.key);
    AnimDef ad;  // 스킬을 못 찾는 일은 없어야 하지만, 못 찾으면 AnimDef 기본값으로 그린다
    if (sk) ad = sk->fxAnim;

    float k = 1.f, alpha = 1.f;
    fxPhaseLook(f, k, alpha);
    // 스킬마다 정한 투명도를 곱한다(#79). 단계 흐름은 그대로 두고 전체만 옅게 한다.
    if (sk) alpha *= std::clamp(sk->fxAlpha, 0.f, 1.f);
    if (alpha <= 0.004f) return true;  // 다 사라졌다. 그리지 않아도 이 층에서 처리한 것이다

    float base = cfg::kDragonRadius * 3.f * charScale;
    float h = base * f.scale * ad.renderScale * k;
    if (h <= 0.5f) return true;
    // 보정값은 "100% 기준 px"다 — 그려지는 본체 크기에 맞추려면 charScale 을 곱한다.
    // (안 곱하면 캐릭터 크기 10배에서 머리 위 보정이 몸통에 파묻힌다. cfg::kCharScaleMax = 10.0)
    // 뒤집기는 드래곤의 facing 을 끌어오지 않고 Fx 의 발동점 a → 끝점 b 방향으로 판정한다.
    // Burst 처럼 a == b 인 효과는 dirSign 이 +1 로 고정된다 — 방향 없는 원형이라 그게 맞다.
    float dirSign = (f.b.x < f.a.x) ? -1.f : 1.f;
    float cx = f.a.x + f.offsetX * charScale * dirSign;
    float cy = f.a.y + f.offsetY * charScale;
    float wpx = h * static_cast<float>(sh.frameW) / static_cast<float>(sh.frameH);
    float half = std::max(wpx, h) * 0.5f;
    markRegion(RectF(cx - half, cy - half, half * 2.f, half * 2.f), 4.f);
    drawSheetFrame(g, sh, animFrame(f.t, ad.fps, sh.frames, ad.loop), cx, cy, h, /*flip=*/dirSign < 0.f, 0.f,
                   alpha);
    return true;
}

static void drawLineBlast(Graphics& g, const Fx& f, float fade) {
    const float halfWidth = f.radius * (1.f - 0.3f * fade);
    markRegion(RectF(std::min(f.a.x, f.b.x) - halfWidth, std::min(f.a.y, f.b.y) - halfWidth,
                     std::fabs(f.b.x - f.a.x) + 2.f * halfWidth, std::fabs(f.b.y - f.a.y) + 2.f * halfWidth));
    Pen pen(col(f.color, 0.7f * fade), halfWidth * 2.f);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawLine(&pen, f.a.x, f.a.y, f.b.x, f.b.y);
    Pen core(Color(static_cast<BYTE>(200 * fade), 255, 255, 255), f.radius * 0.5f);
    g.DrawLine(&core, f.a.x, f.a.y, f.b.x, f.b.y);
}

// 숨결 한 층의 원본 1px 이 논리 몇 px 로 그려지는가. drawBreath 와 캐시 미리 채우기가 같이 쓴다.
static float breathScale(const SpeciesDef& sp, const SkillDef& skill, float range, float charScale) {
    // Keep the visible stream at the configured reach from the mouth, even when
    // the enlarged sprite's muzzle is beyond the center-based hit segment.
    if (skill.usesUmbralBreath()) return range / (967.f - bonestorm::anchorX);
    // At alpha 128 the firing body silhouette is 175px high and the core is 88px high.
    // Scale both axes from that thickness so the internal shapes keep their proportions.
    const AnimDef& pose = sp.anims[static_cast<int>(AnimState::Attack)];
    const float bodyHeight = cfg::kDragonRadius * 3.f * charScale * pose.renderScale;
    return bodyHeight * (175.f / 256.f) / 88.f;
}

// 지금 변환에서 논리 1px 이 장치 몇 px 인가(App 의 dpi 배율). 회전 전에 부른다.
static float deviceScale(Graphics& g) {
    Matrix m;
    g.GetTransform(&m);
    REAL e[6];
    m.GetElements(e);
    return std::sqrt(e[0] * e[0] + e[1] * e[1]);
}

void Renderer::drawBreath(Graphics& g, const DragonActor& dragon, const Sprites& sprites, float charScale) {
    if (!dragon.alive || !dragon.breath) return;
    const BreathCast& cast = *dragon.breath;
    const SpeciesDef& sp = speciesDef(dragon.species);
    const SkillDef& skill = sp.skills[static_cast<size_t>(cast.skill)];
    const BreathSheets* pack = sprites.breathSheets(sp.key, skill.layeredBreath);
    const Sheet body = sprites.breathBody(sp, skill);
    if (!pack || !body.bmp) {
        Fx legacy;
        legacy.a = cast.origin;
        legacy.b = cast.origin + cast.aim * cast.range;
        legacy.radius = cast.radius;
        legacy.color = sp.color;
        legacy.key = skill.fx;
        legacy.scale = skill.fxScale;
        legacy.offsetX = skill.fxOffsetX;
        legacy.offsetY = skill.fxOffsetY;
        legacy.windup = skill.fxWindup > 0.f ? skill.fxWindup : cfg::kFxWindup;
        legacy.hold = skill.fxHold > 0.f ? skill.fxHold : cfg::kFxHold;
        legacy.tail = skill.fxTail > 0.f ? skill.fxTail : cfg::kFxTail;
        legacy.duration = legacy.windup + legacy.hold + legacy.tail;
        legacy.t = cast.elapsed;
        if (legacy.t < legacy.duration && !drawFxSheet(g, legacy, sprites, sp, charScale))
            drawLineBlast(g, legacy, 1.f - legacy.t / legacy.duration);
        return;
    }
    const bool umbral = skill.usesUmbralBreath();
    const AnimDef& pose = umbral ? skill.motionAnim : sp.anims[static_cast<int>(AnimState::Attack)];
    const float bodyHeight = cfg::kDragonRadius * 3.f * charScale * pose.renderScale;
    const bool left = sp.flipAllowed && cast.facingLeft;
    // Mouth landmarks in the right-facing 256px body cells, including the closing frames.
    static const Vec2 shadowMouth[] = {{232, 112}, {233, 110}, {225, 100}, {229, 100},
                                      {224, 99}, {239, 104}, {246, 108}, {248, 110},
                                      {245, 113}, {232, 116}, {230, 117}, {232, 117}};
    const Vec2 anchor = umbral ? shadowMouth[cast.bodyFrame(skill)] : Vec2{243.f, 129.f};
    const Vec2 mouth = dragon.pos + Vec2{(left ? -1.f : 1.f) * (anchor.x - 128.f) / 256.f *
                                            bodyHeight * body.frameW / body.frameH,
                                        cfg::kDragonRadius + (anchor.y - 240.f) / 256.f * bodyHeight};
    const float scale = breathScale(sp, skill, cast.range, charScale);
    if (scale <= 0.f) return;
    const float center = (512.f - bonestorm::anchorX) * scale;
    const float halfW = 512.f * scale, halfH = 256.f * scale;
    const float extentX = std::fabs(cast.aim.x) * halfW + std::fabs(cast.aim.y) * halfH;
    const float extentY = std::fabs(cast.aim.y) * halfW + std::fabs(cast.aim.x) * halfH;
    const Vec2 middle = mouth + cast.aim * center;
    markRegion(RectF(middle.x - extentX, middle.y - extentY, extentX * 2.f, extentY * 2.f));

    // 이번 프레임의 그림 key(0~3 시작, 4~11 반복, 12~15 끝).
    // 망령룡의 끝 첫 칸은 반복 그림에서 끝 그림으로 0.096초 동안 넘어간다(blendKey).
    constexpr float loopPhaseSeconds = BreathCast::frameSeconds / 2.5f;
    constexpr float endBlendSeconds = 0.096f;
    const auto loopPhase = [](float time) {
        return static_cast<int>((time + 1e-6f) / loopPhaseSeconds) % 8;
    };
    const int frame = cast.frame();
    int key = frame, blendKey = -1;
    float keyAlpha = 1.f;
    if (frame >= 4 && frame < 12) {
        key = 4 + (umbral ? frame - 4 : loopPhase(cast.elapsed - BreathCast::impactTime));
    } else if (frame == 12 && !umbral) {
        const float elapsed = std::max(0.f, cast.elapsed - BreathCast::endTime);
        keyAlpha = std::min(1.f, elapsed / endBlendSeconds);
        const float from = BreathCast::endTime - BreathCast::impactTime - loopPhaseSeconds * 0.5f;
        blendKey = 4 + loopPhase(from + elapsed);
    }

    const float devScale = scale * deviceScale(g);
    updateBreathCache(sprites, *pack, devScale);
    const GraphicsState state = g.Save();
    g.TranslateTransform(mouth.x, mouth.y);
    g.RotateTransform(std::atan2(cast.aim.y, cast.aim.x) * 57.2957795f);
    if (left) g.ScaleTransform(1.f, -1.f);
    const InterpolationMode savedMode = g.GetInterpolationMode();
    const RectF dest(center - halfW, -halfH, halfW * 2.f, halfH * 2.f);
    const bool cacheUsable = breathCache_.pack == pack;
    const auto paste = [&](int k, float alpha) {
        if (alpha <= 0.f) return;
        ImageAttributes ia;
        if (alpha < 1.f) {
            ColorMatrix cm = {{{1.f, 0.f, 0.f, 0.f, 0.f},
                               {0.f, 1.f, 0.f, 0.f, 0.f},
                               {0.f, 0.f, 1.f, 0.f, 0.f},
                               {0.f, 0.f, 0.f, alpha, 0.f},
                               {0.f, 0.f, 0.f, 0.f, 1.f}}};
            ia.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
        }
        Bitmap* cached = cacheUsable ? breathCache_.frames[static_cast<size_t>(k)].get() : nullptr;
        if (cached) {
            // 캐시는 장치 픽셀 크기라 1:1 이다(상한에 걸린 큰 배율에서만 늘어난다).
            // 최근접 보간은 양선형보다 열 배 넘게 빠르고, 1:1 에서는 원본 픽셀을 그대로 옮긴다.
            const float cs = breathCache_.cacheScale;
            g.SetInterpolationMode(InterpolationModeNearestNeighbor);
            g.DrawImage(cached, dest, 0.f, 0.f, BreathSheets::kWidth * cs, BreathSheets::kHeight * cs, UnitPixel,
                        alpha < 1.f ? &ia : nullptr);
            return;
        }
        // 캐시가 아직 없으면(켠 직후·크기를 바꾼 직후) 합성본 한 장을 그대로 확대한다.
        ia.SetWrapMode(WrapModeTileFlipXY);
        g.SetInterpolationMode(savedMode);
        g.DrawImage(pack->frames[static_cast<size_t>(k)].get(), dest, 0.f, 0.f,
                    static_cast<float>(BreathSheets::kWidth), static_cast<float>(BreathSheets::kHeight), UnitPixel,
                    &ia);
    };
    if (blendKey >= 0) paste(blendKey, 1.f - keyAlpha);
    paste(key, keyAlpha);
    g.SetInterpolationMode(savedMode);
    g.Restore(state);
}

void Renderer::stopBreathWorker() {
    BreathCache& c = breathCache_;
    if (c.job) c.job->cancel = true;
    // 만드는 중인 한 장(큰 배율에서 길어야 수십 ms)이 끝나면 멈춘다.
    if (c.worker.joinable()) c.worker.join();
    c.job.reset();
}

void Renderer::updateBreathCache(const Sprites& sprites, const BreathSheets& pack, float devScale) {
    if (!cfg::kBreathCache || devScale <= 0.f) return;
    BreathCache& c = breathCache_;

    // 뒤에서 다 만든 그림을 가져온다. 잠겨 있으면 다음 프레임에 가져간다(그리는 스레드를 세우지 않는다).
    if (c.job) {
        std::unique_lock<std::mutex> lock(c.job->m, std::try_to_lock);
        if (lock.owns_lock()) {
            for (size_t k = 0; k < c.frames.size(); ++k)
                if (c.job->done[k]) c.frames[k] = std::move(c.job->done[k]);
        }
    }

    const float maxScale = std::sqrt(static_cast<float>(cfg::kBreathCacheMaxPixels) /
                                     (static_cast<float>(BreathSheets::kWidth) * BreathSheets::kHeight));
    const float target = std::min(devScale, maxScale);
    const bool samePack = c.pack == &pack && c.generation == sprites.generation();
    if (samePack && std::fabs(c.cacheScale - target) < 1e-4f) return;

    if (samePack) {
        // 배율만 바뀌었다 — 크기를 끄는 중일 수 있으니 같은 배율이 이어질 때까지 기다린다.
        // 그동안은 지금 캐시를 늘려 그린다(최근접이라 조금 거칠 뿐 느려지지 않는다).
        if (std::fabs(c.wantScale - target) > 1e-4f) {
            c.wantScale = target;
            c.wantFrames = 0;
        }
        if (++c.wantFrames < cfg::kBreathCacheSettleFrames) return;
    }

    stopBreathWorker();
    for (auto& f : c.frames) f.reset();
    c.pack = &pack;
    c.generation = sprites.generation();
    c.cacheScale = target;
    c.wantScale = target;
    c.wantFrames = 0;

    // 뒤 스레드가 읽을 복사본. 원본(pack)은 그리는 스레드가 계속 쓰므로 넘기지 않는다.
    std::array<std::unique_ptr<Bitmap>, BreathSheets::kFrames> sources;
    for (size_t k = 0; k < sources.size(); ++k) {
        Bitmap* from = pack.frames[k].get();
        if (!from) return;
        auto copy = std::make_unique<Bitmap>(BreathSheets::kWidth, BreathSheets::kHeight, PixelFormat32bppPARGB);
        if (copy->GetLastStatus() != Ok) return;
        Graphics cg(copy.get());
        cg.SetCompositingMode(CompositingModeSourceCopy);
        if (cg.DrawImage(from, 0, 0, BreathSheets::kWidth, BreathSheets::kHeight) != Ok) return;
        sources[k] = std::move(copy);
    }

    auto job = std::make_shared<BreathJob>();
    c.job = job;
    const int devW = static_cast<int>(std::ceil(BreathSheets::kWidth * target));
    const int devH = static_cast<int>(std::ceil(BreathSheets::kHeight * target));
    c.worker = std::thread([job, sources = std::move(sources), devW, devH, target]() mutable {
        // 쏘는 순서대로 만든다(시작 → 반복 → 끝).
        for (size_t k = 0; k < sources.size() && !job->cancel; ++k) {
            auto bmp = std::make_unique<Bitmap>(devW, devH, PixelFormat32bppPARGB);
            if (bmp->GetLastStatus() != Ok) continue;
            {
                Graphics g(bmp.get());
                // 화면과 같은 품질로 키워야 결과가 같다(App::render 의 설정).
                g.SetCompositingMode(CompositingModeSourceCopy);
                g.Clear(Color(0, 0, 0, 0));
                g.SetCompositingMode(CompositingModeSourceOver);
                g.SetInterpolationMode(InterpolationModeBilinear);
                ImageAttributes ia;
                ia.SetWrapMode(WrapModeTileFlipXY);
                g.DrawImage(sources[k].get(),
                            RectF(0.f, 0.f, BreathSheets::kWidth * target, BreathSheets::kHeight * target), 0.f,
                            0.f, static_cast<float>(BreathSheets::kWidth),
                            static_cast<float>(BreathSheets::kHeight), UnitPixel, &ia);
            }
            sources[k].reset();
            std::lock_guard<std::mutex> lock(job->m);
            job->done[k] = std::move(bmp);
        }
    });
}

void Renderer::warmBreathCache(Graphics& g, const World& w, const Sprites& sprites, float charScale) {
    const DragonActor& d = w.dragon();
    if (!cfg::kBreathCache || !d.alive || d.breath) return;
    const SpeciesDef& sp = speciesDef(d.species);
    for (int s : w.slots()) {
        if (s < 0 || s >= static_cast<int>(sp.skills.size())) continue;
        const SkillDef& skill = sp.skills[static_cast<size_t>(s)];
        if (skill.layeredBreath.empty()) continue;
        const BreathSheets* pack = sprites.breathSheets(sp.key, skill.layeredBreath);
        if (!pack || !sprites.breathBody(sp, skill).bmp) continue;
        updateBreathCache(sprites, *pack, breathScale(sp, skill, skill.range, charScale) * deviceScale(g));
        return;
    }
}

void Renderer::drawFx(Graphics& g, const World& w, const Sprites& sprites, const SpeciesDef& sp, float charScale,
                      bool inFront) {
    for (const Fx& f : w.fx()) {
        if (f.t < 0.f) continue;
        if (f.inFront != inFront) continue;  // 이 층이 아니다
        // v1.6: 시트가 있으면 스프라이트로 그리고, 없으면 아래 도형 분기로 내려간다.
        if (!f.key.empty() && drawFxSheet(g, f, sprites, sp, charScale)) continue;
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
    const bool showSouls = sp.key == "revenant";
    const float expandedHeight = showSouls ? 310.f : 292.f;
    const float cardH = game.panelCollapsed() ? 44.f : expandedHeight;
    Vec2 saved = game.panelPos();
    float rx = saved.x >= 0.f ? saved.x : width_ - 96.f - cardW;
    float ry = saved.y >= 0.f ? saved.y : height_ - 16.f - expandedHeight;
    // 화면 밖으로 나가지 않게 맞춘다(해상도가 바뀌어도 사라지지 않게)
    rx = std::clamp(rx, 0.f, std::max(0.f, width_ - cardW));
    ry = std::clamp(ry, 0.f, std::max(0.f, height_ - cardH));
    RectF r(rx, ry, cardW, cardH);
    lastCardRect_ = r;  // v1.9: App 이 끌기 범위를 자를 때 쓴다
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

    if (showSouls) {
        text(g, "영혼 " + std::to_string(d.souls) + " / " + std::to_string(kRevenantSoulCap),
             x, y, 13.f, 0xFFA6FFF0);
        y += 18.f;
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
    lastMenuRect_ = r;  // v1.9: App 이 끌기 범위를 자를 때 쓴다
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

        // v1.9: 작업표시줄 꾸미기. 스킨에 taskbar.png 가 있는 드래곤에서만 효과가 있다.
        toggle(g, out, ctx, RectF(x, y, w, 26.f * s), "작업표시줄 꾸미기", game.taskbarSkin(),
               UiAction::ToggleTaskbarSkin, 0, true);
        y += 32.f * s;
        paragraph(g, "선택한 드래곤에 맞춰 작업표시줄에 띠를 덮습니다. 클릭은 그대로 통과하지만 "
                     "시작 단추와 아이콘이 가려 보일 수 있습니다.",
                  RectF(x, y, w, 46.f * s), 11.f * s, kSub);
        y += 52.f * s;

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
    text(g, "탑 " + std::to_string(game.towerFloor()) + "층 / " + std::to_string(game.towerFloorCount()), a.X + 16.f,
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
            // v1.6: 층 수가 데이터에서 오므로 "마지막 층인가"가 아니라 "다음 층이 보스인가"로 본다.
            std::string next =
                game.towerFloorDef(game.towerFloor() + 1).boss ? "다음은 보스 층입니다." : "다음 층으로 갈까요?";
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
    text(g, "탑 최고 도달 층: " + std::to_string(s.bestFloor) + " / " + std::to_string(game.towerFloorCount()) +
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

}  // namespace dt::win
