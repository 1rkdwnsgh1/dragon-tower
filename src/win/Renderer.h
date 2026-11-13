// GDI+로 게임 화면을 그린다. 이미지가 없으면 임시 도형으로 대신한다.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Sprites.h"
#include "WinCommon.h"
#include "core/Game.h"

namespace dt::win {

enum class UiAction {
    None,
    ChooseSpecies,  // arg = 종류
    StartHunt,
    StopHunt,
    SetMode,        // arg = CombatMode
    OpenTower,
    EnterTower,     // arg = CombatMode
    CloseModal,
    ConfirmAuto,
    NextFloor,
    LeaveTower,
    OpenCodex,
    AckDeath,
    ExitGame,
};

struct UiButton {
    Gdiplus::RectF rect;
    UiAction action = UiAction::None;
    int arg = 0;
    bool enabled = true;
};

struct RenderContext {
    bool inputEnabled = false;
    float time = 0.f;      // 애니메이션용 누적 시간
    float mouseX = -1.f;   // 논리 좌표
    float mouseY = -1.f;
};

class Renderer {
public:
    bool init();
    void shutdown();

    // 전체 오버레이(바탕화면 크기). 눌릴 수 있는 버튼 목록을 outButtons에 채운다.
    void drawOverlay(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                     std::vector<UiButton>& outButtons);

    // 설정 버튼 창. attention이면 입력을 켜 달라는 의미로 깜빡인다.
    void drawSettingsButton(Gdiplus::Graphics& g, float size, const Sprites& sprites, bool inputOn, bool attention,
                            bool hover, float time);

private:
    Gdiplus::Font* font(float size, bool bold);
    void text(Gdiplus::Graphics& g, const std::string& s, float x, float y, float size, uint32_t color,
              int align = 0, bool bold = false, bool strongShadow = false);
    void paragraph(Gdiplus::Graphics& g, const std::string& s, const Gdiplus::RectF& r, float size, uint32_t color,
                   int align = 0, bool bold = false);
    void panel(Gdiplus::Graphics& g, const Gdiplus::RectF& r, uint8_t alpha = 236);
    void button(Gdiplus::Graphics& g, std::vector<UiButton>& out, const RenderContext& ctx, const Gdiplus::RectF& r,
                const std::string& label, UiAction action, int arg = 0, bool enabled = true, bool selected = false,
                bool danger = false);
    void bar(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float ratio, uint32_t color, const std::string& label = "");

    void drawWorld(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx);
    void drawDragon(Gdiplus::Graphics& g, const DragonActor& d, const Sprites& sprites, float time, float x, float y,
                    float height, bool preview);
    void drawEnemy(Gdiplus::Graphics& g, const Enemy& e, const Sprites& sprites, float time);
    void drawSprite(Gdiplus::Graphics& g, Gdiplus::Bitmap* bmp, float cx, float cy, float height, bool flip,
                    float flash, float alpha);
    // 시트의 한 칸만 그린다. frame은 0부터.
    void drawSheetFrame(Gdiplus::Graphics& g, const Sheet& sh, int frame, float cx, float cy, float height, bool flip,
                        float flash, float alpha);
    void drawFx(Gdiplus::Graphics& g, const World& w);

    void drawStatusCard(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx);
    void drawMenu(Gdiplus::Graphics& g, const Game& game, const RenderContext& ctx, std::vector<UiButton>& out);
    void drawNotices(Gdiplus::Graphics& g, const Game& game);
    void drawTowerHud(Gdiplus::Graphics& g, const Game& game);
    void drawModal(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                   std::vector<UiButton>& out);
    void drawSelect(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                    std::vector<UiButton>& out);
    void drawCodex(Gdiplus::Graphics& g, const Game& game, const RenderContext& ctx, std::vector<UiButton>& out);

    std::unique_ptr<Gdiplus::FontFamily> family_;
    std::map<int, std::unique_ptr<Gdiplus::Font>> fonts_;
    float width_ = 0.f;
    float height_ = 0.f;
};

}  // namespace dt::win
