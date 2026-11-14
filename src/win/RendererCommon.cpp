// 공용 그리기 도구: 글꼴·글자·창틀 조각·버튼·막대·토글·슬라이더·스프라이트 한 장.
// 월드·HUD·모달이 모두 쓴다.
#include "RendererInternal.h"

namespace dt::win {

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

}  // namespace dt::win
