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
    DragPanel,        // 상태창 끌기 손잡이
    TogglePanel,      // 상태창 접기/펼치기
    ToggleConfine,    // 드래곤을 아래 띠에만 둘지
    DragDragon,       // 드래곤 끌어 옮기기
    DragMenu,         // 메뉴 창 끌기 손잡이
    MenuScale,        // 메뉴 창 크기. arg = -1 작게 / +1 크게
    CharScale,        // 캐릭터 보기 크기. arg = -1 작게 / +1 크게
    TogglePowerSave,  // 조용할 때 프레임 줄이기
    SetKeepRange,     // 싸울 때 유지할 간격. arg = 0 근접 / 1 중간 / 2 바깥
    ToggleMenu,       // 메뉴 창 열고 닫기(키보드 조작과 무관)
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

// 한 프레임에 그린 것들의 위치. 어디서 클릭을 받을지(나머지는 통과)와
// 어디를 다시 그릴지 정하는 데 쓴다.
struct FrameRegions {
    std::vector<Gdiplus::RectF> hit;   // 마우스를 받아야 하는 영역
    bool fullScreen = false;           // 창·모달이 떠 있어 화면 전체를 받아야 한다
    void add(const Gdiplus::RectF& r) { hit.push_back(r); }
};

class Renderer {
public:
    bool init();
    void shutdown();

    // 전체 오버레이(바탕화면 크기). 눌릴 수 있는 버튼 목록을 outButtons에 채운다.
    void drawOverlay(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                     std::vector<UiButton>& outButtons, FrameRegions* outRegions = nullptr);

    // 설정 버튼 창. attention이면 입력을 켜 달라는 의미로 깜빡인다.
    void drawSettingsButton(Gdiplus::Graphics& g, float size, const Sprites& sprites, bool inputOn, bool attention,
                            bool hover, float time);

private:
    Gdiplus::Font* font(float size, bool bold);
    void text(Gdiplus::Graphics& g, const std::string& s, float x, float y, float size, uint32_t color,
              int align = 0, bool bold = false, bool strongShadow = false);
    void paragraph(Gdiplus::Graphics& g, const std::string& s, const Gdiplus::RectF& r, float size, uint32_t color,
                   int align = 0, bool bold = false);
    // v0.9 UI 스킨. 조각이 있으면 그림으로, 없으면 예전처럼 도형으로 그린다.
    // piece()는 그렸으면 true. 9조각으로 늘릴 여백이 0이면 통째로 늘린다.
    bool piece(Gdiplus::Graphics& g, const char* name, const Gdiplus::RectF& r, float alpha = 1.f,
               Gdiplus::RectF* content = nullptr);
    void panel(Gdiplus::Graphics& g, const Gdiplus::RectF& r, uint8_t alpha = 236);
    void button(Gdiplus::Graphics& g, std::vector<UiButton>& out, const RenderContext& ctx, const Gdiplus::RectF& r,
                const std::string& label, UiAction action, int arg = 0, bool enabled = true, bool selected = false,
                bool danger = false);
    void bar(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float ratio, uint32_t color, const std::string& label = "");

    void drawWorld(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx);
    void drawDragon(Gdiplus::Graphics& g, const DragonActor& d, const Sprites& sprites, float time, float x, float y,
                    float height, bool preview);
    // charScale은 보기 배수다. 그림 크기만 바꾸고 판정에는 쓰지 않는다.
    void drawEnemy(Gdiplus::Graphics& g, const Enemy& e, const Sprites& sprites, float time, float charScale);
    void drawSprite(Gdiplus::Graphics& g, Gdiplus::Bitmap* bmp, float cx, float cy, float height, bool flip,
                    float flash, float alpha);
    // 시트의 한 칸만 그린다. frame은 0부터.
    void drawSheetFrame(Gdiplus::Graphics& g, const Sheet& sh, int frame, float cx, float cy, float height, bool flip,
                        float flash, float alpha);
    void drawFx(Gdiplus::Graphics& g, const World& w);

    void drawStatusCard(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                        std::vector<UiButton>& out);
    // 켜짐/꺼짐 토글 스위치. 누르면 action이 발생한다.
    void toggle(Gdiplus::Graphics& g, std::vector<UiButton>& out, const RenderContext& ctx, const Gdiplus::RectF& r,
                const std::string& label, bool on, UiAction action, int arg, bool enabled);
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
    // 한 프레임 동안 쓸 스킨. drawOverlay 시작에서 정한다.
    const Sprites* sprites_ = nullptr;
    std::string skin_ = "default";
};

}  // namespace dt::win
