#include "HeatBladeAtlases.h"
#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

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
    frameCache_.shutdown();
}

void Renderer::shutdown() {
    frameCache_.shutdown();  // 뒤 스레드가 GDI+ 를 쓰는 중에 GDI+ 를 끄면 안 된다
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

void Renderer::panel(Graphics& g, const RectF& r, uint8_t alpha, bool hit) {
    if (hit) markRegion(r);
    if (piece(g, "panel", r, alpha / 255.f)) return;
    fillRound(g, RectF(r.X + 3.f, r.Y + 5.f, r.Width, r.Height), 14.f, Color(static_cast<BYTE>(alpha / 3), 0, 0, 0));
    fillRound(g, r, 14.f, Color(alpha, 24, 26, 38));
    strokeRound(g, r, 14.f, Color(255, 86, 96, 140), 1.5f);
}

RectF Renderer::modalPanel(Graphics& g, RectF content, uint8_t alpha) {
    UiPiece frame = sprites_ ? sprites_->ui(skin_, "panel") : UiPiece{};
    if (frame.valid() && frame.fit == UiFit::Nine && frame.stretch()) {
        // 기본·그림자 패널의 보라색/녹색 안쪽 기준선. 꼭대기와 모서리 장식은 이 밖에 둔다.
        const float w = static_cast<float>(frame.bmp->GetWidth());
        const float h = static_cast<float>(frame.bmp->GetHeight());
        frame.left = frame.right = static_cast<int>(std::round(w * 0.052f));
        frame.top = static_cast<int>(std::round(h * 0.172f));
        frame.bottom = static_cast<int>(std::round(h * 0.103f));
        const float sides = static_cast<float>(frame.left + frame.right);
        const float ends = static_cast<float>(frame.top + frame.bottom);
        const float share = kBorderShare / (1.f - kBorderShare);
        const float borderScale = std::min({1.f, content.Width * share / std::max(1.f, sides),
                                                content.Height * share / std::max(1.f, ends)});
        const RectF outer(content.X - sides * borderScale * 0.5f, content.Y - ends * borderScale * 0.5f,
                          content.Width + sides * borderScale, content.Height + ends * borderScale);
        RectF inner = content;
        if (drawPieceCached(g, frame, outer, alpha / 255.f, scale_, &inner)) {
            markRegion(outer);
            return inner;
        }
    }
    markRegion(content);
    fillRound(g, content, 14.f, Color(alpha, 24, 26, 38));
    strokeRound(g, content, 14.f, Color(255, 86, 96, 140), 1.5f);
    return content;
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
    frameCache_.beginFrame(sprites.generation());
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
    towerListRect_ = {};
    if (sc == Screen::TowerSelect) {
        if (outRegions) outRegions->fullScreen = false;
        g_regions = nullptr;  // 변환된 선택창 외곽만 클릭 영역에 넣는다.
        drawTowerSelection(g, game, sprites, ctx, out, outRegions);
        return;
    }
    towerArtTime_ = -1.f;

    // #48: 키보드를 받는 동안 화면 전체를 살짝 어둡게 하던 표시를 없앴다. 바탕화면 전투를 시작하면
    // 키보드가 저절로 켜져서(syncKeyboard) 화면 전체에 필터를 씌운 것처럼 보였다.

    if (game.inTowerFlow()) {
        RectF a(game.arena().x, game.arena().y, game.arena().w, game.arena().h);
        fillRound(g, RectF(a.X - 6.f, a.Y - 6.f, a.Width + 12.f, a.Height + 12.f), 18.f, Color(150, 0, 0, 0));
        fillRound(g, a, 14.f, Color(225, 26, 22, 36));
        // 2026-11-25: 층의 지역 배경(1~10층 숲 입구 … 91~100층 심연). 그림이 없으면 예전처럼 격자.
        if (!drawTowerBackground(g, std::max(1, game.towerFloor()), a)) {
            Pen grid(Color(40, 140, 120, 200), 1.f);
            for (float x = a.X + 60.f; x < a.GetRight(); x += 60.f) g.DrawLine(&grid, x, a.Y, x, a.GetBottom());
            for (float y = a.Y + 60.f; y < a.GetBottom(); y += 60.f) g.DrawLine(&grid, a.X, y, a.GetRight(), y);
        }
        strokeRound(g, a, 14.f, Color(255, 150, 110, 220), 2.f);
    }

    if (game.hasDragon() || sc == Screen::Death) drawWorld(g, game, sprites, ctx);
    stage(FrameStage::World);  // 여기까지가 스프라이트(배경 띠·드래곤·적·효과)

    if (game.hasDragon()) drawStatusCard(g, game, sprites, ctx, out);
    if (sc == Screen::Tower) {
        drawTowerHud(g, game);
        drawTowerPotions(g, game, ctx, out);
    }
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
    RectF r(16.f, 16.f, 268.f, 98.f + rowH * (rows + 1));  // +16: UI 캐시 줄, +16: 그림 캐시 줄
    // 2026-11-20: 누를 것이 없는 표라 클릭을 아래로 통과시킨다(상호작용 없는 그림은 통과, 사용자 결정).
    panel(g, r, 232, false);

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
    // 그림 캐시(FrameCache). "만드는 중"이 줄지 않고 계속 쌓이면 키가 매 프레임 달라지고 있다는 뜻이다.
    std::snprintf(buf, sizeof(buf), "그림 캐시 %zu장 · %.0f MB · 만드는 중 %zu%s", frameCache_.count(),
                  frameCache_.bytes() / (1024.0 * 1024.0), frameCache_.pending(), cfg::kFrameCache ? "" : " (꺼짐)");
    text(g, buf, x0, y + 36.f, 12.f, kSub, 0);
}

// ---------------------------------------------------------------------------
// 월드
// ---------------------------------------------------------------------------

void Renderer::drawSprite(Graphics& g, Bitmap* bmp, float cx, float cy, float height, bool flip, float flash,
                          float alpha) {
    if (!bmp || bmp->GetHeight() == 0) return;
    const float bw = static_cast<float>(bmp->GetWidth());
    const float bh = static_cast<float>(bmp->GetHeight());
    const FrameCache::Source src{bmp, 0, 0, static_cast<int>(bw), static_cast<int>(bh)};
    drawFrameCached(g, src, cx, cy, height * bw / bh, height, flip, flash, alpha);
}

void Renderer::drawSheetFrame(Graphics& g, const Sheet& sh, int frame, float cx, float cy, float height, bool flip,
                              float flash, float alpha) {
    if (!sh.bmp || sh.frames <= 0 || sh.frameH <= 0) return;
    const int f = std::clamp(frame, 0, sh.frames - 1);
    const int columns = sh.columns > 0 ? sh.columns : sh.frames;
    const auto cellOf = [&](int i) {
        return FrameCache::Source{sh.bmp, sh.frameW * (i % columns), sh.frameH * (i / columns), sh.frameW, sh.frameH};
    };
    const float w = height * static_cast<float>(sh.frameW) / static_cast<float>(sh.frameH);
    drawFrameCached(g, cellOf(f), cx, cy, w, height, flip, flash, alpha);
    // 애니메이션의 다음 두 칸을 미리 맡긴다 — 지금 칸이 이미 있으면(크기가 자리 잡았으면) 뒤 스레드가
    // 앞서 나가 있어 칸이 바뀌는 순간에도 키운 그림을 쓴다.
    int devW = 0, devH = 0;
    const InterpolationMode interp = g.GetInterpolationMode();
    if (cfg::kFrameCache && sh.frames > 1 && frameDeviceSize(g, w, height, devW, devH) &&
        frameCache_.find(cellOf(f), devW, devH, flip, flash, interp)) {
        for (int step = 1; step <= 2; ++step)
            frameCache_.prefetch(cellOf((f + step) % sh.frames), devW, devH, flip, flash, interp);
    }
}

bool Renderer::frameDeviceSize(Graphics& g, float w, float h, int& devW, int& devH) {
    Matrix m;
    g.GetTransform(&m);
    REAL e[6];
    m.GetElements(e);
    const float ds = std::sqrt(std::fabs(e[0] * e[3] - e[1] * e[2]));
    float dw = std::fabs(w) * ds, dh = std::fabs(h) * ds;
    bool full = true;
    const float px = dw * dh;
    if (px > static_cast<float>(cfg::kFrameCacheMaxPixels)) {
        const float k = std::sqrt(static_cast<float>(cfg::kFrameCacheMaxPixels) / px);
        dw *= k;
        dh *= k;
        full = false;
    }
    devW = std::max(1, static_cast<int>(std::lround(dw)));
    devH = std::max(1, static_cast<int>(std::lround(dh)));
    return full;
}

void Renderer::drawFrameDirect(Graphics& g, const FrameCache::Source& src, float cx, float cy, float w, float h,
                               bool flip, float flash, float alpha) {
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
    g.DrawImage(src.bmp, RectF(-w * 0.5f, -h * 0.5f, w, h), static_cast<REAL>(src.x), static_cast<REAL>(src.y),
                static_cast<REAL>(src.w), static_cast<REAL>(src.h), UnitPixel, &ia);
    g.Restore(st);
}

void Renderer::drawFrameCached(Graphics& g, const FrameCache::Source& src, float cx, float cy, float w, float h,
                               bool flip, float flash, float alpha) {
    if (!src.bmp || alpha <= 0.004f || w <= 0.f || h <= 0.f) return;
    if (!cfg::kFrameCache) return drawFrameDirect(g, src, cx, cy, w, h, flip, flash, alpha);

    int devW = 0, devH = 0;
    const bool fullRes = frameDeviceSize(g, w, h, devW, devH);
    Matrix m;
    g.GetTransform(&m);
    REAL e[6];
    m.GetElements(e);
    // 회전·뒤집기가 없는 보통 화면이면 장치 좌표에 정수로 찍는다(가장 빠르다).
    const bool axisAligned = fullRes && std::fabs(e[1]) < 1e-5f && std::fabs(e[2]) < 1e-5f && e[0] > 0.f && e[3] > 0.f;

    ImageAttributes fade;
    const bool faded = alpha < 0.996f;
    if (faded) {
        ColorMatrix cm = {{{1.f, 0.f, 0.f, 0.f, 0.f},
                           {0.f, 1.f, 0.f, 0.f, 0.f},
                           {0.f, 0.f, 1.f, 0.f, 0.f},
                           {0.f, 0.f, 0.f, alpha, 0.f},
                           {0.f, 0.f, 0.f, 0.f, 1.f}}};
        fade.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    }

    if (Bitmap* scaled = frameCache_.find(src, devW, devH, flip, flash, g.GetInterpolationMode())) {
        if (axisAligned) {
            const float dcx = e[0] * cx + e[2] * cy + e[4];
            const float dcy = e[1] * cx + e[3] * cy + e[5];
            const INT x0 = static_cast<INT>(std::floor(dcx - devW * 0.5f + 0.5f));
            const INT y0 = static_cast<INT>(std::floor(dcy - devH * 0.5f + 0.5f));
            g.ResetTransform();
            g.DrawImage(scaled, Rect(x0, y0, devW, devH), 0, 0, devW, devH, UnitPixel, faded ? &fade : nullptr);
            g.SetTransform(&m);
            return;
        }
        // 회전이 있거나(숨결) 상한에 걸려 늘려야 하면 최근접으로 붙인다. 1:1 이면 원본 픽셀 그대로다.
        const InterpolationMode saved = g.GetInterpolationMode();
        g.SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.DrawImage(scaled, RectF(cx - w * 0.5f, cy - h * 0.5f, w, h), 0.f, 0.f, static_cast<REAL>(devW),
                    static_cast<REAL>(devH), UnitPixel, faded ? &fade : nullptr);
        g.SetInterpolationMode(saved);
        return;
    }

    // 키운 그림이 아직 없다 — 잘라 둔 칸을 대신 그린다. 긴 시트를 통째로 다루지 않으니 그것만으로도 빠르고,
    // 작은 그림은 양선형(예전과 같은 모양), 큰 그림은 최근접(한두 프레임만 조금 거칠다)으로 그린다.
    Bitmap* cell = frameCache_.cell(src);
    if (!cell) return drawFrameDirect(g, src, cx, cy, w, h, flip, flash, alpha);
    const InterpolationMode saved = g.GetInterpolationMode();
    const bool smooth = static_cast<size_t>(devW) * devH <= cfg::kFrameCacheSmoothFallbackPixels;
    g.SetInterpolationMode(smooth ? saved : InterpolationModeNearestNeighbor);
    drawFrameDirect(g, FrameCache::Source{cell, 0, 0, src.w, src.h}, cx, cy, w, h, flip, flash, alpha);
    g.SetInterpolationMode(saved);
}

// Short ballistic debris rendered directly at the ground anchor.
static void drawSoulfireDebris(Graphics& g, Vec2 anchor, float size, float progress, bool landing, int seed) {
    if (progress < 0.f || progress >= 1.f || size <= 0.f) return;
    const float fade = 1.f - progress;
    const Color hue = landing ? Color(188, 172, 139) : Color(112, 244, 221);
    for (int i = 0; i < 9; ++i) {
        const int pattern = (i * 7 + seed * 3) % 11;
        const float direction = i % 2 ? -1.f : 1.f;
        const float spread = (0.11f + 0.036f * pattern) * size;
        const float x = anchor.x + direction * spread * (0.14f + progress);
        const float y = anchor.y - spread * 2.1f * progress * fade;
        const float chip = size * (0.018f + 0.003f * (pattern % 4));
        SolidBrush brush(Color(static_cast<BYTE>(185.f * fade), hue.GetR(), hue.GetG(), hue.GetB()));
        PointF points[] = {{x - chip, y + chip * 0.3f}, {x + chip * direction, y - chip},
                           {x + chip, y + chip * 0.45f}};
        g.FillPolygon(&brush, points, 3);
    }
}

namespace {
struct SoulfireAnchors {
    Vec2 wingTip, tornMembrane, mouth;
};

// Source coordinates in the 256px body cells, starting at zero-based frame 8.
const SoulfireAnchors kSoulfireAnchors[] = {
    {{51,111},{65,147},{186,134}}, {{44,132},{66,157},{180,110}},
    {{46,136},{62,153},{182,108}}, {{48,117},{67,142},{186,111}},
    {{43,136},{61,161},{183,119}}, {{50,121},{65,148},{195,117}},
    {{61,152},{75,174},{184,112}}, {{62,160},{76,180},{191,126}},
    {{85,67},{101,110},{194,123}}, {{84,78},{103,115},{194,140}},
    {{74,66},{95,101},{193,141}}, {{73,66},{93,104},{193,148}},
    {{74,49},{96,95},{192,149}}, {{61,71},{87,101},{192,149}},
    {{45,91},{73,112},{192,149}}, {{41,111},{67,125},{192,149}},
    {{90,164},{99,176},{192,149}}, {{101,177},{106,191},{192,149}},
    {{50,108},{77,132},{192,149}}, {{53,81},{80,105},{192,149}},
    {{49,72},{77,91},{192,149}}, {{78,54},{95,99},{192,149}},
    {{84,68},{102,110},{187,124}}, {{42,110},{65,137},{196,116}},
    {{60,88},{72,123},{196,116}}, {{52,94},{75,129},{194,117}},
    {{65,112},{83,140},{199,151}}, {{48,85},{68,131},{193,123}},
};

// Tail tips in the same source cells as kSoulfireAnchors.
const Vec2 kSoulfireTailTips[] = {
    {26,205}, {12,207}, {28,200}, {26,193}, {23,201}, {21,184},
    {42,211}, {19,127}, {16,127}, {38,135}, {42,135}, {41,135},
    {29,136}, {28,135}, {28,135}, {38,142}, {27,134}, {28,133},
    {26,134}, {28,135}, {28,133}, {28,134}, {38,136}, {20,170},
    {20,160}, {17,180}, {19,172}, {18,183},
};
static_assert(std::size(kSoulfireTailTips) == std::size(kSoulfireAnchors));
}

void Renderer::drawSoulfireBodyVfx(Graphics& g, const DragonActor& d, const Sprites& sprites,
                                   const std::string& key, int frame, Vec2 center, float size,
                                   bool flip, bool front) {
    const SkillCast& cast = *d.skillCast;
    const Vec2 earlyMouth[] = {{185,157}, {193,151}, {198,131}, {187,158},
                                {213,157}, {198,136}, {194,157}, {188,154}};
    if (frame < 0 || frame >= 8 + static_cast<int>(std::size(kSoulfireAnchors))) return;
    const bool early = frame < 8;
    const SoulfireAnchors& anchors = kSoulfireAnchors[std::max(0, frame - 8)];
    const float sign = flip ? -1.f : 1.f;
    const float unit = size / 256.f;
    const auto worldPoint = [&](Vec2 p) {
        return center + Vec2{sign * (p.x - 128.f) * unit, (p.y - 128.f) * unit};
    };
    const float age = cast.elapsed;
    const float launch = age / cast.fxTime;
    const float flight = age - cast.fxTime;
    const float wingAlpha = std::clamp((launch - .59f) / .25f, 0.f, 1.f) *
                            std::clamp(1.f - (flight - kSoulfireBreathDuration) / .19f, 0.f, 1.f);
    const Sheet mote = sprites.fxSheet(key, "soulfire_mote");
    if (!early && wingAlpha > 0.f) {
        const Sheet wing = sprites.fxSheet(key, front ? "soulfire_wing_front" : "soulfire_wing");
        const float speed = d.stats.speed * cfg::kIdleFlySpeed *
                            (d.hasteTime > 0.f ? 1.f + d.hasteSpeed : 1.f);
        const float stretch = std::clamp(speed / 230.f, .75f, 1.45f);
        if (front && wing.bmp) {
            const Vec2 flank = worldPoint({(anchors.tornMembrane.x + anchors.mouth.x) * .5f - 19.f,
                                          (anchors.tornMembrane.y + anchors.mouth.y) * .5f + 15.f});
            const float extent = size * .62f;
            markRegion(RectF(flank.x - extent * .5f, flank.y - extent * .5f, extent, extent), 3.f);
            drawSheetFrame(g, wing, animFrame(age, 20.f, wing.frames, true),
                           flank.x, flank.y, extent, flip, 0.f, wingAlpha * .56f);
        }
        for (int i = 0; i < 2; ++i) {
            const Vec2 wingRoot = anchors.wingTip * .65f + anchors.tornMembrane * .35f;
            const Vec2 attachment = i == 0 ? wingRoot : kSoulfireTailTips[frame - 8];
            const Vec2 anchor = worldPoint(attachment + Vec2{i == 0 ? 24.f : 36.f, 0.f});
            const float extent = size * (i == 0 ? .80f : .62f);
            if (!front && wing.bmp) {
                markRegion(RectF(anchor.x - extent * stretch, anchor.y - extent * .5f,
                                 extent * stretch * 2.f, extent), 3.f);
                const auto state = g.Save();
                g.TranslateTransform(anchor.x, anchor.y);
                g.ScaleTransform(stretch, i == 0 ? 1.f : .74f);
                drawSheetFrame(g, wing, animFrame(age + i * .17f, 20.f, wing.frames, true),
                               -sign * .34f * extent, 0.f, extent, flip, 0.f,
                               wingAlpha * (i == 0 ? .95f : .76f));
                g.Restore(state);
            }
            for (int j = 0; front && mote.bmp && j < 5; ++j) {
                const float phase = std::fmod(age * (2.3f + .18f * j) + j * .218f + i * .37f, 1.f);
                const Vec2 ember = anchor + Vec2{-sign * extent * stretch * phase,
                                                extent * phase * .22f * std::sin(j * 2.4f + age * 7.f)};
                const float emberSize = size * .095f;
                markRegion(RectF(ember.x - emberSize * .5f, ember.y - emberSize * .5f,
                                 emberSize, emberSize), 2.f);
                drawSheetFrame(g, mote, animFrame(age, 20.f, mote.frames, true),
                               ember.x, ember.y, emberSize, flip, 0.f,
                               wingAlpha * (1.f - phase) * .85f);
            }
        }
    }

    const Vec2 mouth = worldPoint(early ? earlyMouth[frame] : anchors.mouth);
    const float gather = std::clamp((launch - .10f) / .90f, 0.f, 1.f);
    const float release = std::clamp(1.f - flight / .13f, 0.f, 1.f);
    if (gather <= 0.f || release <= 0.f || (front && flight >= 0.f)) return;

    const Sheet layer = sprites.fxSheet(key, front ? "soulfire_core" : "soulfire_orbit");
    if (!layer.bmp) return;
    const float extent = size * (front ? (.10f + .26f * gather) : (.66f - .22f * gather));
    const float opacity = release * std::min(gather * (front ? 1.55f : 3.5f), 1.f);
    markRegion(RectF(mouth.x - extent, mouth.y - extent, extent * 2.f, extent * 2.f), 3.f);
    const auto state = g.Save();
    g.TranslateTransform(mouth.x, mouth.y);
    g.ScaleTransform(sign, 1.f);
    if (!front) g.RotateTransform(270.f * gather);
    drawSheetFrame(g, layer, animFrame(age, 20.f, layer.frames, true),
                   0.f, 0.f, extent, false, 0.f, opacity);
    g.Restore(state);
}

void Renderer::drawDragon(Graphics& g, const DragonActor& d, const Sprites& sprites, float time, float x, float y,
                          float height, bool preview, bool showBody, bool effects) {
    const bool soulfire = !preview && d.skillCast && d.skillCast->soulfireCastId;
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
    const bool boneShotMotion = skillAnim && d.skillMotion >= 0 && sp.skills[d.skillMotion].boneShot;
    AnimDef awakenedAnim{};
    const bool showAwakened = !preview && d.alive && d.awakened && !breathing && !soulfire &&
        (!skillAnim || (d.skillMotion >= 0 && !sp.skills[d.skillMotion].spectralCleave &&
                        !sp.skills[d.skillMotion].ultimate));
    if (showAwakened && !boneShotMotion) {
        AnimState resolved = state;
        const Sheet alternate = sprites.sheet(sp.key + ":awakened", state, &resolved);
        if (alternate.bmp) {
            sh = alternate;
            awakenedAnim.fps = resolved == AnimState::Move ? 7.5f : resolved == AnimState::Attack ? 24.f : 3.f;
            awakenedAnim.loop = resolved != AnimState::Attack;
            awakenedAnim.renderScale = kAwakenedBodyScale;
            skillAnim = &awakenedAnim;
        }
    }
    bool animated = sh.bmp != nullptr && sh.frames > 1;

    // 시트가 여러 칸이면 그림 자체가 움직이므로 위아래 흔들기를 하지 않는다.
    float bob = animated ? 0.f : std::sin(time * (d.moving ? 9.f : 3.f)) * (d.moving ? 4.f : 2.5f);
    float cy = y + bob;

    // 그림자. 날고 있으면 작고 옅게 그려 공중에 떠 있는 것을 알린다.
    if (showBody) {
        float shrink = (!preview && d.flying) ? 0.6f : 1.f;
        if (soulfire) {
            const SkillCast& cast = *d.skillCast;
            const float flightEnd = cast.fxTime + kSoulfireBreathDuration;
            const float landing = cast.lockTime - flightEnd;
            shrink = 1.f - 2.2f * soulfireLift(cast.elapsed, cast.fxTime, landing, 1.f);
            const float footY = y + (240.f / 256.f - 0.5f) * height;
            const float push = (cast.elapsed / cast.fxTime - 0.44f) / 0.5f;
            const float contact = ((cast.elapsed - flightEnd) / landing - 0.72f) / 0.28f;
            if (effects) {
                drawSoulfireDebris(g, {x, footY}, height * 0.65f, push, true, 1);
                drawSoulfireDebris(g, {x, footY}, height, contact, true, 2);
            }
        }
        BYTE a = static_cast<BYTE>(70 * alpha * (soulfire ? shrink : (shrink < 1.f ? 0.55f : 1.f)));
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
        float k = ad.renderScale * (boneShotMotion ? kAwakenedBodyScale : 1.f);
        float drawH = height * k;
        float cyAdj = cy - (kAnchorFrac - 0.5f) * height * (k - 1.f);
        if (skillAnim && soulfire) {
            const SkillCast& cast = *d.skillCast;
            flip = sp.flipAllowed && cast.facingLeft;
            x += (flip ? -1.f : 1.f) * kSoulfireBodyOffsetX * height;
            const float flightEnd = cast.fxTime + kSoulfireBreathDuration;
            const float landing = cast.lockTime - flightEnd;
            cyAdj += (kAnchorFrac - kSoulfireFoot) * drawH -
                     soulfireLift(cast.elapsed, cast.fxTime, landing, height);
            // Hold the loaded crouch, then drive through the launch in a quick stroke.
            const float launch = std::clamp(cast.elapsed / cast.fxTime, 0.f, 1.f);
            const float wing = std::fmod(std::max(0.f, cast.elapsed - cast.fxTime), 1.f);
            const float stroke = wing < 0.36f ? wing * (0.6f / 0.36f)
                                              : 0.6f + (wing - 0.36f) * (0.4f / 0.64f);
            const float recovery = std::clamp((cast.elapsed - flightEnd) / landing, 0.f, 1.f);
            t = cast.elapsed < cast.fxTime ? 2.f * launch * launch
                : cast.elapsed < flightEnd ? 2.f + stroke
                : 3.f + recovery;
        }
        const SkillCast* cleave = !preview && d.skillCast && d.skillCast->skill >= 0 &&
                d.skillCast->skill < static_cast<int>(sp.skills.size()) &&
                skillAnim && sp.skills[d.skillCast->skill].spectralCleave ? &*d.skillCast : nullptr;
        if (skillAnim && d.skillCast && d.skillCast->boneShotOrigin) {
            t = d.skillCast->elapsed;
            flip = sp.flipAllowed && d.skillCast->facingLeft;
        }
        if (cleave) {
            t = cleave->elapsed;
            flip = sp.flipAllowed && cleave->facingLeft;
        }
        if (!preview)
            markRegion(RectF(x - drawH * 0.5f, cyAdj - drawH * 0.5f, drawH, drawH), 4.f);
        int frame = breathing ? d.breath->bodyFrame(*breath) : animFrame(t, ad.fps, sh.frames, ad.loop);
        const bool steelMotion = !preview && !skillAnim && !breathing && sp.key == "steel_dragon";
        int idleVentFrame = -1;
        if (steelMotion && state == AnimState::Idle && sh.frames == steel::idleFrames) {
            const Sheet ventBody = sprites.fxSheet(sp.key, "idle_vent_body");
            const Sheet ventFx = sprites.fxSheet(sp.key, "idle_vent");
            if (ventBody.frames == steel::ventBodyFrames && ventFx.frames == steel::ventFrames) {
                // V5 미리보기 순서: IDLE F01–F24 → VENT F01–F20 → IDLE F02–F24.
                const int cycle = animFrame(t, ad.fps, steel::idleCycleFrames, true);
                const int ventEnd = steel::idleFrames + steel::ventFrames;
                if (cycle >= steel::idleFrames && cycle < ventEnd) {
                    idleVentFrame = cycle - steel::idleFrames;
                    sh = ventBody;
                    frame = std::min(idleVentFrame, ventBody.frames - 1);
                } else {
                    frame = cycle < steel::idleFrames ? cycle : cycle - ventEnd + 1;
                }
            }
        }
        if (soulfire && skillAnim && effects)
            drawSoulfireBodyVfx(g, d, sprites, sp.key, frame, {x, cyAdj}, drawH, flip, false);
        const auto drawCleaveCast = [&](const char* layer) {
            const SkillDef& skill = sp.skills[cleave->skill];
            const Sheet sheet = sprites.fxSheet(sp.key, skill.fx + layer);
            // The FX canvas is 360 logical pixels centered on the 256px motion cell.
            drawSheetFrame(g, sheet, frame, x, cyAdj, drawH * (360.f / 256.f), flip, 0.f, alpha);
            return sheet.bmp != nullptr;
        };
        if (cleave && effects && !drawCleaveCast("_cast_rear") && t >= cleave->fxTime && t < cleave->hitTime) {
            const SkillDef& skill = sp.skills[cleave->skill];
            const Sheet swing = sprites.fxSheet(sp.key, skill.fx + "_swing");
            drawSheetFrame(g, swing, animFrame(t - cleave->fxTime, skill.fxAnim.fps, swing.frames, false),
                           x, cyAdj, drawH, flip, 0.f, alpha);
        }
        const auto orbit = [&](bool front) {
            if (!effects || !d.alive || !d.heatBladeStorm) return;
            const int tick = static_cast<int>(d.heatBladeStorm->time * 60.f + .0001f);
            const Vec2 torso = heatBladeTorso(state);
            const float anchorX = x + (flip ? -1.f : 1.f) * (torso.x - 128.f) * drawH / 256.f;
            const float anchorY = cyAdj + (torso.y - 128.f) * drawH / 256.f;
            for (int i = 0; i < heatBlade::orbitLayers; ++i) {
                const auto& layer = heatBlade::atlases[i];
                if ((layer.z > 0) != front || tick < layer.startTick || tick >= layer.stopTick) continue;
                const bool mirror = layer.mirror && flip;
                const float scale = layer.scale * height / 102.f;
                const int local = (tick - layer.startTick) * layer.fps / 60;
                const int index = layer.loop ? local % layer.frames : std::min(local, layer.frames - 1);
                const Sheet sheet = sprites.fxSheet(sp.key, layer.path);
                drawSheetFrame(g, sheet, index,
                               anchorX + (mirror ? -1.f : 1.f) * (layer.width * .5f - layer.anchorX) * scale,
                               anchorY + (layer.height * .5f - layer.anchorY) * scale,
                               layer.height * scale, mirror, 0.f, alpha);
            }
        };
        orbit(false);
        if (showBody) drawSheetFrame(g, sh, frame, x, cyAdj, drawH, flip, flash, alpha);
        if (steelMotion && effects && (d.overheatSkill >= 0 || d.overheatResidual > 0.f)) {
            const char* mask = nullptr;
            if (state == AnimState::Idle) mask = idleVentFrame >= 0 ? "overheat_idle_vent" : "overheat_idle";
            if (state == AnimState::Move) mask = "overheat_move";
            if (state == AnimState::Fly) mask = "overheat_fly";
            if (state == AnimState::Attack) mask = "overheat_attack";
            if (mask) {
                const Sheet heat = sprites.fxSheet(sp.key, mask);
                const float cooling = std::clamp(d.overheatResidual / kCoreOverheatResidual, 0.f, 1.f);
                const float charge = d.overheatSkill >= 0
                    ? 0.25f + 0.65f * std::clamp(1.f - d.overheatWindup / kCoreOverheatWindup, 0.f, 1.f) : 0.f;
                if (heat.frames == sh.frames && heat.frameW == sh.frameW && heat.frameH == sh.frameH)
                    drawSheetFrame(g, heat, frame, x, cyAdj, drawH, flip, 0.f, alpha * std::max(charge, cooling));
            }
        }
        if (steelMotion && effects) {
            if (state == AnimState::Fly && sh.frames == 24) {
                // V2 원본의 알파가 본체에 가려지는 영역을 반영한다. 연기 위에 추진 화염을 그린다.
                for (const char* layer : {"fly_vent", "fly_thrust"}) {
                    const Sheet fx = sprites.fxSheet(sp.key, layer);
                    if (fx.frames == sh.frames)
                        drawSheetFrame(g, fx, frame, x, cyAdj, drawH * (384.f / 256.f), flip, 0.f, alpha);
                }
            } else if (state == AnimState::Move && sh.frames == 16) {
                // WALK 본체와 같은 셀 좌표에 연기와 불씨를 순서대로 얹는다.
                for (const char* layer : {"walk_vent", "walk_ember"}) {
                    const Sheet fx = sprites.fxSheet(sp.key, layer);
                    if (fx.frames == sh.frames && fx.frameW == sh.frameW && fx.frameH == sh.frameH)
                        drawSheetFrame(g, fx, frame, x, cyAdj, drawH, flip, 0.f, alpha);
                }
            } else if (state == AnimState::Attack && sh.frames == 10) {
                const Sheet heat = sprites.fxSheet(sp.key, "basic_heat");
                if (heat.frames == sh.frames && heat.frameW == sh.frameW && heat.frameH == sh.frameH)
                    drawSheetFrame(g, heat, frame, x, cyAdj, drawH, flip, 0.f, alpha);
            } else if (idleVentFrame >= 0) {
                const Sheet fx = sprites.fxSheet(sp.key, "idle_vent");
                // 288px 셀을 256px 본체의 좌상단 (-16, -24)에 놓는다.
                drawSheetFrame(g, fx, idleVentFrame, x, cyAdj - drawH * (8.f / 256.f),
                               drawH * (288.f / 256.f), flip, 0.f, alpha);
            }
        }
        orbit(true);
        if (cleave && effects) drawCleaveCast("_cast_front");
        if (soulfire && skillAnim && effects)
            drawSoulfireBodyVfx(g, d, sprites, sp.key, frame, {x, cyAdj}, drawH, flip, true);
        return;
    }

    if (!showBody) return;
    if (!preview) markRegion(RectF(x - height * 0.5f, y - height * 0.5f, height, height), 4.f);
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

void Renderer::drawEnemyHealth(Graphics& g, const EnemyHealthLabel& label) {
    const Enemy& e = *label.enemy;
    constexpr float width = 56.f;
    bar(g, RectF(e.pos.x - width * 0.5f, label.y, width, 7.f), e.hp / e.maxHp, 0xFFFF6B6B);
    text(g, "Lv." + std::to_string(e.level), e.pos.x, label.y - 18.f, 12.f, 0xFFE0D0FF, 1, false, true);
}

void Renderer::drawEnemy(Graphics& g, const Enemy& e, const Sprites& sprites, float time, float charScale,
                         const AnimState* forcedState, std::vector<EnemyHealthLabel>* deferredHealth) {
    const EnemyDef& edef = enemyDef(e.kind);
    // v1.5: 적 그림은 왼쪽을 보고 그려져 있다. 드래곤(오른쪽 기준)과 반대라 전에는 거꾸로 걸었다.
    bool flip = edef.artFacesRight ? (e.facing < 0.f) : (e.facing > 0.f);
    // v1.5: 맞았을 때 번쩍임이 너무 밝다는 이야기가 있어 낮췄다(0.7 -> 0.3).
    float flash = e.hitFlash > 0.f ? 0.3f : 0.f;
    float bob = std::sin(time * 6.f + e.id) * 2.f;
    // 2026-11-25 분열 연출: 분열체는 포물선으로 떠서 날아가고(그림만 위로), 본체는 움찔 부푼다.
    float hopLift = 0.f;
    if (e.hopping()) {
        const float p = 1.f - e.hopLeft / std::max(0.01f, e.hopTime);
        hopLift = std::sin(3.14159265f * p) * cfg::kSplitHopHeight * charScale;
        bob = 0.f;
    }
    // 분열 그림(split.png)이 있으면 그 동작을 재생하고, 없으면 크기만 움찔 키운다.
    const float splitAge = e.squashLeft > 0.f ? cfg::kSplitSquashTime - e.squashLeft : -1.f;
    const Sheet splitBody = splitAge >= 0.f ? sprites.skillSheet("enemy:" + edef.key, "split") : Sheet{};
    const bool splitArt = splitBody.bmp && splitAge < cfg::kSplitBodyTime;
    const float squash = (e.squashLeft > 0.f && !splitBody.bmp)
        ? 1.f + 0.18f * std::sin(3.14159265f * (1.f - e.squashLeft / cfg::kSplitSquashTime)) : 1.f;
    bob -= hopLift;
    float x = e.pos.x;
    float y = e.pos.y + bob;
    const EnemyDef& ed = edef;
    bool isBoss = ed.role == EnemyRole::Boss;
    float height = e.radius * 2.6f * charScale * squash;
    // #49: 그림(시트·한 장 그림)은 발을 판정 원의 아래 끝(pos.y + 반지름)에 맞춘다. 드래곤과 같은 까닭이다.
    const float artY = e.pos.y + e.radius + bob - (240.f / 256.f - 0.5f) * height;

    SolidBrush sh(Color(static_cast<BYTE>(hopLift > 0.f ? 40 : 70), 0, 0, 0));  // 떠 있으면 그림자가 옅다
    g.FillEllipse(&sh, x - e.radius, e.pos.y + e.radius * 0.9f, e.radius * 2.f, e.radius * 0.5f);

    if (isBoss && e.phase2) {
        float pulse = 0.5f + 0.5f * std::sin(time * 6.f);
        fillCircle(g, x, y, e.radius * 1.45f, Color(static_cast<BYTE>(40 + 40 * pulse), 255, 60, 40));
    }
    if (e.castTimer > 0.f) {
        strokeCircle(g, x, y, e.radius * 1.25f, Color(200, 255, 90, 60), 2.f);
    }

    // v0.4: 적도 시트 애니메이션을 쓴다. 상태는 움직임·예고·피격으로 정한다.
    AnimState st = AnimState::Idle;
    if (e.hitFlash > 0.f) st = AnimState::Hit;
    else if (e.castTimer > 0.f) st = AnimState::Attack;
    else if (e.stun <= 0.f) st = AnimState::Move;
    if (forcedState) st = *forcedState;
    const Sheet sh2 = sprites.sheet("enemy:" + ed.key, st);
    const bool drawnBySheet = sh2.bmp != nullptr;
    const AnimDef& ad = ed.anims[static_cast<int>(st)];
    int frame = drawnBySheet ? animFrame(time, ad.fps, sh2.frames, ad.loop) : 0;
    // T8: hit_frame 을 적은 적은 예고가 끝나는 순간 그 칸이 보이도록 공격 시트를 맞춰 재생한다.
    // 예고가 짧아 앞 칸을 다 보여 줄 수 없으면 그만큼 앞 칸을 건너뛴다.
    if (drawnBySheet && st == AnimState::Attack && !forcedState && ed.attackHitFrame > 0 && e.castTimer > 0.f) {
        const float hitAt = static_cast<float>(ed.attackHitFrame - 1) / std::max(1.f, ad.fps);
        const float animT = std::max(0.f, hitAt - e.castTimer);
        frame = std::clamp(static_cast<int>(animT * ad.fps), 0, sh2.frames - 1);
    }
    Bitmap* bmp = drawnBySheet ? nullptr : sprites.get(isBoss ? "boss_guardian" : "enemy_imp");
    const float visibleHeight = drawnBySheet
        ? height * ad.renderScale * (sh2.visibleHeight ? sh2.visibleHeight[frame] : 1.f)
        : (bmp ? height : e.radius * 2.f);
    const SoulfireHit& fire = e.soulfireHit;
    float fireSize = 0.f, fireY = 0.f;
    int pixelW = 0, pixelH = 0;
    if (e.alive && fire.intensity > 0.f) {
        frameDeviceSize(g, visibleHeight, visibleHeight, pixelW, pixelH);
        const float release = std::clamp(fire.releaseAge / 0.10f, 0.f, 1.f);
        const float attackIntensity = fire.releaseAge > 0.f ? fire.releaseStart : fire.intensity;
        const float scale = std::clamp(0.60f + 0.40f * attackIntensity, 0.80f, 1.f) * (1.f - 0.14f * release);
        fireSize = visibleHeight * 0.92f * scale;
        const float foot = e.pos.y + e.radius + bob;
        fireY = foot - visibleHeight * 0.43f + fireSize * (0.5f - 104.f / 128.f);
    }
    const auto drawAttachedFire = [&](bool front) {
        if (fireSize <= 0.f || (front && pixelH < 40)) return;
        const float cy = fireY - (front ? 0.f : visibleHeight * 0.13f);
        if (!g.IsVisible(RectF(x - fireSize * 0.5f, cy - fireSize * 0.5f, fireSize, fireSize))) return;
        const Sheet fx = sprites.fxSheet("revenant", front ? "soulfire_hit_front" : "soulfire_hit_rear");
        if (!fx.bmp) return;
        const int fxFrame = animFrame(fire.loopAge + (e.id % 8) * 0.1f, 10.f, fx.frames, true);
        const FrameCache::Source cell{fx.bmp, fxFrame * fx.frameW, 0, fx.frameW, fx.frameH};
        drawFrameCached(g, cell, x, cy, fireSize, fireSize, e.facing < 0.f, 0.f, fire.intensity);
    };

    drawAttachedFire(false);
    // 착지 점액(분열체). 본체 폴더의 fx_split_land.png, 없으면 초록 고리.
    if (e.landLeft > 0.f) {
        const float lp = 1.f - e.landLeft / cfg::kSplitLandTime;
        const Sheet land = sprites.fxSheet("enemy:" + enemyDef(e.parentKind).key, "split_land");
        const float foot = e.pos.y + e.radius;
        if (land.bmp) {
            const int lf = std::clamp(static_cast<int>(lp * static_cast<float>(land.frames)), 0, land.frames - 1);
            const float lh = height * ad.renderScale;  // 착지점 (128, 200) 이 발에 오게
            drawSheetFrame(g, land, lf, x, foot - (200.f / 256.f - 0.5f) * lh, lh, false, 0.f, 1.f);
        } else {
            fillCircle(g, x, foot - e.radius * 0.5f, 36.f * (0.6f + 0.4f * lp), col(0xFF8CFF8C, 0.25f * (1.f - lp)));
        }
    }
    if (drawnBySheet && splitArt) {
        const AnimDef& idleDef = ed.anims[static_cast<int>(AnimState::Idle)];
        const float bodyH = height * idleDef.renderScale;
        const int sf = std::clamp(static_cast<int>(splitAge / cfg::kSplitBodyTime * static_cast<float>(splitBody.frames)), 0,
                                  splitBody.frames - 1);
        drawSheetFrame(g, splitBody, sf, x, artY - (240.f / 256.f - 0.5f) * height * (idleDef.renderScale - 1.f), bodyH,
                       flip, flash, 1.f);
    } else if (drawnBySheet) {
        const float cyAdj = artY - (240.f / 256.f - 0.5f) * height * (ad.renderScale - 1.f);
        drawSheetFrame(g, sh2, frame, x, cyAdj, height * ad.renderScale, flip, flash, 1.f);
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

    // 분열 점액 터짐(본체 몸 가운데, 0.40초). 칸 512 = 본체 칸 256 의 2배, Codex 미리보기 표시 배율 0.65.
    if (splitAge >= 0.f) {
        const Sheet burst = sprites.fxSheet("enemy:" + edef.key, "split");
        if (burst.bmp) {
            const AnimDef& idleDef = ed.anims[static_cast<int>(AnimState::Idle)];
            const float bodyH = height * idleDef.renderScale;
            const int bf = std::clamp(static_cast<int>(splitAge / cfg::kSplitSquashTime * static_cast<float>(burst.frames)), 0,
                                      burst.frames - 1);
            const float bodyCenterY = e.pos.y + e.radius + bob - bodyH * 0.40f;
            drawSheetFrame(g, burst, bf, x, bodyCenterY, bodyH * 2.f * 0.65f, flip, 0.f, 1.f);
        }
    }
    drawAttachedFire(true);

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
        const EnemyHealthLabel label{&e, barY};
        if (deferredHealth) deferredHealth->push_back(label);
        else drawEnemyHealth(g, label);
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
    drawSoulfire(g, w, sprites, SoulfirePass::Ground);

    // 적 공격 예고
    for (const AttackZone& z : w.zones()) {
        float p = std::clamp(z.t / std::max(0.01f, z.windup), 0.f, 1.f);
        BYTE fillA = static_cast<BYTE>(35 + 70 * p);
        if (z.shape == ZoneShape::Fan) {
            // T8 휘두르기: 부채꼴. 안쪽 채움이 꼭짓점에서 바깥으로 자란다
            const Vec2 axis = z.b - z.a;
            const float len = axis.length();
            const float mid = std::atan2(axis.y, axis.x) * 180.f / 3.14159265f;
            const float start = mid - z.arc * 0.5f;
            SolidBrush fb(Color(fillA, 255, 60, 40));
            g.FillPie(&fb, z.a.x - len, z.a.y - len, len * 2.f, len * 2.f, start, z.arc);
            const float r2 = len * p;
            if (r2 > 1.f) {
                SolidBrush ib(Color(80, 255, 40, 20));
                g.FillPie(&ib, z.a.x - r2, z.a.y - r2, r2 * 2.f, r2 * 2.f, start, z.arc);
            }
            Pen pen(Color(230, 255, 80, 60), 2.f);
            g.DrawPie(&pen, z.a.x - len, z.a.y - len, len * 2.f, len * 2.f, start, z.arc);
        } else if (z.shape == ZoneShape::Circle) {
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

    // T8 적 투사체. 적 폴더의 projectile.png(오른쪽으로 날아가는 그림)가 있으면 그것을, 없으면 빛나는 구슬을 그린다.
    for (const EnemyShot& shot : w.enemyShots()) {
        const EnemyDef& sd = enemyDef(shot.kind);
        const Sheet ps = sprites.fxSheet("enemy:" + sd.key, "projectile");
        if (ps.bmp) {
            const float size = std::max(32.f, shot.radius * 5.f);
            const int pf = animFrame(shot.t, 14.f, ps.frames, true);
            const FrameCache::Source cell{ps.bmp, pf * ps.frameW, 0, ps.frameW, ps.frameH};
            const float ang = std::atan2(shot.dir.y, shot.dir.x) * 180.f / 3.14159265f;
            const GraphicsState gs = g.Save();
            g.TranslateTransform(shot.pos.x, shot.pos.y);
            g.RotateTransform(ang);
            drawFrameCached(g, cell, 0.f, 0.f, size, size, false, 0.f, 1.f);
            g.Restore(gs);
        } else {
            const Vec2 tail = shot.pos - shot.dir * shot.radius * 3.f;
            Pen trail(Color(140, 255, 120, 70), shot.radius * 1.2f);
            trail.SetStartCap(LineCapRound);
            trail.SetEndCap(LineCapRound);
            g.DrawLine(&trail, tail.x, tail.y, shot.pos.x, shot.pos.y);
            fillCircle(g, shot.pos.x, shot.pos.y, shot.radius, Color(235, 255, 110, 60));
            fillCircle(g, shot.pos.x, shot.pos.y, shot.radius * 0.5f, Color(255, 255, 230, 180));
        }
    }

    // v0.8: 캐릭터 보기 크기. 그림만 커지고 충돌·사거리 판정은 그대로다.
    const float cs = game.charScale();
    curseBadges_.clear();
    for (auto& entry : curseFlashes_) entry.second.seen = false;
    const bool layeredFlames = std::any_of(w.dots().begin(), w.dots().end(),
                                         [](const DotZone& zone) { return !zone.flames.empty(); });
    std::vector<EnemyHealthLabel> health;
    const bool cleaveImpact = std::any_of(w.fx().begin(), w.fx().end(),
                                         [](const Fx& f) { return f.type == FxType::CleaveImpact; });
    const bool deferHealth = layeredFlames || cleaveImpact;
    if (deferHealth) health.reserve(w.enemies().size());
    if (cleaveImpact) drawCleaveImpacts(g, w, sprites, false);
    if (layeredFlames) {
        drawSoulfire(g, w, sprites, SoulfirePass::Rear);
    }
    for (const Enemy& e : w.enemies()) drawEnemy(g, e, sprites, time, cs, nullptr, deferHealth ? &health : nullptr);
    if (layeredFlames) drawSoulfire(g, w, sprites, SoulfirePass::Front);
    if (cleaveImpact) drawCleaveImpacts(g, w, sprites, true);
    for (const EnemyHealthLabel& label : health) drawEnemyHealth(g, label);

    // v1.6: 본체 뒤 효과(fx_front=0)를 드래곤보다 먼저 그린다.
    // 지금은 fx= 를 적은 스킬이 하나도 없어서 이 층에 걸리는 효과가 없다 — 화면은 그대로다.
    drawProcession(g, w.dragon(), sprites, cs, false);
    drawFx(g, w, sprites, dragonSp, cs, false);
    drawBreath(g, w.dragon(), sprites, cs);
    drawSoulfireBreath(g, w, sprites);
    drawSoulfireImpact(g, w, sprites);
    if (game.hasDragon()) warmBreathCache(g, w, sprites, cs);

    // T7: 필드 보스 파티 동료(자동). 조작 드래곤보다 먼저 그려 뒤에 선다.
    for (const PartyMember& m : w.party()) {
        const DragonActor& a = m.actor;
        if (!a.alive) continue;
        drawBreath(g, a, sprites, cs);
        const float ah = cfg::kDragonRadius * 3.f * cs;
        const float acy = a.pos.y + cfg::kDragonRadius - (240.f / 256.f - 0.5f) * ah;
        drawDragon(g, a, sprites, time, a.pos.x, acy, ah, false);
        const float aTop = acy - ah * 0.5f;
        bar(g, RectF(a.pos.x - 30.f, aTop - 18.f, 60.f, 7.f), a.hp / std::max(1.f, a.stats.maxHp), kHpC);
        text(g, "Lv." + std::to_string(a.level) + " 자동", a.pos.x, aTop - 36.f, 12.f, 0xFFBFD4FF, 1, true, true);
    }

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
    drawProcession(g, d, sprites, cs, true);

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

void Renderer::drawSoulfire(Graphics& g, const World& w, const Sprites& sprites, SoulfirePass pass) {
    const SpeciesDef& species = speciesDef(w.dragon().species);
    std::array<Sheet, kSoulfireFlameVariants> sheets{};
    bool loaded = false;
    const InterpolationMode normalInterpolation = g.GetInterpolationMode();
    // Flame growth changes the destination size each tick. Reuse source cells
    // rather than rebuilding and prefetching scaled bitmaps for each size.
    const auto drawFlameCell = [&](const FrameCache::Source& source, float x, float y,
                                   float width, float height, bool flip, float alpha) {
        if (alpha <= 0.004f || !g.IsVisible(RectF(x - width * 0.5f, y - height * 0.5f, width, height))) return;
        const FrameCache::Source frame = frameCache_.visibleCell(source);
        if (frame.w <= 0 || frame.h <= 0) return;
        // A cropped cell retains the full cell's scale and ground anchor.
        const bool original = frame.bmp == source.bmp;
        const float sx = width / source.w, sy = height / source.h;
        const float offsetX = original ? 0.f : (frame.x + frame.w * 0.5f - source.w * 0.5f) * sx;
        const float offsetY = original ? 0.f : (frame.y + frame.h * 0.5f - source.h * 0.5f) * sy;
        drawFrameDirect(g, frame, x + (flip ? -offsetX : offsetX), y + offsetY,
                        frame.w * sx, frame.h * sy, flip, 0.f, alpha);
    };
    for (const DotZone& zone : w.dots()) {
        if (zone.flames.empty()) continue;
        if (!loaded) {
            g.SetInterpolationMode(InterpolationModeBilinear);
            for (int variant = 0; variant < kSoulfireFlameVariants; ++variant)
                sheets[variant] = sprites.fxSheet(species.key, soulfireLayerKey(variant, pass));
            loaded = true;
        }
        const float age = zone.duration - zone.remaining;
        if (pass == SoulfirePass::Ground && age > 0.f && zone.remaining > 0.f) {
            // Stretch the low flame's ground texture across adjacent trail samples.
            // Keep the narrow painted band around its (64, 108) ground anchor.
            const Sheet& embers = sheets[3];
            if (embers.bmp && embers.frames > 0) {
                const int frame = animFrame(age, 12.f, embers.frames, true);
                const int columns = embers.columns > 0 ? embers.columns : embers.frames;
                const FrameCache::Source band{
                    embers.bmp, embers.frameW * (frame % columns) + 38,
                    embers.frameH * (frame / columns) + 102, 52, 12};
                const float width = zone.radius * 0.60f;
                const float height = zone.flameHeight * 0.22f;
                const float fade = std::min({age / 0.15f, zone.remaining / 0.54f, 1.f});
                drawFlameCell(band, zone.pos.x, zone.pos.y, width, height, false, 0.52f * fade);
            }
        }
        for (const SoulfireFlame& flame : zone.flames) {
            const float local = age - flame.delay;
            if (local < 0.f) continue;
            constexpr float eruptionDuration = 1.2f;
            const bool eruption = flame.variant == 4;
            if (eruption && local >= eruptionDuration) continue;
            const float progress = eruption ? local / eruptionDuration : std::fmod(local, flame.cycle) / flame.cycle;
            const float cycleEnvelope = std::min({progress / 0.18f, (1.f - progress) / 0.21f, 1.f});
            const float flicker = eruption ? cycleEnvelope : 0.40f + 0.60f * cycleEnvelope;
            const float envelope = flicker * std::min({local / 0.13f, zone.remaining / 0.44f, 1.f});
            if (envelope <= 0.f) continue;
            const Sheet& sheet = sheets[flame.variant];
            if (!sheet.bmp || sheet.frames <= 0 || sheet.frameH <= 0) continue;
            const int frame = std::min(sheet.frames - 1, static_cast<int>(progress * sheet.frames));
            const int columns = sheet.columns > 0 ? sheet.columns : sheet.frames;
            const FrameCache::Source source{sheet.bmp, sheet.frameW * (frame % columns),
                                            sheet.frameH * (frame / columns), sheet.frameW, sheet.frameH};
            const Vec2 anchor = zone.pos + flame.offset * zone.radius;
            const float height = zone.flameHeight * flame.size * (0.68f + 0.32f * envelope);
            const float opacity = pass == SoulfirePass::Front ? 0.62f : 0.79f;
            const float width = height * sheet.frameW / sheet.frameH;
            drawFlameCell(source, anchor.x, anchor.y + (0.5f - 108.f / 128.f) * height,
                          width, height, flame.flip, opacity * envelope);
        }
    }
    if (loaded) g.SetInterpolationMode(normalInterpolation);
}

void Renderer::drawSoulfireImpact(Graphics& g, const World& w, const Sprites& sprites) {
    const DragonActor& dragon = w.dragon();
    if (!dragon.skillCast || !dragon.skillCast->soulfireCastId) return;
    const SkillCast& cast = *dragon.skillCast;
    const float age = cast.elapsed - cast.fxTime;
    constexpr float impactTail = 0.18f;
    const float release = std::max(0.f, age - kSoulfireBreathDuration);
    if (release >= impactTail) return;
    const auto contact = w.soulfireBeam(true);
    if (!contact) return;

    const SpeciesDef& species = speciesDef(dragon.species);
    const Sheet impact = sprites.fxSheet(species.key, "soulfire_impact");
    // V5 starts with sparks and expands to a full plume in cell 6.
    // Oscillate only through the mature cells, keeping the same contact anchor.
    constexpr int firstFullFrame = 6;
    const int span = std::max(1, impact.frames - 1 - firstFullFrame);
    const float activeAge = std::min(age, kSoulfireBreathDuration);
    const int tick = static_cast<int>(activeAge * 20.f) % (2 * span);
    const int frame = firstFullFrame + std::min(tick, 2 * span - tick);
    const float life = 1.f - release / impactTail;
    const float alpha = life * life * (3.f - 2.f * life);
    const SkillDef& skill = species.skills[cast.skill];
    const float h = w.skillRadius(skill) * skill.soulfireImpactScale * (0.86f + 0.14f * life);
    const float sign = cast.facingLeft ? -1.f : 1.f;
    drawSheetFrame(g, impact, frame,
                   contact->ground.x + sign * (0.5f - 440.f / 1024.f) * h,
                   contact->ground.y + (0.5f - 700.f / 1024.f) * h, h, cast.facingLeft, 0.f, alpha);
}

void Renderer::drawSoulfireBreath(Graphics& g, const World& w, const Sprites& sprites) {
    const auto beam = w.soulfireBeam();
    if (!beam) return;
    const DragonActor& d = w.dragon();
    const SkillCast& cast = *d.skillCast;
    const SpeciesDef& species = speciesDef(d.species);
    const SkillDef& skill = species.skills[cast.skill];
    const Sheet sheet = sprites.fxSheet(species.key, "soulfire_beam");
    if (!sheet.bmp || sheet.frames <= 0) return;

    const float body = cfg::kDragonRadius * 3.f * w.sizeScale() * skill.motionAnim.renderScale;
    const float sign = cast.facingLeft ? -1.f : 1.f;
    // The attack cells share one jaw pose. Seat the bright core inside its opening.
    const Vec2 mouth = beam->mouth + Vec2{sign * body / 256.f, 3.f * body / 256.f};
    const Vec2 aim = beam->ground - mouth;
    const float angle = std::atan2(aim.y, aim.x) * 180.f / kPi;
    const float height = aim.length() / 0.75f;
    const bool left = cast.facingLeft;
    constexpr float centerOffset = 0.5f - 160.f / 1024.f;
    const GraphicsState state = g.Save();
    g.TranslateTransform(mouth.x, mouth.y);
    g.RotateTransform(angle);
    g.ScaleTransform(1.f, skill.soulfireWidth);
    g.RotateTransform(left ? -125.f : -55.f);
    const float age = cast.elapsed - cast.fxTime;
    drawSheetFrame(g, sheet, animFrame(age, 20.f, sheet.frames, true),
                   (left ? -1.f : 1.f) * centerOffset * height, centerOffset * height, height, left, 0.f, 0.83f);
    g.Restore(state);

    const Vec2 normal{-aim.y, aim.x};
    for (int i = 0; i < 12; ++i) {
        const float phase = std::fmod(age * (1.7f + 0.23f * (i % 4)) + i * 0.381966f, 1.f);
        const float length = 0.035f + 0.017f * (i % 3);
        const float spread = 0.045f * phase * skill.soulfireWidth;
        const float curl = std::sin(phase * 12.f + i * 2.4f);
        const Vec2 tip = mouth + aim * phase + normal * (spread * curl);
        const Vec2 tail = tip - aim * std::min(phase, length) - normal * (spread * 0.28f);
        const float fade = std::min({phase * 12.f, (1.f - phase) * 8.f, 1.f});
        Pen streak(Color(static_cast<BYTE>(165.f * fade), 159, 255, 230), std::max(0.65f, body * 0.012f));
        g.DrawLine(&streak, tail.x, tail.y, tip.x, tip.y);
    }

    const float ignition = std::clamp(age / .065f, 0.f, 1.f);
    const float fade = std::clamp((kSoulfireBreathDuration - age) / .17f, 0.f, 1.f);
    const float pulse = .5f + .5f * std::cos(age * 11.f);
    const float extent = body * (.34f + ignition * .31f);
    markRegion(RectF(mouth.x - extent, mouth.y - extent, extent * 2.f, extent * 2.f), 3.f);
    const auto emission = g.Save();
    g.TranslateTransform(mouth.x, mouth.y);
    g.RotateTransform(angle);
    g.ScaleTransform(1.f, sign);
    const Sheet crown = sprites.fxSheet(species.key, "soulfire_crown");
    if (crown.bmp)
        drawSheetFrame(g, crown, animFrame(age, 20.f, crown.frames, true),
                       -.06f * extent, 0.f, extent, false, 0.f, fade * (.76f + .24f * pulse));
    const Sheet fragments = sprites.fxSheet(species.key, "soulfire_fragments");
    if (fragments.bmp)
        drawSheetFrame(g, fragments, animFrame(age, 20.f, fragments.frames, true),
                       extent * (.18f + .12f * pulse), 0.f, extent * .93f, false, 0.f,
                       fade * ignition * (.48f + .34f * (1.f - pulse)));
    const Sheet core = sprites.fxSheet(species.key, "soulfire_core");
    if (core.bmp)
        drawSheetFrame(g, core, animFrame(age, 20.f, core.frames, true), 0.f, 0.f,
                       body * (.28f + .10f * pulse), false, 0.f, fade * (.60f + .40f * pulse));
    g.Restore(emission);
}

void Renderer::drawDotZones(Graphics& g, const World& w, const Sprites& sprites, float time) {
    const SpeciesDef& species = speciesDef(w.dragon().species);
    const uint32_t spColor = species.color;
    // 지속 피해 장판
    for (const DotZone& z : w.dots()) {
        if (z.soulfireCastId) continue;
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
                // 2026-11-20: 효과·장판·투사체·숨결은 누를 것이 없으므로 마우스를 받지 않는다(클릭 통과).
                drawSheetFrame(g, sheet, animFrame(z.duration - z.remaining, skill.fxAnim.fps, sheet.frames, false),
                               x, y, h, false, 0.f, skill.fxAlpha);
                continue;
            }
            if (skill.fxZoneSpriteOnly) continue;
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

void Renderer::drawProcession(Graphics& g, const DragonActor& dragon, const Sprites& sprites,
                              float scale, bool inFront) {
    if (!dragon.alive || !dragon.procession) return;
    const ProcessionCast& cast = *dragon.procession;
    const float body = cfg::kDragonRadius * 3.f * scale;
    const float direction = cast.facingLeft ? -1.f : 1.f;
    const auto drawLayer = [&](const char* key, float local, float duration, Vec2 at,
                               float height, float opacity, bool left, bool loop = false) {
        if (local < 0.f || local >= duration) return;
        const Sheet sheet = sprites.fxSheet("revenant", key);
        if (!sheet.bmp) return;
        const float fade = loop ? 1.f : std::min(1.f, (duration - local) / 0.10f);
        const int frame = animFrame(local, sheet.frames / (loop ? 0.48f : duration), sheet.frames, loop);
        drawSheetFrame(g, sheet, frame, at.x, at.y, height, left, 0.f, opacity * fade);
    };
    if (!inFront) {
        drawLayer("procession_ritual", cast.elapsed, 0.72f,
                  cast.origin + Vec2{0.f, body * 0.24f}, body * 1.55f, 0.72f, cast.facingLeft);
    } else {
        drawLayer("procession_soul_eruption", cast.elapsed - 0.48f, 0.52f,
                  cast.origin + Vec2{body * 0.12f * direction, -body * 0.24f}, body, 0.73f, cast.facingLeft);
        drawLayer("procession_spectral_crown", cast.elapsed - 0.70f, 0.80f,
                  cast.origin + Vec2{body * 0.26f * direction, -body * 0.57f}, body * 0.60f, 0.78f, cast.facingLeft);
    }
    for (const ProcessionWave& wave : cast.waves) {
        if (wave.impacted) {
            if (inFront)
                drawLayer("procession_final_impact", wave.age, 0.44f, wave.pos, body * 1.65f, 0.68f, wave.facingLeft);
            else
                drawLayer("procession_aftermath", wave.age, ProcessionCast::aftermath,
                          wave.pos, body * 1.55f, 0.52f, wave.facingLeft);
        } else if (inFront) {
            constexpr float emergenceLead = 0.12f;
            drawLayer("procession_spirit_charge", wave.age - emergenceLead,
                      cast.flightLimit,
                      wave.pos, body * 1.45f, 0.75f, wave.facingLeft, true);
        } else {
            drawLayer("procession_army_emergence", wave.age, 0.31f, wave.pos, body * 1.20f, 0.80f, wave.facingLeft);
        }
    }
}

void Renderer::drawProjectiles(Graphics& g, const World& w, const Sprites& sprites,
                               const SpeciesDef& dragonSp, float cs) {
    const uint32_t spColor = dragonSp.color;
    // 투사체
    for (const Projectile& p : w.projectiles()) {
        if (p.heatBlade) {
            const Vec2 aim = p.heatBlade->aim;
            const bool left = aim.x < 0.f;
            const auto& atlas = heatBlade::atlases[left ? heatBlade::flightLeft : heatBlade::flightRight];
            const Sheet sheet = sprites.fxSheet("steel_dragon", atlas.path);
            const float scale = atlas.scale * p.heatBlade->scale;
            const auto transform = g.Save();
            g.TranslateTransform(p.pos.x, p.pos.y);
            g.RotateTransform(std::atan2(aim.y, aim.x) * 180.f / kPi - (left ? 180.f : 0.f));
            drawSheetFrame(g, sheet, animFrame(p.t, static_cast<float>(atlas.fps), atlas.frames, true),
                           (atlas.width * .5f - atlas.anchorX) * scale,
                           (atlas.height * .5f - atlas.anchorY) * scale,
                           atlas.height * scale, false, 0.f, 1.f);
            g.Restore(transform);
            continue;
        }
        if (p.steelBasic) {
            const Sheet sheet = sprites.fxSheet(dragonSp.key, "basic_projectile");
            if (sheet.frames == 8) {
                const Vec2 aim = p.steelBasic->aim;
                const bool left = aim.x < 0.f;
                const float angle = std::atan2(aim.y, aim.x) * 180.f / kPi - (left ? 180.f : 0.f);
                const float height = 18.f * p.steelBasic->scale;
                // 원본의 탄두 끝 x=990, 중심선 y=144를 월드 투사체 위치에 고정한다.
                const float tip = (990.f - 512.f) / 288.f * height;
                const auto previous = g.Save();
                g.TranslateTransform(p.pos.x, p.pos.y);
                g.RotateTransform(angle);
                drawSheetFrame(g, sheet, animFrame(p.t, kSteelBasicFps, sheet.frames, true),
                               left ? tip : -tip, 0.f, height, left, 0.f, 1.f);
                g.Restore(previous);
                continue;
            }
        }
        // 2026-11-17: fx= 를 적은 스킬이 쏜 발사체는 그 시트를 **현재 위치에** 그린다.
        // Fx 를 쓰지 않는 이유는 Fx 의 시작점·끝점이 고정이라 날아가는 것을 못 따라가서다.
        // 시트가 없거나 스킬을 못 찾으면 전과 똑같이 동그라미로 그린다.
        const SkillDef* ps = (p.skill >= 0 && p.skill < static_cast<int>(dragonSp.skills.size()))
                                 ? &dragonSp.skills[static_cast<size_t>(p.skill)] : nullptr;
        if (p.boneShot && ps) {
            const Sheet sheet = sprites.fxSheet(dragonSp.key, ps->fx);
            if (sheet.bmp) {
                const Vec2 aim = p.boneShot->aim;
                const bool left = aim.x < 0.f;
                const float angle = std::atan2(aim.y, aim.x) * 180.f / kPi - (left ? 180.f : 0.f);
                const float height = cfg::kDragonRadius * 3.f * p.boneShot->scale;
                // 셀의 뼈 끝이 월드 접촉점과 일치하도록 회전 중심을 잡는다.
                const float tipX = 54.667f / 256.f * height * (left ? -1.f : 1.f);
                const float tipY = 5.167f / 256.f * height;
                const auto previous = g.Save();
                g.TranslateTransform(p.pos.x, p.pos.y);
                g.RotateTransform(angle);
                drawSheetFrame(g, sheet, animFrame(p.t, ps->fxAnim.fps, sheet.frames, true),
                               -tipX, -tipY, height, left, 0.f, 1.f);
                g.Restore(previous);
                continue;
            }
        }
        if (p.cleave && ps) {
            const float height = cfg::kDragonRadius * 3.f * p.cleave->scale * ps->fxScale;
            for (const char* layer : {"fragments", "membrane", "edge"}) {
                const bool fragments = std::string_view(layer) == "fragments";
                if (!fragments && p.t >= kCleaveFlight) continue;
                const Sheet sheet = sprites.fxSheet(dragonSp.key, ps->fx + "_" + layer);
                if (!sheet.bmp) continue;
                drawSheetFrame(g, sheet, animFrame(p.t, ps->fxAnim.fps, sheet.frames, false),
                               p.pos.x, p.pos.y, height, p.cleave->direction < 0.f, 0.f, 1.f);
            }
            continue;
        }
        if (ps && !ps->fx.empty()) {
            Sheet sh = sprites.fxSheet(dragonSp.key, ps->fx);
            if (sh.bmp && sh.frames > 0 && sh.frameH > 0) {
                float h = cfg::kDragonRadius * 3.f * cs * ps->fxScale * ps->fxAnim.renderScale;
                if (h > 0.5f) {
                    // 날아가는 쪽을 본다. 목표가 사라졌으면 그냥 오른쪽으로 둔다.
                    bool flip = false;
                    for (const Enemy& e : w.enemies())
                        if (e.id == p.targetId) { flip = e.pos.x < p.pos.x; break; }
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
    frameCache_.beginFrame(sprites.generation());
    drawEnemy(g, enemy, sprites, time, charScale, &state);
}

void Renderer::drawCleaveImpacts(Graphics& g, const World& w, const Sprites& sprites, bool front) {
    const SpeciesDef& species = speciesDef(w.dragon().species);
    for (const Fx& f : w.fx()) {
        if (f.type != FxType::CleaveImpact || f.t < 0.f) continue;
        const Sheet sheet = sprites.fxSheet(species.key, f.key + (front ? "_hit_front" : "_hit_rear"));
        drawSheetFrame(g, sheet, animFrame(f.t, kCleaveImpactFps, sheet.frames, false),
                       f.a.x, f.a.y, f.radius * 1.55f, f.b.x < f.a.x, 0.f, 1.f);
    }
}

void Renderer::drawAnimationPreview(Graphics& g, const World& w, const Sprites& sprites,
                                    float time, bool body, bool effects, float charScale, bool cleaveTarget) {
    frameCache_.beginFrame(sprites.generation());
    const DragonActor& d = w.dragon();
    const SpeciesDef& species = speciesDef(d.species);
    if (effects) {
        drawDotZones(g, w, sprites, time);
        drawSoulfire(g, w, sprites, SoulfirePass::Ground);
        drawSoulfire(g, w, sprites, SoulfirePass::Rear);
        drawSoulfireBreath(g, w, sprites);
        drawProcession(g, d, sprites, charScale, false);
        drawFx(g, w, sprites, species, charScale, false);
        drawBreath(g, d, sprites, charScale);
    }
    if (cleaveTarget) {
        if (effects) drawCleaveImpacts(g, w, sprites, false);
        if (body) {
            // Suppress the HUD in the capture while reusing the game's enemy renderer.
            std::vector<EnemyHealthLabel> health;
            for (const Enemy& e : w.enemies()) {
                Enemy target = e;
                target.stun = 0.f;
                const AnimState state = e.hitFlash > 0.f ? AnimState::Hit : AnimState::Idle;
                drawEnemy(g, target, sprites, time, charScale, &state, &health);
            }
        }
        if (effects) drawCleaveImpacts(g, w, sprites, true);
    }
    const bool steelMotionFx = species.key == "steel_dragon" &&
        (d.anim == AnimState::Idle || d.anim == AnimState::Move ||
         d.anim == AnimState::Fly || d.anim == AnimState::Attack);
    if (body || (effects && (steelMotionFx || (d.skillCast && (d.skillCast->soulfireCastId ||
            species.skills[d.skillCast->skill].spectralCleave))))) {
        const float height = cfg::kDragonRadius * 3.f * charScale;
        const float cy = d.pos.y + cfg::kDragonRadius - (240.f / 256.f - 0.5f) * height;
        drawDragon(g, d, sprites, time, d.pos.x, cy, height, false, body, effects);
    }
    if (effects) {
        drawSoulfire(g, w, sprites, SoulfirePass::Front);
        drawSoulfireImpact(g, w, sprites);
        drawProjectiles(g, w, sprites, species, charScale);
        drawFx(g, w, sprites, species, charScale, true);
        drawProcession(g, d, sprites, charScale, true);
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
    if (sk) {
        ad = sk->fxAnim;
        ad.fps = sk->fxPlaybackFps(f.key);
    }

    if (sk && (sk->coreOverheat || ((sk->boneShot || sk->furnaceBurst) && f.key == sk->fxImpact))) {
        drawSheetFrame(g, sh, animFrame(f.t, ad.fps, sh.frames, false),
                       f.a.x, f.a.y, f.radius, false, 0.f, 1.f);
        return true;
    }
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
    drawSheetFrame(g, sh, animFrame(f.t, ad.fps, sh.frames, ad.loop), cx, cy, h, /*flip=*/dirSign < 0.f, 0.f,
                   alpha);
    return true;
}

static void drawLineBlast(Graphics& g, const Fx& f, float fade) {
    const float halfWidth = f.radius * (1.f - 0.3f * fade);
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
        const float elapsed = std::max(0.f, cast.elapsed - cast.tailStart());
        keyAlpha = std::min(1.f, elapsed / endBlendSeconds);
        const float from = cast.tailStart() - BreathCast::impactTime - loopPhaseSeconds * 0.5f;
        blendKey = 4 + loopPhase(from + elapsed);
    }

    const GraphicsState state = g.Save();
    g.TranslateTransform(mouth.x, mouth.y);
    g.RotateTransform(std::atan2(cast.aim.y, cast.aim.x) * 57.2957795f);
    if (left) g.ScaleTransform(1.f, -1.f);
    const auto paste = [&](int k, float alpha) {
        Bitmap* frame = pack->frames[static_cast<size_t>(k)].get();
        drawFrameCached(g, FrameCache::Source{frame, 0, 0, BreathSheets::kWidth, BreathSheets::kHeight}, center, 0.f,
                        halfW * 2.f, halfH * 2.f, false, 0.f, alpha);
    };
    if (blendKey >= 0) paste(blendKey, 1.f - keyAlpha);
    paste(key, keyAlpha);
    g.Restore(state);
}

void Renderer::warmBreathCache(Graphics& g, const World& w, const Sprites& sprites, float charScale) {
    const DragonActor& d = w.dragon();
    if (!cfg::kFrameCache || !d.alive || d.breath) return;
    const SpeciesDef& sp = speciesDef(d.species);
    for (int s : w.slots()) {
        if (s < 0 || s >= static_cast<int>(sp.skills.size())) continue;
        const SkillDef& skill = sp.skills[static_cast<size_t>(s)];
        if (skill.layeredBreath.empty()) continue;
        const BreathSheets* pack = sprites.breathSheets(sp.key, skill.layeredBreath);
        if (!pack || !sprites.breathBody(sp, skill).bmp) continue;
        const float scale = breathScale(sp, skill, skill.range, charScale);
        if (scale <= 0.f) return;
        int devW = 0, devH = 0;
        frameDeviceSize(g, BreathSheets::kWidth * scale, BreathSheets::kHeight * scale, devW, devH);
        // 크기를 끄는 동안은 맡기지 않는다 — 같은 크기가 몇 프레임 이어진 뒤에 16장을 맡긴다.
        const float devScale = static_cast<float>(devW) / BreathSheets::kWidth;
        if (std::fabs(devScale - breathWarmScale_) > 1e-4f) {
            breathWarmScale_ = devScale;
            breathWarmFrames_ = 0;
        }
        if (++breathWarmFrames_ < cfg::kBreathCacheSettleFrames) return;
        for (const auto& frame : pack->frames)
            frameCache_.prefetch(FrameCache::Source{frame.get(), 0, 0, BreathSheets::kWidth, BreathSheets::kHeight},
                                 devW, devH, false, 0.f, g.GetInterpolationMode());
        return;
    }
}

void Renderer::drawFx(Graphics& g, const World& w, const Sprites& sprites, const SpeciesDef& sp, float charScale,
                      bool inFront) {
    for (const Fx& f : w.fx()) {
        if (f.t < 0.f || f.type == FxType::CleaveImpact) continue;
        if (f.inFront != inFront) continue;  // 이 층이 아니다
        if (f.key.rfind("heat_blade_", 0) == 0) {
            const int index = f.key == "heat_blade_launch" ? heatBlade::launch :
                f.key == "heat_blade_contact" ? heatBlade::contact : heatBlade::disappear;
            const auto& atlas = heatBlade::atlases[index];
            const Sheet sheet = sprites.fxSheet("steel_dragon", atlas.path);
            drawSheetFrame(g, sheet, animFrame(f.t, static_cast<float>(atlas.fps), atlas.frames, false),
                           f.a.x, f.a.y, f.radius, false, 0.f, 1.f);
            continue;
        }
        if (f.type == FxType::Hit && f.key == "basic_hit") {
            const Sheet sheet = sprites.fxSheet(sp.key, f.key);
            if (sheet.frames == 8) {
                drawSheetFrame(g, sheet, animFrame(f.t, kSteelBasicFps, sheet.frames, false),
                               f.a.x, f.a.y, f.radius, f.b.x < f.a.x, 0.f, 1.f);
                continue;
            }
        }
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
            const float iw = static_cast<float>(icon->GetWidth());
            const float ih = static_cast<float>(icon->GetHeight());
            const float fit = std::min((b.Width - 4.f) / iw, (b.Height - 4.f) / ih);
            const float w = iw * fit, h = ih * fit;
            g.DrawImage(icon, RectF(b.X + (b.Width - w) * 0.5f, b.Y + (b.Height - h) * 0.5f, w, h));
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
    // 2026-11-20: 궁극기 줄. 궁극기가 있거나 영혼을 모으는 종이면 보인다. 영혼을 쓰는 궁극기는 영혼 게이지,
    // 자원이 없는 궁극기는 재사용 대기 게이지를 보여 주고, 오른쪽에 궁극기 아이콘(클릭·키 5)을 둔다.
    const World& world = game.world();
    const int ultimateIndex = world.ultimateSkill();
    const SkillDef* ultimate = ultimateIndex >= 0 ? &sp.skills[static_cast<size_t>(ultimateIndex)] : nullptr;
    const bool soulSpecies = std::any_of(sp.skills.begin(), sp.skills.end(),
                                         [](const SkillDef& skill) { return skill.grantsSoul; });
    const bool showSouls = soulSpecies || (ultimate && ultimate->ultimateUsesSouls);
    const bool showUltimateRow = showSouls || ultimate != nullptr;
    // 탑 도전 버튼은 스킬 칸 아래 줄이다(접으면 숨긴다).
    const float expandedHeight = 292.f + (showUltimateRow ? 48.f : 0.f) + 30.f;
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

    // 제목 줄은 끌기 손잡이다. 2026-11-20: 접기 버튼을 맨 왼쪽으로 옮겼다(사용자 요청). 그 옆이 손잡이 줄무늬다.
    // 탑 도전 버튼은 제목 줄에서 빼서 스킬 칸 아래로 옮겼다.
    // v1.4: 전에는 ▦·≡ 같은 글자를 썼는데 폰트에 없어 깨져 보였다. 한글로 적는다.
    button(g, out, ctx, RectF(r.X + 4.f, r.Y + 4.f, 26.f, 22.f), game.panelCollapsed() ? "▴" : "▾",
           UiAction::TogglePanel);
    RectF handle(r.X + 32.f, r.Y, r.Width - 32.f - 124.f, 30.f);
    out.push_back({handle, UiAction::DragPanel, 0, true});
    markRegion(handle);
    button(g, out, ctx, RectF(r.GetRight() - 120.f, r.Y + 4.f, 56.f, 22.f), "관리", UiAction::OpenStable);
    button(g, out, ctx, RectF(r.GetRight() - 60.f, r.Y + 4.f, 56.f, 22.f), "메뉴", UiAction::ToggleMenu, 0, true,
           game.menuOpen());
    {
        bool hov = handle.Contains(PointF(ctx.mouseX, ctx.mouseY));
        for (int i = 0; i < 3; ++i)
            fillRound(g, RectF(r.X + 36.f, r.Y + 9.f + i * 4.f, 14.f, 2.f), 1.f,
                      Color(hov ? 220 : 120, 180, 190, 220));
    }
    if (game.panelCollapsed()) {
        const auto clip = g.Save();
        g.SetClip(RectF(r.X + 54.f, r.Y + 5.f, handle.GetRight() - r.X - 56.f, 24.f), CombineModeIntersect);
        text(g, dragonName(rec) + "  Lv." + std::to_string(rec.level), r.X + 56.f, r.Y + 8.f, 11.f, kText, 0,
             true);
        g.Restore(clip);
        const float soulWidth = showUltimateRow ? 75.f : 0.f;
        bar(g, RectF(r.X + 14.f, r.Y + 32.f, r.Width - 28.f - soulWidth, 8.f),
            d.hp / std::max(1.f, d.stats.maxHp), kHpC);
        const float right = r.GetRight() - 14.f;
        if (showSouls) {
            const UltimateCharge charge = world.ultimateCharge();
            if (charge.full())
                fillRound(g, RectF(right - 64.f, r.Y + 28.f, 67.f, 14.f), 4.f, Color(65, 75, 255, 222));
            text(g, "영혼 " + std::to_string(charge.souls) + "/" + std::to_string(kUltimateSoulCost),
                 right, r.Y + 28.f, 11.f, charge.full() ? 0xFFA6FFF0 : 0xFF72D8CD, 2, charge.full());
        } else if (ultimate) {
            const float cd = ultimateIndex < static_cast<int>(d.skillCd.size())
                                 ? d.skillCd[static_cast<size_t>(ultimateIndex)] : 0.f;
            const bool ready = rec.level >= ultimate->unlockLevel && cd <= 0.f;
            text(g, ready ? "궁극 준비" : (rec.level < ultimate->unlockLevel ? "궁극 Lv." +
                     std::to_string(ultimate->unlockLevel) : "궁극 " + fmtInt(cd) + "초"),
                 right, r.Y + 28.f, 11.f, ready ? 0xFFA6FFF0 : 0xFF72D8CD, 2, ready);
        }
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

    if (showUltimateRow) {
        const bool inTower = game.screen() == Screen::Tower;
        const bool inCombat = inTower || (game.screen() == Screen::Desktop && game.hunting() &&
                                          game.waveActive() && !game.combatPaused());
        // 오른쪽 아이콘 칸(40px)만큼 게이지를 줄인다.
        const float icon = 40.f;
        const float gw = ultimate ? w - icon - 8.f : w;
        const char* status = ultimate ? world.ultimateBlockedReason(inCombat) : nullptr;
        bool ready = false;
        if (showSouls) {
            const UltimateCharge charge = world.ultimateCharge();
            const bool full = charge.full();
            ready = full;
            text(g, "영혼 " + std::to_string(charge.souls) + " / " + std::to_string(kUltimateSoulCost),
                 x, y, 13.f, 0xFFA6FFF0);
            text(g, std::to_string(charge.percent()) + "%", x + gw, y, 12.f, full ? 0xFFA6FFF0 : kSub, 2, full);
            const RectF gauge(x, y + 18.f, gw, 7.f);
            if (full)
                fillRound(g, RectF(gauge.X - 1.f, gauge.Y - 2.f, gauge.Width + 2.f, gauge.Height + 4.f),
                          4.f, Color(65, 75, 255, 222));
            bar(g, gauge, static_cast<float>(charge.percent()) / 100.f, full ? 0xFF78FCE5 : 0xFF36C9B4);
            if (!charge.soulHarvestUnlocked) status = "영혼 수확 미해제";
            else if (!ultimate) status = full ? "충전 완료 · 궁극기 없음" : "영혼 10개 필요";
        } else {
            // 자원이 없는 궁극기: 영혼 대신 재사용 대기 게이지.
            const float cd = ultimateIndex < static_cast<int>(d.skillCd.size())
                                 ? d.skillCd[static_cast<size_t>(ultimateIndex)] : 0.f;
            const float total = std::max(0.01f, ultimate->cooldown);
            const float charge = std::clamp(1.f - cd / total, 0.f, 1.f);
            ready = charge >= 1.f && rec.level >= ultimate->unlockLevel;
            text(g, ultimate->name, x, y, 13.f, 0xFFA6FFF0);
            text(g, cd > 0.f ? fmtInt(cd) + "초" : "준비", x + gw, y, 12.f, ready ? 0xFFA6FFF0 : kSub, 2, ready);
            const RectF gauge(x, y + 18.f, gw, 7.f);
            if (ready)
                fillRound(g, RectF(gauge.X - 1.f, gauge.Y - 2.f, gauge.Width + 2.f, gauge.Height + 4.f),
                          4.f, Color(65, 75, 255, 222));
            bar(g, gauge, charge, ready ? 0xFF78FCE5 : 0xFF36C9B4);
        }
        const bool usable = ultimate && !status;
        text(g, status ? status : "궁극기 사용 가능", x, y + 27.f, 12.f, usable || ready ? 0xFFA6FFF0 : kSub, 0,
             usable);
        if (ultimate) {
            // 궁극기 아이콘. 누르면 쓴다(자동 모드에서도). 키 5와 짝이다.
            const RectF b(x + w - icon, y, icon, icon);
            if (usable)
                fillRound(g, RectF(b.X - 2.f, b.Y - 2.f, b.Width + 4.f, b.Height + 4.f), 8.f,
                          Color(110, 120, 255, 230));
            drawSkillBox(g, sprites, sp, d, ultimateIndex, b, rec.level, "5");
            if (b.Contains(PointF(ctx.mouseX, ctx.mouseY)))
                strokeRound(g, b, 6.f, Color(255, 166, 255, 240), 2.f);
            out.push_back({b, UiAction::UseUltimate, 0, rec.level >= ultimate->unlockLevel});
            markRegion(b);
        }
        y += 48.f;
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
            hint = "WASD 이동\n1~5 스킬";  // 칸이 좁다(약 60px) — 두 줄에 맞춘다
        else if ((game.screen() == Screen::Tower ? game.towerMode() : game.huntMode()) == CombatMode::SkillManual)
            hint = "1~4 스킬\n5 궁극기";
        if (hint) paragraph(g, hint, RectF(hx, y + 21.f, hw, box - 15.f), 9.f, kSub);
    }
    // 2026-11-20: 탑 도전 버튼은 스킬 칸 아래 줄이다(제목 줄에서 옮김). 바탕화면에서 무리 사이에만 누를 수 있다.
    y += box + 6.f;
    button(g, out, ctx, RectF(x, y, w, 24.f), "탑 도전", UiAction::OpenTower, 0,
           game.screen() == Screen::Desktop && !game.waveActive());
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

void Renderer::drawTowerSelection(Graphics& g, const Game& game, const Sprites& sprites,
                                  const RenderContext& ctx, std::vector<UiButton>& out, FrameRegions* regions) {
    constexpr float designW = 1120.f, designH = 620.f;
    constexpr uint32_t gold = 0xFFFFCF82;
    const float ratio = std::min({1.25f, width_ / (designW + 24.f), height_ / (designH + 24.f)});
    if (ratio <= 0.f) return;
    const float ox = (width_ - designW * ratio) * 0.5f;
    const float oy = (height_ - designH * ratio) * 0.5f;
    const auto placed = [&](RectF r) {
        return RectF(ox + r.X * ratio, oy + r.Y * ratio, r.Width * ratio, r.Height * ratio);
    };
    const RectF window(0.f, 0.f, designW, designH);
    const RectF list(226.f, 82.f, 555.f, 371.f);
    const RectF track(765.f, 93.f, 14.f, 348.f);
    towerListRect_ = placed(list);
    if (regions) regions->add(placed(window));
    const auto state = g.Save();
    g.TranslateTransform(ox, oy);
    g.ScaleTransform(ratio, ratio);
    g.SetClip(window, CombineModeIntersect);
    const RenderContext local{ctx.inputEnabled, ctx.time, (ctx.mouseX - ox) / ratio,
                              (ctx.mouseY - oy) / ratio, ctx.scale * ratio};
    std::vector<UiButton> controls;
    const auto box = [&](const RectF& r, uint32_t fill, uint32_t outline) {
        fillRound(g, r, 9.f, col(fill));
        strokeRound(g, r, 9.f, col(outline), 1.4f);
    };
    const auto art = [&](const std::string& key, const RectF& r) {
        const UiPiece image = sprites.ui(game.uiSkin(), key);
        if (!image.valid()) return false;
        const float w = static_cast<float>(image.bmp->GetWidth()), h = static_cast<float>(image.bmp->GetHeight());
        if (w <= 0.f || h <= 0.f) return false;
        const float scale = std::max(r.Width / w, r.Height / h);
        const float sw = r.Width / scale, sh = r.Height / scale;
        g.DrawImage(image.bmp, r, (w - sw) * 0.5f, (h - sh) * 0.5f, sw, sh, UnitPixel);
        return true;
    };
    const auto kindLabel = [](FloorType kind) -> const char* {
        if (kind == FloorType::FieldBoss) return "필드 보스";
        if (kind == FloorType::MidBoss) return "중간 보스";
        return "일반";
    };
    const auto label = [](int floor) {
        const auto n = std::to_string(floor);
        return std::string(n.size() < 3 ? 3 - n.size() : 0, '0') + n + "층";
    };
    const auto highlight = [](FloorType kind) -> uint32_t {
        if (kind == FloorType::FieldBoss) return 0xFFFFAD7C;
        if (kind == FloorType::MidBoss) return 0xFFFF9BA8;
        return 0xFFB5C8FF;
    };
    const auto enemyKind = [](const TowerSpawn& spawn, bool boss) {
        EnemyKind kind{};
        return enemyFromKey(spawn.key, kind) ? kind : boss ? defaultBoss() : defaultMelee();
    };
    const auto portrait = [&](const TowerFloorDef& floor, RectF area, uint32_t color) {
        const auto clip = g.Save();
        g.SetClip(area, CombineModeIntersect);
        if (!floor.spawns.empty()) {
            const auto& enemy = enemyDef(enemyKind(floor.spawns.front(), floor.boss));
            const auto sprite = sprites.sheet("enemy:" + enemy.key, AnimState::Idle);
            if (sprite.bmp) {
                drawSheetFrame(g, sprite, 0, area.X + area.Width * 0.56f, area.Y + area.Height * 0.50f,
                               area.Height * (area.Height < 100.f ? 1.75f : 1.18f), enemy.artFacesRight, 0.f, 1.f);
            } else {
                fillCircle(g, area.X + area.Width * 0.56f, area.Y + area.Height * 0.55f,
                           area.Height * 0.23f, col(color, 0.55f));
            }
        }
        g.Restore(clip);
    };

    box(window, 0xFF100D20, 0xFF715B90);
    const auto mapped = [](const RectF& panel, float sourceWidth, float x, float y, float w, float h) {
        const float scale = panel.Width / sourceWidth;
        return RectF(panel.X + x * scale, panel.Y + y * scale, w * scale, h * scale);
    };
    const RectF header(12.f, 8.f, 1096.f, 1096.f * 82.f / 1380.f);
    const bool headerV2 = art("tower_header_v2", header);
    const RectF close = headerV2 ? mapped(header, 1380.f, 1281.f, 16.f, 79.f, 49.f)
                                 : RectF(1030.f, 24.f, 63.f, 34.f);
    const bool closeHover = close.Contains(PointF(local.mouseX, local.mouseY));
    if (!headerV2) {
        box(RectF(12.f, 12.f, 1096.f, 58.f), 0xFF221731, 0xFF9271A9);
        text(g, "탑 도전", 33.f, 19.f, 31.f, gold, 0, true);
        box(close, closeHover ? 0xFF573860 : 0xFF32213D, 0xFF906AA9);
        text(g, "닫기", 1061.5f, 31.f, 15.f, kText, 1, true);
    } else if (closeHover) {
        strokeRound(g, close, 6.f, col(gold), 1.f);
    }
    controls.push_back({close, UiAction::CloseModal, 0, true});

    const RectF tower(12.f, 82.f, 199.f, 371.f);
    box(tower, 0xFF211936, 0xFF6D517B);
    const int first = game.firstVisibleTowerFloor();
    const int scrollRange = std::max(0, game.towerFloorCount() - Game::visibleTowerFloors);
    const float target = scrollRange > 0 ? static_cast<float>(first - 1) / scrollRange : 0.f;
    if (towerArtTime_ < 0.f || ctx.time < towerArtTime_) {
        towerArtProgress_ = target;
    } else {
        const float elapsed = std::min(0.1f, ctx.time - towerArtTime_);
        towerArtProgress_ += (target - towerArtProgress_) * (1.f - std::exp(-12.f * elapsed));
    }
    towerArtTime_ = ctx.time;

    // 목록을 올라가면 같은 창을 통해 석탑의 더 높은 부분을 본다.
    constexpr float travel = 140.f;
    const float shift = travel * (towerArtProgress_ - 1.f);
    const auto towerClip = g.Save();
    g.SetClip(tower, CombineModeIntersect);
    const RectF tallArt(tower.X, tower.Y + shift, tower.Width, tower.Height + travel);
    if (!art("tower_illustration", tallArt)) {
        fillCircle(g, 66.f, 149.f + shift * 0.35f, 36.f, Color(105, 168, 128, 233));
        for (int tier = 0; tier < 10; ++tier) {
            const float width = std::min(174.f, 45.f + tier * 12.f);
            const float y = 119.f + tier * 48.f + shift;
            box(RectF(103.f - width * 0.5f, y, width, 55.f), 0xFF322543, 0xFF674E7B);
            box(RectF(98.f, y + 11.f, 10.f, 23.f), 0xFFB16F56, 0xFFE7A765);
        }
    }
    g.Restore(towerClip);
    strokeRound(g, tower, 9.f, col(0xFF6D517B), 1.4f);
    Pen route(col(0xFFBF936C), 2.f);
    g.DrawLine(&route, 162.f, 119.f, 162.f, 411.f);
    const auto& dragon = game.dragonRecord();
    const int selected = game.selectedTowerFloor();
    const int topFloor = game.topVisibleTowerFloor();
    for (int floor = topFloor; floor >= first; --floor) {
        const int row = topFloor - floor;
        const auto kind = floorType(floor);
        const auto& def = game.towerFloorDef(floor);
        const bool active = floor == selected;
        const float y = 88.f + row * 72.f;
        const RectF card(226.f, y, 525.f, 64.f);
        const uint32_t accent = highlight(kind);
        const char* key = kind == FloorType::FieldBoss ? "tower_card_field" :
                          kind == FloorType::MidBoss ? "tower_card_mid" : "tower_card_normal";
        if (!art(key, card)) {
            box(card, kind == FloorType::Normal ? 0xFF242943 : 0xFF3B2238, accent);
            portrait(def, RectF(card.X + 104.f, y + 2.f, 281.f, 60.f), accent);
        }
        if (active) {
            if (!art("tower_selection", card)) strokeRound(g, card, 8.f, col(gold), 3.f);
        } else if (card.Contains(PointF(local.mouseX, local.mouseY))) {
            strokeRound(g, card, 8.f, col(gold), 1.5f);
        }
        text(g, label(floor), 243.f, y + 6.f, 22.f, active ? gold : kText, 0, true, true);
        text(g, "권장 Lv." + std::to_string(game.towerRecommendedLevel(floor)), 244.f, y + 35.f, 14.f, kText, 0, false, true);
        text(g, kindLabel(kind), 730.f, y + 9.f, 16.f, accent, 2, true, true);
        if (dragon.clearedFloors.count(floor)) text(g, "돌파 완료", 730.f, y + 35.f, 13.f, gold, 2, false, true);
        controls.push_back({card, UiAction::SelectTowerFloor, floor, true});
        fillCircle(g, 162.f, y + 32.f, active ? 9.f : 6.f, col(active ? gold : accent));
        if (active) g.DrawLine(&route, 171.f, y + 32.f, 215.f, y + 32.f);
    }
    box(track, 0xFF211B33, 0xFF6D517B);
    if (scrollRange > 0) {
        constexpr float thumb = 42.f;
        const float top = track.Y + (track.Height - thumb) * (scrollRange - (first - 1)) / scrollRange;
        fillRound(g, RectF(track.X + 2.f, top, track.Width - 4.f, thumb), 4.f, col(gold));
        controls.push_back({track, UiAction::DragTowerScroll, static_cast<int>(thumb * ratio * 100.f), true});
    }

    const auto& def = game.towerFloorDef(selected);
    const auto kind = floorType(selected);
    const uint32_t accent = highlight(kind);
    const RectF detail(792.f, 82.f, 316.f, 316.f * 465.f / 400.f);
    const bool infoV2 = art("tower_info_panel_v2", detail);
    if (!infoV2) box(RectF(792.f, 82.f, 316.f, 371.f), 0xFF241A32, accent);
    const auto info = [&](float x, float y) { return mapped(detail, 400.f, x, y, 0.f, 0.f); };
    text(g, label(selected) + " · " + kindLabel(kind), 950.f, infoV2 ? info(0.f, 32.f).Y : 94.f, infoV2 ? 16.f : 22.f, gold, 1, true);
    const RectF detailArt = infoV2 ? mapped(detail, 400.f, 19.f, 59.f, 362.f, 192.f)
                                  : RectF(804.f, 128.f, 292.f, 155.f);
    if (!infoV2) box(detailArt, kind == FloorType::Normal ? 0xFF202840 : 0xFF51273E, accent);
    if (!art("tower_floor_" + std::to_string(selected), detailArt)) portrait(def, detailArt, accent);
    const float valueX = infoV2 ? info(125.f, 0.f).X : 807.f;
    text(g, std::string(infoV2 ? "Lv." : "권장 Lv.") + std::to_string(game.towerRecommendedLevel(selected)),
         valueX, infoV2 ? info(0.f, 269.f).Y : 293.f, infoV2 ? 13.f : 17.f, accent, 0, true);
    float ey = infoV2 ? info(0.f, 302.f).Y : 320.f;
    int spawnExp = 0;
    for (const auto& spawn : def.spawns) {
        const auto key = enemyKind(spawn, def.boss);
        spawnExp += spawn.count * enemyStatsFor(key, spawn.level).expReward;
        if (ey > 361.f) continue;
        const auto& enemy = enemyDef(key);
        text(g, enemy.name + " ×" + std::to_string(spawn.count) + "  Lv." + std::to_string(spawn.level),
             valueX, ey, 13.f, kText);
        ey += 18.f;
    }
    text(g, dragon.clearedFloors.count(selected) ? "이 드래곤 · 돌파 완료" : "이 드래곤 · 미격파", valueX, infoV2 ? info(0.f, 375.f).Y : 380.f, 13.f, gold);
    const bool reward = selected == game.towerFloorCount() && !game.save().firstClearFloors.count(selected);
    const std::string xp = "처치 EXP " + std::to_string(spawnExp);
    text(g, reward ? xp + " + 첫 돌파 " + std::to_string(cfg::kTowerClearExp) : xp,
         valueX, infoV2 ? info(0.f, 414.f).Y : 404.f, 13.f, kText);
    text(g, game.save().firstClearFloors.count(selected) ? "계정 첫 돌파 기록 있음" : "계정 첫 돌파 기록 없음",
         valueX, infoV2 ? info(0.f, 392.f).Y : 427.f, infoV2 ? 10.f : 12.f, kSub);

    const RectF dragonPanel(12.f, 468.f, 1096.f, 1096.f * 175.f / 1380.f);
    const bool dragonV2 = art("tower_dragon_panel_v2", dragonPanel);
    if (!dragonV2) box(dragonPanel, 0xFF231831, 0xFF9F7591);
    const auto panelAt = [&](float x, float y, float w = 0.f, float h = 0.f) {
        return mapped(dragonPanel, 1380.f, x, y, w, h);
    };
    const auto& actor = game.world().dragon();
    const auto& species = speciesDef(dragon.species);
    const RectF dragonArt = dragonV2 ? panelAt(24.f, 27.f, 144.f, 124.f) : RectF(23.f, 479.f, 130.f, 118.f);
    if (!art("tower_portrait_" + species.key, dragonArt)) {
        const auto sheet = sprites.sheet(species.key, AnimState::Idle);
        const float x = dragonArt.X + dragonArt.Width * 0.5f, y = dragonArt.Y + dragonArt.Height * 0.5f;
        const auto clip = g.Save();
        g.SetClip(dragonArt, CombineModeIntersect);
        if (sheet.bmp) drawSheetFrame(g, sheet, 0, x, y, dragonArt.Height, false, 0.f, 1.f);
        else fillCircle(g, x, y, 24.f, col(species.color));
        g.Restore(clip);
    }
    if (!dragonV2) {
        text(g, "출전 드래곤", 160.f, 482.f, 14.f, gold, 0, true);
        text(g, "장착 스킬", 537.f, 481.f, 14.f, gold, 0, true);
    }
    text(g, dragonName(dragon), 160.f, dragonV2 ? panelAt(0.f, 60.f).Y : 510.f, 22.f, kText, 0, true);
    text(g, "Lv." + std::to_string(dragon.level), 160.f, dragonV2 ? panelAt(0.f, 110.f).Y : 549.f, 20.f, gold, 0, true);
    const auto stat = [&](const char* name, const std::string& value, float y, float v2Y) {
        if (dragonV2) text(g, value, panelAt(612.f, 0.f).X, panelAt(0.f, v2Y).Y, 16.f, kText, 2);
        else text(g, std::string(name) + "   " + value, 333.f, y, 16.f, kText);
    };
    stat("HP", fmtInt(actor.hp) + " / " + fmtInt(actor.stats.maxHp), 492.f, 40.f);
    stat("공격력", fmtInt(actor.stats.atk), 524.f, 78.f);
    stat("방어력", fmtInt(actor.stats.def), 556.f, 116.f);
    for (int i = 0; i < kSkillSlots; ++i) {
        const RectF slot = dragonV2 ? panelAt(661.f + i * 80.f, 59.f, 63.f, 63.f)
                                    : RectF(537.f + i * 63.f, 510.f, 54.f, 51.f);
        const int index = dragon.slots[i];
        drawSkillBox(g, sprites, species, actor, index, slot, dragon.level, std::to_string(i + 1));
        if (index >= 0 && index < static_cast<int>(species.skills.size()) &&
            slot.Contains(PointF(local.mouseX, local.mouseY)))
            text(g, species.skills[index].name, slot.X, slot.GetBottom() + 3.f, 12.f, kText);
    }
    const RectF enterArt = panelAt(1000.f, 40.f, 360.f, 360.f * 124.f / 508.f);
    RectF enter(826.f, 507.f, 254.f, 62.f);
    if (art("tower_enter_v2", enterArt)) {
        // 클릭 영역은 외곽의 투명 여백과 뾰족한 장식보다 보라색 본체에 가깝게 맞춘다.
        enter = mapped(enterArt, 508.f, 48.f, 23.f, 412.f, 73.f);
        if (enter.Contains(PointF(local.mouseX, local.mouseY))) strokeRound(g, enter, 12.f, col(gold, 0.7f), 1.f);
    } else {
        if (!art("tower_enter", enter)) box(enter, 0xFF633187, gold);
        if (enter.Contains(PointF(local.mouseX, local.mouseY))) strokeRound(g, enter, 10.f, col(gold), 3.f);
        text(g, "탑 입장", 953.f, 522.f, 27.f, kText, 1, true);
    }
    controls.push_back({enter, UiAction::ConfirmTowerFloor, 0, true});
    g.Restore(state);
    for (const auto& control : controls)
        out.push_back({placed(control.rect), control.action, control.arg, control.enabled});
}

bool Renderer::drawTowerBackground(Graphics& g, int floor, const RectF& a) {
    if (!sprites_ || scale_ <= 0.f) return false;
    const int zone = std::clamp((floor - 1) / 10 + 1, 1, 10);
    const int devX = static_cast<int>(std::floor(a.X * scale_ + 0.5f));
    const int devY = static_cast<int>(std::floor(a.Y * scale_ + 0.5f));
    const int devW = static_cast<int>(std::floor(a.GetRight() * scale_ + 0.5f)) - devX;
    const int devH = static_cast<int>(std::floor(a.GetBottom() * scale_ + 0.5f)) - devY;
    if (devW <= 0 || devH <= 0) return false;
    if (zone != towerBgZone_ || devW != towerBgW_ || devH != towerBgH_) {
        towerBgZone_ = zone;
        towerBgW_ = devW;
        towerBgH_ = devH;
        towerBg_.reset();
        towerBgMissing_ = true;
        std::unique_ptr<Bitmap> src = sprites_->loadTowerBackground(zone);
        if (src) {
            // 전투장 비율에 맞춰 가운데를 잘라(cover) 장치 크기로 한 번만 줄인다.
            auto dst = std::make_unique<Bitmap>(devW, devH, PixelFormat32bppPARGB);
            if (dst->GetLastStatus() == Ok) {
                Graphics cg(dst.get());
                cg.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                cg.SetPixelOffsetMode(PixelOffsetModeHalf);
                const float sw = static_cast<float>(src->GetWidth()), sh = static_cast<float>(src->GetHeight());
                const float k = std::max(static_cast<float>(devW) / sw, static_cast<float>(devH) / sh);
                const float cw = static_cast<float>(devW) / k, ch = static_cast<float>(devH) / k;
                ImageAttributes ia;
                ia.SetWrapMode(WrapModeTileFlipXY);
                cg.DrawImage(src.get(), RectF(0.f, 0.f, static_cast<float>(devW), static_cast<float>(devH)),
                             (sw - cw) * 0.5f, (sh - ch) * 0.5f, cw, ch, UnitPixel, &ia);
                towerBg_ = std::move(dst);
                towerBgMissing_ = false;
            }
        }
    }
    if (towerBgMissing_ || !towerBg_) return false;
    Matrix saved;
    g.GetTransform(&saved);
    g.ResetTransform();
    g.DrawImage(towerBg_.get(), devX, devY);
    g.SetTransform(&saved);
    return true;
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

    if (sc == Screen::Select || sc == Screen::Stable || sc == Screen::Skills || sc == Screen::Shop) {
        // 좁은 작업 영역에서는 창과 클릭 좌표에 동일한 축소율을 적용한다.
        const float factor = std::min({1.f, width_ / 870.f, height_ / 1010.f});
        if (factor <= 0.f) return;
        const float originalWidth = width_, originalHeight = height_, originalScale = scale_;
        const size_t first = out.size();
        const size_t firstRegion = g_regions ? g_regions->hit.size() : 0;
        const auto transform = g.Save();
        g.ScaleTransform(factor, factor);
        width_ /= factor;
        height_ /= factor;
        if (factor < 1.f) scale_ = 0.f;  // 캐시의 장치 좌표 붙이기 대신 현재 변환으로 그린다.
        RenderContext local = ctx;
        local.mouseX /= factor;
        local.mouseY /= factor;
        if (sc == Screen::Select) drawSelect(g, game, sprites, local, out);
        else if (sc == Screen::Stable) drawStable(g, game, sprites, local, out);
        else if (sc == Screen::Shop) drawShop(g, game, local, out);
        else drawSkills(g, game, sprites, local, out);
        const auto scaleRect = [factor](RectF& r) {
            r.X *= factor; r.Y *= factor; r.Width *= factor; r.Height *= factor;
        };
        for (size_t i = first; i < out.size(); ++i) scaleRect(out[i].rect);
        if (g_regions)
            for (size_t i = firstRegion; i < g_regions->hit.size(); ++i) scaleRect(g_regions->hit[i]);
        g.Restore(transform);
        width_ = originalWidth;
        height_ = originalHeight;
        scale_ = originalScale;
        return;
    }
    switch (sc) {
        case Screen::TowerSelect: break;  // drawOverlay에서 전용 레이아웃으로 처리
        case Screen::Select:
        case Screen::Stable:
        case Screen::Skills:
        case Screen::Shop:
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
            const bool partyFloor = game.fieldBossFloorSelected();
            const int partyRows = partyFloor ? static_cast<int>(game.stable().size()) : 0;
            const float extraH = partyFloor ? 70.f + 40.f * static_cast<float>(partyRows) : 0.f;
            RectF r(cx - 310.f, cy - 236.f - extraH * 0.5f, 620.f, 472.f + extraH);
            panel(g, r);
            text(g, "탑에 도전할까요?", r.X + 24.f, r.Y + 20.f, 20.f, kText, 0, true);
            const int floor = game.selectedTowerFloor();
            const int recommended = game.towerRecommendedLevel(floor);
            text(g, std::to_string(floor) + (partyFloor ? "층 · 필드 보스 · 4마리 파티 도전" : "층 · 단독 도전"), r.X + 24.f,
                 r.Y + 56.f, 14.f, partyFloor ? 0xFFFFAD7C : kSub, 0, partyFloor);
            bool low = rec.level < recommended;
            text(g, "현재 Lv." + std::to_string(rec.level) + "  ·  권장 Lv." + std::to_string(recommended),
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
            {
                // T6: 가방의 물약을 모두 가지고 간다(개수 제한 없음). 쓰지 않은 것은 나올 때 돌려받는다.
                const SaveData& sv = game.save();
                const float py = r.Y + 290.f;
                text(g, "가지고 갈 물약", r.X + 24.f, py, 14.f, kText, 0, true);
                std::string line;
                int total = 0;
                for (int k = 0; k < kPotionKinds; ++k) {
                    const int n = sv.potion(static_cast<PotionKind>(k));
                    total += n;
                    if (n > 0) line += std::string(line.empty() ? "" : "  ·  ") + potionName(static_cast<PotionKind>(k)) + " " + std::to_string(n);
                }
                text(g, total > 0 ? line : std::string("가방에 물약이 없습니다. 상점에서 살 수 있습니다."), r.X + 24.f, py + 26.f,
                     14.f, total > 0 ? kText : 0xFF7C8090);
                text(g, "가방에 있는 물약을 모두 가지고 갑니다. 남은 것은 나올 때 돌려받습니다.", r.X + 24.f, py + 48.f, 12.f, kSub);
            }
            if (partyFloor) {
                // T7: 출전 4마리 고르기. 지금 띄운 드래곤은 늘 들어가고 조작한다. 상한을 넘으면 고를 수 없다.
                const int cap = fieldBossLevelCap(floor);
                float py = r.Y + 360.f;
                text(g, "출전 드래곤 " + std::to_string(game.partyPick().size()) + " / " +
                            std::to_string(cfg::kFieldBossPartySize) + "  ·  레벨 상한 Lv." + std::to_string(cap),
                     r.X + 24.f, py, 14.f, kText, 0, true);
                py += 26.f;
                const auto& list = game.stable();
                for (int i = 0; i < static_cast<int>(list.size()); ++i) {
                    const DragonRecord& d = list[static_cast<size_t>(i)];
                    const bool me = d.id == rec.id;
                    const bool picked = game.partyPicked(d.id);
                    const bool over = d.level > cap;
                    text(g, dragonName(d) + "  Lv." + std::to_string(d.level) + (me ? "  (조작)" : "") +
                                (over ? "  — 레벨 상한 초과" : ""),
                         r.X + 32.f, py + 8.f, 13.f, over ? kWarnC : (picked ? kText : kSub));
                    button(g, out, ctx, RectF(r.GetRight() - 24.f - 120.f, py + 2.f, 120.f, 32.f),
                           me ? "조작" : (picked ? "빼기" : "넣기"), UiAction::TogglePartyMember, i,
                           !me && (picked || (!over && static_cast<int>(game.partyPick().size()) < cfg::kFieldBossPartySize)),
                           picked);
                    py += 40.f;
                }
                if (const char* why = game.partyBlockedReason())
                    text(g, why, r.X + 24.f, py + 4.f, 13.f, kWarnC, 0, true);
            }
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
            const bool nextParty = floorType(game.towerFloor() + 1) == FloorType::FieldBoss;
            std::string next = nextParty ? "다음은 필드 보스입니다. 탑 선택창에서 4마리로 도전하세요."
                               : game.towerFloorDef(game.towerFloor() + 1).boss ? "다음은 보스 층입니다." : "다음 층으로 갈까요?";
            text(g, next, r.X + r.Width * 0.5f, r.Y + 130.f, 14.f, nextParty ? kWarnC : kSub, 1);
            button(g, out, ctx, RectF(r.X + 24.f, r.GetBottom() - 66.f, 220.f, 46.f), "다음 층", UiAction::NextFloor, 0,
                   !nextParty);
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
            const std::string reward = game.towerFinalRewardGranted()
                ? "첫 돌파 보상 +" + std::to_string(cfg::kTowerClearExp) + " EXP"
                : "마지막 층 돌파";
            text(g, reward + "  ·  정복 횟수 " +
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
    const float availW = width_ - 150.f - 48.f;  // 양옆 여백과 패널 안쪽 여백
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
    r = modalPanel(g, r, 242);
    const auto contentClip = g.Save();
    g.SetClip(r, CombineModeIntersect);
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
        const size_t previewSkills = std::min<size_t>(2, sp.skills.size());
        for (size_t skillIndex = 0; skillIndex < previewSkills; ++skillIndex) {
            const auto& s = sp.skills[skillIndex];
            text(g, s.name, x, y, 12.f, 0xFFE8EAF2, 0, true);
            text(g, "Lv." + std::to_string(s.unlockLevel), c.GetRight() - 14.f, y, 12.f, kSub, 2);
            y += 17.f;
            paragraph(g, s.desc, RectF(x, y, c.Width - 28.f, 30.f), 11.f, 0xFF9AA2B4);
            y += 30.f;
        }
        if (sp.skills.size() > previewSkills)
            text(g, "외 " + std::to_string(sp.skills.size() - previewSkills) + "개 · 스킬 관리에서 확인", x, y, 10.f, kSub);
        button(g, out, ctx, RectF(c.X + 14.f, c.GetBottom() - 52.f, c.Width - 28.f, 40.f), "이 드래곤으로 시작",
               UiAction::ChooseSpecies, i);
    }
    RectF warn(r.X + 24.f, r.GetBottom() - 60.f, r.Width - 48.f, 40.f);
    fillRound(g, warn, 8.f, Color(110, 120, 20, 24));
    text(g, "드래곤이 쓰러지면 그 개체는 되살릴 수 없습니다. 탑 최고 기록과 도감은 남습니다.", warn.X + warn.Width * 0.5f,
         warn.Y + 10.f, 14.f, 0xFFFFC2C2, 1, true);
    g.Restore(contentClip);
}

void Renderer::drawStable(Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                          std::vector<UiButton>& out) {
    const std::vector<DragonRecord>& list = game.stable();
    const SaveData& s = game.save();
    const int rows = std::max(static_cast<int>(list.size()), s.stableCapacity());  // T6: 칸 수는 늘어난다
    // 4칸이면 예전 크기(580). 칸이 늘면 창을 아래로 늘린다(최대 9칸 ≈ 980, drawModal 이 화면에 맞춰 줄인다).
    const float winH = 580.f + 84.f * static_cast<float>(std::max(0, rows - 4));
    RectF r(width_ * 0.5f - 350.f, height_ * 0.5f - winH * 0.5f, 700.f, winH);
    r = modalPanel(g, r, 244);
    const auto contentClip = g.Save();
    g.SetClip(r, CombineModeIntersect);
    text(g, "드래곤 관리", r.X + 24.f, r.Y + 18.f, 22.f, kText, 0, true);
    text(g, std::to_string(list.size()) + " / " + std::to_string(s.stableCapacity()) + " 마리  ·  크레딧 " +
             std::to_string(game.credits()),
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
            text(g, "비어 있음", row.X + 18.f, row.Y + row.Height * 0.5f - 10.f, 14.f, 0xFF7C8090, 0, true);
            // 2026-11-24: 처음 4마리만 공짜(모두 죽으면 한 마리). 그 뒤는 상점의 알.
            const float bh = std::min(40.f, row.Height - 8.f);
            RectF b(row.GetRight() - 170.f, row.Y + (row.Height - bh) * 0.5f, 150.f, bh);
            if (game.freeDragonAvailable())
                button(g, out, ctx, b, "새 드래곤 (공짜)", UiAction::AddDragon, 0, true);
            else
                button(g, out, ctx, b, "상점에서 알 사기", UiAction::OpenShop, 0, true);
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
    button(g, out, ctx, RectF(r.GetRight() - 300.f, r.GetBottom() - 54.f, 130.f, 40.f), "상점", UiAction::OpenShop);
    button(g, out, ctx, RectF(r.GetRight() - 150.f, r.GetBottom() - 54.f, 130.f, 40.f),
           canClose ? "닫기" : "띄울 드래곤을 고르세요", UiAction::CloseStable, 0, canClose);
    g.Restore(contentClip);
}
// ---------------------------------------------------------------------------
// 탑 100층 T6 상점(2026-11-24). 수치·규칙은 core(Game::buy, shopBlockedReason)가 정한다.
// ---------------------------------------------------------------------------
void Renderer::drawShop(Graphics& g, const Game& game, const RenderContext& ctx, std::vector<UiButton>& out) {
    const SaveData& s = game.save();
    struct Item {
        ShopItem id;
        std::string name;
        std::string desc;
        int have;  // 가방 수량(-1 이면 표시 안 함)
    };
    const auto pct = [](float v) { return std::to_string(static_cast<int>(v * 100.f + 0.5f)) + "%"; };
    const auto sec = [](float v) { return std::to_string(static_cast<int>(v)) + "초"; };
    const auto oneDecimal = [](float v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
        return std::string(buf);
    };
    const Item items[] = {
        {ShopItem::Egg, "드래곤 알", "바로 부화합니다. 종은 무작위, Lv.1. 빈 칸이 있어야 하고 살 때마다 비싸집니다.", -1},
        {ShopItem::PotionSmall, "물약(소)", "탑 전용. 체력 " + pct(cfg::kPotionSmallHeal) + " 회복.", s.potionSmall},
        {ShopItem::PotionLarge, "물약(대)", "탑 전용. 체력 " + pct(cfg::kPotionLargeHeal) + " 회복.", s.potionLarge},
        {ShopItem::PotionFury, "분노의 물약",
         "탑 전용. " + sec(cfg::kFuryTime) + " 동안 공격력 +" + pct(cfg::kFuryAtkMul - 1.f) + ".", s.potionFury},
        {ShopItem::PotionGuard, "수호의 물약",
         "탑 전용. " + sec(cfg::kGuardTime) + " 동안 받는 피해 -" + pct(1.f - cfg::kGuardDamageMul) + ".", s.potionGuard},
        {ShopItem::PotionSwift, "신속의 물약",
         "탑 전용. " + sec(cfg::kSwiftTime) + " 동안 스킬 쿨타임이 " + oneDecimal(cfg::kSwiftCdMul) + "배 빨리 돕니다.",
         s.potionSwift},
        {ShopItem::HunterCharm, "사냥꾼의 부적",
         "실제 시간 " + std::to_string(cfg::kHunterCharmSeconds / 60) + "분 동안 바탕화면 처치 경험치 +" +
             pct(cfg::kHunterCharmExpMul - 1.f) + "(직접·스킬 수동만, 자동 제외). 사면 시간이 이어 붙습니다.",
         -1},
    };
    const int count = static_cast<int>(sizeof(items) / sizeof(items[0]));
    const char* const icons[] = {"shop_icon_egg",          "shop_icon_potion_small", "shop_icon_potion_large",
                                 "shop_icon_potion_fury",  "shop_icon_potion_guard", "shop_icon_potion_swift",
                                 "shop_icon_charm"};
    // 2026-11-24 Codex 상점 그림(assets/ui/default/shop_*). 그림이 없으면 예전처럼 도형으로 그린다.
    // 배치는 Codex 조립 미리보기(assets/references/ui/shop-2026-11-24/shop_preview.png)를 따른다:
    // 위쪽 저울 장식 아래에 제목 띠(64~106), 안내 한 줄, 품목 줄은 147부터 66 간격.
    const bool art = sprites_ && sprites_->ui(skin_, "shop_panel").valid();
    const float rowTop = art ? 147.f : 84.f;
    const float rowStep = 66.f, rowH = 58.f;
    const float eggH = s.eggs > 0 ? 50.f : 0.f;
    const float winH = rowTop + rowStep * static_cast<float>(count) + eggH + 84.f;
    RectF r(width_ * 0.5f - 360.f, height_ * 0.5f - winH * 0.5f, 720.f, winH);
    if (art) {
        markRegion(r);
        piece(g, "shop_panel", r, 244.f / 255.f);
    } else {
        r = modalPanel(g, r, 244);
    }
    const auto clip = g.Save();
    g.SetClip(RectF(r.X - 4.f, r.Y - 40.f, r.Width + 8.f, r.Height + 44.f), CombineModeIntersect);
    const std::string creditText = std::to_string(game.credits());
    if (art) {
        RectF hb(r.X + 28.f, r.Y + 64.f, r.Width - 56.f, 42.f);
        RectF hc = hb;
        if (!piece(g, "shop_header", hb, 1.f, &hc)) hc = hb;
        text(g, "상점", hc.X + 8.f, hb.Y + 9.f, 20.f, kText, 0, true);
        text(g, creditText, hc.GetRight() - 8.f, hb.Y + 11.f, 17.f, 0xFFFFD45A, 2, true);
        const float tw = 11.f * static_cast<float>(creditText.size()) + 14.f;
        piece(g, "icon_credit", RectF(hc.GetRight() - 8.f - tw - 22.f, hb.Y + 10.f, 22.f, 22.f), 1.f);
    } else {
        text(g, "상점", r.X + 24.f, r.Y + 18.f, 22.f, kText, 0, true);
        text(g, "크레딧 " + creditText, r.GetRight() - 24.f, r.Y + 22.f, 18.f, 0xFFFFD45A, 2, true);
    }
    const long long charm = game.hunterCharmLeft();
    text(g, charm > 0 ? "사냥꾼의 부적 " + std::to_string((charm + 59) / 60) + "분 남음"
                      : std::string("물약은 탑 안에서만 마십니다. 마시는 데 1초, 그동안 느려집니다."),
         r.X + 32.f, r.Y + (art ? 116.f : 54.f), 13.f, charm > 0 ? kExpC : kSub);

    float y = r.Y + rowTop;
    for (int idx = 0; idx < count; ++idx) {
        const Item& it = items[idx];
        RectF row(r.X + 20.f, y, r.Width - 40.f, rowH);
        const bool hover = row.Contains(PointF(ctx.mouseX, ctx.mouseY));
        const bool rowArt = art && piece(g, hover ? "shop_row_hover" : "shop_row", row, 1.f);
        if (!rowArt) {
            fillRound(g, row, 10.f, Color(215, 32, 34, 46));
            strokeRound(g, row, 10.f, Color(120, 80, 88, 120), 1.f);
        }
        // 왼쪽 아이콘 받침(줄 그림의 왼쪽 정사각형) 안에 아이콘
        const bool iconArt = piece(g, icons[idx], RectF(row.X + 9.f, row.Y + 9.f, rowH - 18.f, rowH - 18.f), 1.f);
        const float tx = row.X + (iconArt || rowArt ? 66.f : 16.f);
        text(g, it.name, tx, row.Y + 6.f, 15.f, kText, 0, true);
        if (it.have >= 0) text(g, "보유 " + std::to_string(it.have), tx + 140.f, row.Y + 8.f, 12.f, kSub);
        const std::string price = std::to_string(game.shopPrice(it.id));
        const bool coin = piece(g, "icon_credit", RectF(tx + 220.f, row.Y + 7.f, 16.f, 16.f), 1.f);
        text(g, coin ? price : price + " 크레딧", tx + (coin ? 240.f : 220.f), row.Y + 6.f, 14.f, 0xFFFFD45A, 0, true);
        paragraph(g, it.desc, RectF(tx, row.Y + 29.f, row.GetRight() - 170.f - tx, 26.f), 11.f, kSub);
        const char* why = game.shopBlockedReason(it.id);
        const bool enabled = why == nullptr;
        RectF b(row.GetRight() - 158.f, row.Y + 11.f, 140.f, 34.f);
        const bool bHover = enabled && b.Contains(PointF(ctx.mouseX, ctx.mouseY));
        if (art && piece(g, !enabled ? "shop_buy_off" : bHover ? "shop_buy_hover" : "shop_buy", b, 1.f)) {
            text(g, "사기", b.X + b.Width * 0.5f, b.Y + 7.f, 15.f, enabled ? 0xFF2A1640 : 0xFF6A6A72, 1, true);
            UiButton ub;
            ub.rect = b;
            ub.action = UiAction::BuyItem;
            ub.arg = static_cast<int>(it.id);
            ub.enabled = enabled;
            out.push_back(ub);
            markRegion(b);
        } else {
            button(g, out, ctx, b, "사기", UiAction::BuyItem, static_cast<int>(it.id), enabled);
        }
        if (why) text(g, why, b.X + b.Width * 0.5f, b.GetBottom() + 1.f, 10.f, kWarnC, 1);
        y += rowStep;
    }

    // 보상으로 받은 알(칸이 없어 남겨 둔 것)
    if (s.eggs > 0) {
        const bool can = !game.stableFull();
        piece(g, "shop_icon_egg", RectF(r.X + 28.f, y + 4.f, 36.f, 36.f), 1.f);
        text(g, "부화하지 않은 알 " + std::to_string(s.eggs) + "개", r.X + 72.f, y + 12.f, 15.f, kText, 0, true);
        button(g, out, ctx, RectF(r.X + 280.f, y + 2.f, 150.f, 40.f), "알 부화", UiAction::HatchEgg, 0, can);
        if (!can) text(g, "빈 칸이 없습니다", r.X + 444.f, y + 14.f, 12.f, kWarnC);
    }

    paragraph(g, "크레딧은 바탕화면에서 적을 쓰러뜨리거나 탑의 층을 처음 돌파하면 얻습니다. 부활·사망 무효는 팔지 않습니다.",
              RectF(r.X + 32.f, r.GetBottom() - 70.f, r.Width - 230.f, 36.f), 12.f, 0xFF9AA0B4);
    button(g, out, ctx, RectF(r.GetRight() - 170.f, r.GetBottom() - 70.f, 130.f, 38.f), "닫기", UiAction::CloseShop);
    g.Restore(clip);
}

// 탑 전투 중 물약 버튼(마우스는 키보드 조작과 무관하게 항상 받는다).
void Renderer::drawTowerPotions(Graphics& g, const Game& game, const RenderContext& ctx, std::vector<UiButton>& out) {
    RectF a(game.arena().x, game.arena().y, game.arena().w, game.arena().h);
    const DragonActor& d = game.world().dragon();
    const bool drinking = game.drinkLeft() > 0.f;
    const float bw = 118.f, bh = 34.f;
    float x = a.X + 16.f;
    const float y = a.GetBottom() + 10.f;
    static const char* const kIcons[kPotionKinds] = {"shop_icon_potion_small", "shop_icon_potion_large",
                                                     "shop_icon_potion_fury", "shop_icon_potion_guard",
                                                     "shop_icon_potion_swift"};
    for (int k = 0; k < kPotionKinds; ++k) {
        const PotionKind pk = static_cast<PotionKind>(k);
        const int n = game.carried(pk);
        if (n <= 0) continue;  // 가지고 들어오지 않은 종류는 버튼을 만들지 않는다
        // 상점 아이콘이 있으면 버튼 왼쪽에 붙인다(그림이 없으면 글자 버튼만)
        if (piece(g, kIcons[k], RectF(x, y, bh, bh), drinking ? 0.5f : 1.f)) x += bh + 2.f;
        button(g, out, ctx, RectF(x, y, bw, bh), std::string(potionName(pk)) + " x" + std::to_string(n),
               UiAction::DrinkPotion, k, !drinking);
        x += bw + 8.f;
    }
    if (drinking) {
        bar(g, RectF(x + 4.f, y + 10.f, 120.f, 14.f), 1.f - game.drinkLeft() / cfg::kPotionDrinkTime, 0xFF7CFFB0, "마시는 중");
        x += 132.f;
    }
    // 켜져 있는 물약 효과와 남은 초
    std::string buffs;
    if (d.furyTime > 0.f) buffs += "분노 " + fmtInt(d.furyTime) + "초  ";
    if (d.guardTime > 0.f) buffs += "수호 " + fmtInt(d.guardTime) + "초  ";
    if (d.swiftTime > 0.f) buffs += "신속 " + fmtInt(d.swiftTime) + "초";
    if (!buffs.empty()) text(g, buffs, x + 4.f, y + 8.f, 14.f, 0xFFFFD45A, 0, true, true);
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
    float maxH = std::max(320.f, height_ - 400.f);
    float ph = std::min(chromeH + rowH * static_cast<float>(std::max(1, total)), maxH);
    int rowsPerPage = static_cast<int>((ph - chromeH) / rowH);
    if (rowsPerPage < 1) rowsPerPage = 1;
    const int pages = (total + rowsPerPage - 1) / rowsPerPage;
    const int page = std::clamp(game.skillPage(), 0, std::max(0, pages - 1));

    const float pw = std::min(760.f, std::max(560.f, width_ - 150.f));
    RectF r(width_ * 0.5f - pw * 0.5f, std::max(10.f, height_ * 0.5f - ph * 0.5f), pw, ph);
    r = modalPanel(g, r, 244);
    const auto contentClip = g.Save();
    g.SetClip(r, CombineModeIntersect);
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
        else if (s.ultimate) text(g, "궁극기 · 키 5", labelRight, row.Y + 25.f, 12.f, 0xFFA6FFF0, 2, true);
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
        paragraph(g, desc, RectF(tx, row.Y + 24.f, std::max(1.f, labelRight - tx - 105.f), 16.f), 12.f, kSub);

        RectF btn(rx - 126.f, row.Y + 6.f, 116.f, 30.f);
        if (passive) {
            button(g, out, ctx, btn, unlocked ? "적용 중" : "잠김", UiAction::None, 0, false, unlocked);
        } else if (s.ultimate) {
            // 2026-11-20: 궁극기는 장착 칸에 넣지 않는다 — 상태창 전용 칸(키 5·클릭)에서 쓴다.
            button(g, out, ctx, btn, unlocked ? "전용 칸" : "Lv." + std::to_string(s.unlockLevel) + " 필요",
                   UiAction::None, 0, false, unlocked);
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
    g.Restore(contentClip);
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
