// GDI+로 게임 화면을 그린다. 이미지가 없으면 임시 도형으로 대신한다.
#pragma once

#include <array>
#include <map>
#include <tuple>
#include <memory>
#include <string>
#include <vector>

#include "FrameCache.h"
#include "Sprites.h"
#include "WinCommon.h"
#include "core/FrameStats.h"
#include "core/Game.h"

namespace dt::win {

enum class UiAction {
    None,
    DragPanel,        // 상태창 끌기 손잡이
    TogglePanel,      // 상태창 접기/펼치기
    ToggleConfine,    // 드래곤을 아래 띠에만 둘지
    DragDragon,       // 드래곤 끌어 옮기기
    DragMenu,         // 메뉴 창 끌기 손잡이
    DragMenuSize,     // 메뉴 오른쪽 아래 모서리를 끌어 크기 바꾸기
    CloseMenu,        // 메뉴 닫기(X)
    SetMenuTab,       // arg = 0 전투 설정 / 1 환경 설정
    DragCharSlider,   // 캐릭터 크기 슬라이더 끌기
    SetMotion,        // arg = AnimState. 고른 모션을 다음 입력까지 유지
    ClearMotion,      // 모션 고정 풀기
    TogglePowerSave,  // 조용할 때 프레임 줄이기
    ToggleTaskbarSkin,  // v1.9: 작업표시줄 꾸미기
    SetKeepRange,     // 싸울 때 유지할 간격. arg = 0 근접 / 1 중간 / 2 바깥
    ToggleMenu,       // 메뉴 창 열고 닫기(키보드 조작과 무관)
    OpenStable,       // 드래곤 관리창 열기
    CloseStable,      // 관리창 닫기
    OpenSkills,       // v1.4 스킬 관리창 열기
    CloseSkills,      // 스킬 관리창 닫기
    SelectSlot,       // arg = 장착 칸(0~3). 이 칸에 다음으로 고른 스킬이 들어간다
    EquipSkill,       // arg = 스킬 목록 번호. 고른 칸에 넣는다
    ClearSlot,        // arg = 장착 칸. 비운다
    SkillPage,        // arg = 스킬 목록 쪽 번호
    ShowDragon,       // arg = 보유 목록 자리. 바탕화면에 띄운다(= 플레이할 드래곤)
    AddDragon,        // 새 드래곤 데려오기(선택 화면으로)
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
    // 화면 배율(DPI). UI 조각 캐시가 장치 픽셀 크기로 미리 그리는 데 쓴다.
    // 1 이 아니면 논리 좌표와 실제 픽셀 수가 다르다.
    float scale = 1.f;
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
    ~Renderer();

    // 전체 오버레이(바탕화면 크기). 눌릴 수 있는 버튼 목록을 outButtons에 채운다.
    void drawOverlay(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                     std::vector<UiButton>& outButtons, FrameRegions* outRegions = nullptr);

    // 제작 도구용. 게임과 같은 본체, 투사체, 효과를 HUD 없이 그린다.
    void drawAnimationPreview(Gdiplus::Graphics& g, const World& world, const Sprites& sprites,
                              float time, bool body, bool effects, float charScale = 1.f);
    void drawEnemyPreview(Gdiplus::Graphics& g, const Enemy& enemy, const Sprites& sprites,
                          AnimState state, float time, float charScale);

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
    // #50: 같은 크기·투명도의 조각은 한 번만 합성해 두고 다시 붙인다(piece 가 부른다).
    void panel(Gdiplus::Graphics& g, const Gdiplus::RectF& r, uint8_t alpha = 236);
    void button(Gdiplus::Graphics& g, std::vector<UiButton>& out, const RenderContext& ctx, const Gdiplus::RectF& r,
                const std::string& label, UiAction action, int arg = 0, bool enabled = true, bool selected = false,
                bool danger = false);
    void bar(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float ratio, uint32_t color, const std::string& label = "");

    struct CurseBadge {
        const Enemy* enemy;
        float x, top;
    };
    struct CurseFlash {
        int stackAtLastDraw = 0;  // 증가 감지용. 표시값은 매번 Enemy에서 읽는다.
        double remainingAtLastDraw = 0.0;
        float started = 0.f;
        bool seen = false;
    };
    void drawCurseBadge(Gdiplus::Graphics& g, const CurseBadge& badge, float time);
    std::vector<CurseBadge> curseBadges_;
    std::map<int, CurseFlash> curseFlashes_;

    void drawWorld(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx);
    void drawDragon(Gdiplus::Graphics& g, const DragonActor& d, const Sprites& sprites, float time, float x, float y,
                    float height, bool preview);
    // charScale은 보기 배수다. 그림 크기만 바꾸고 판정에는 쓰지 않는다.
    void drawDotZones(Gdiplus::Graphics& g, const World& w, const Sprites& sprites, float time);
    void drawBreath(Gdiplus::Graphics& g, const DragonActor& dragon, const Sprites& sprites, float charScale);
    // 장착한 숨결이 있으면 지금 배율의 16장을 뒤에서 미리 만들어 둔다(첫 숨결이 끊기지 않게).
    void warmBreathCache(Gdiplus::Graphics& g, const World& w, const Sprites& sprites, float charScale);

    // --- 그림 캐시 (FrameCache.h) ---------------------------------------------
    // 시트의 한 칸(src)을 (cx, cy) 가운데에 w x h(논리 크기)로 그린다. 지금 변환의 배율을 보고 장치 픽셀
    // 크기로 키워 둔 그림을 쓰고, 아직 없으면 잘라 둔 칸을 대신 그린다. drawSprite·drawSheetFrame·
    // drawBreath 가 모두 여기로 온다 — 새 스킬 효과도 이 길로 그려지면 따로 할 일이 없다.
    void drawFrameCached(Gdiplus::Graphics& g, const FrameCache::Source& src, float cx, float cy, float w, float h,
                         bool flip, float flash, float alpha);
    // 예전 그리기(캐시를 끄거나 칸을 자를 수 없을 때).
    void drawFrameDirect(Gdiplus::Graphics& g, const FrameCache::Source& src, float cx, float cy, float w, float h,
                         bool flip, float flash, float alpha);
    // 지금 변환에서 w x h 가 장치 픽셀로 몇 x 몇인가(한 장 상한을 넘으면 줄인 크기). 상한에 걸리면 false.
    static bool frameDeviceSize(Gdiplus::Graphics& g, float w, float h, int& devW, int& devH);
    FrameCache frameCache_;
    // 숨결 미리 만들기: 크기를 끄는 동안 매번 맡기지 않게 같은 배율이 몇 프레임 이어진 뒤에 맡긴다.
    float breathWarmScale_ = 0.f;
    int breathWarmFrames_ = 0;
    void drawProcession(Gdiplus::Graphics& g, const DragonActor& dragon, const Sprites& sprites,
                        float scale, bool inFront);
    void drawProjectiles(Gdiplus::Graphics& g, const World& w, const Sprites& sprites,
                         const SpeciesDef& dragonSp, float cs);
    void drawEnemy(Gdiplus::Graphics& g, const Enemy& e, const Sprites& sprites, float time, float charScale,
                   const AnimState* forcedState = nullptr);
    void drawSprite(Gdiplus::Graphics& g, Gdiplus::Bitmap* bmp, float cx, float cy, float height, bool flip,
                    float flash, float alpha);
    // 시트의 한 칸만 그린다. frame은 0부터.
    void drawSheetFrame(Gdiplus::Graphics& g, const Sheet& sh, int frame, float cx, float cy, float height, bool flip,
                        float flash, float alpha);
    // v1.6: 효과를 한 층만 그린다. inFront 가 false 면 본체 뒤 효과(Fx::inFront == false)만,
    // true 면 본체 위 효과만 그린다 — 그래서 드래곤 앞뒤로 두 번 부른다.
    // sp·charScale 은 스프라이트 효과의 크기 기준(본체 크기)과 fx_ 시트를 찾는 데 쓴다.
    void drawFx(Gdiplus::Graphics& g, const World& w, const Sprites& sprites, const SpeciesDef& sp,
                float charScale, bool inFront);
    // 효과 하나를 fx_<key>.png 시트로 그린다. 시트가 없으면 false — 부르는 쪽이 도형으로 그린다.
    bool drawFxSheet(Gdiplus::Graphics& g, const Fx& f, const Sprites& sprites, const SpeciesDef& sp,
                     float charScale);

    void drawStatusCard(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                        std::vector<UiButton>& out);
    // 켜짐/꺼짐 토글 스위치. 누르면 action이 발생한다.
    void toggle(Gdiplus::Graphics& g, std::vector<UiButton>& out, const RenderContext& ctx, const Gdiplus::RectF& r,
                const std::string& label, bool on, UiAction action, int arg, bool enabled);
    // v1.4 값을 끌어서 고르는 막대
    void slider(Gdiplus::Graphics& g, std::vector<UiButton>& out, const RenderContext& ctx,
                const Gdiplus::RectF& r, float value, float lo, float hi, UiAction action);
    // v1.5 드래곤을 오른쪽 클릭하면 뜨는 모션 목록
    void drawMotionMenu(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites,
                        const RenderContext& ctx, std::vector<UiButton>& out);
    void drawMenu(Gdiplus::Graphics& g, const Game& game, const RenderContext& ctx, std::vector<UiButton>& out);
    void drawNotices(Gdiplus::Graphics& g, const Game& game);
    // 큐 11번: 프레임 측정 표시. 트레이 메뉴로 켜야 그려진다.
    void drawFrameStats(Gdiplus::Graphics& g, const FrameStats& st);
    void drawTowerHud(Gdiplus::Graphics& g, const Game& game);
    void drawModal(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                   std::vector<UiButton>& out);
    void drawSelect(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                    std::vector<UiButton>& out);
    void drawCodex(Gdiplus::Graphics& g, const Game& game, const RenderContext& ctx, std::vector<UiButton>& out);
    // v1.3 드래곤 관리창: 보유 목록, 바탕화면에 띄울 개체 고르기, 묘비
    void drawStable(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites, const RenderContext& ctx,
                    std::vector<UiButton>& out);
    // v1.4 스킬 관리창: 장착 4칸과 그 종의 스킬 목록(해제 레벨·패시브 표시)
    void drawSkills(Gdiplus::Graphics& g, const Game& game, const Sprites& sprites,
                    const RenderContext& ctx,
                    std::vector<UiButton>& out);
    // 스킬 한 칸(아이콘·대기시간·잠금)을 그린다. 상태창과 관리창이 함께 쓴다.
    // skillIndex < 0 이면 빈 칸으로 그린다.
    void drawSkillBox(Gdiplus::Graphics& g, const Sprites& sprites, const SpeciesDef& sp,
                      const DragonActor& d, int skillIndex,
                      const Gdiplus::RectF& b, int level, const std::string& cornerLabel);

    // --- UI 조각 캐시 (큐 12번) ---------------------------------------------
    // drawPiece 는 (그림, 목적지 크기, 투명도)의 **순수 함수**다 — 위치는 평행이동일 뿐이다.
    // 그래서 결과를 장치 픽셀 크기로 한 번 그려 두고 다음부터는 1:1 로 붙이기만 한다.
    // 상태를 추적하지 않으므로 **UI 가 낡을 위험이 없다**(같은 입력이면 같은 그림이다).
    //
    // 왜 필요한가: panel.png 가 1109x1382 인데 9조각이라 한 번 그릴 때 DrawImage 가 9번이고,
    // 전부 ColorMatrix 를 써서 GDI+ 의 빠른 경로를 못 탄다. 스킬 창은 줄이 7개라
    // 한 프레임에 DrawImage 가 70번이 넘었다. 측정은 implementation-notes.md 참고.
    struct PieceKey {
        const Gdiplus::Bitmap* bmp = nullptr;
        int fit = 0;
        int l = 0, t = 0, r = 0, b = 0;  // 9조각 테두리
        int devW = 0, devH = 0;          // 장치 픽셀 크기
        int alphaQ = 0;                  // 투명도(0~255로 양자화)
        bool operator<(const PieceKey& o) const {
            return std::tie(bmp, fit, l, t, r, b, devW, devH, alphaQ) <
                   std::tie(o.bmp, o.fit, o.l, o.t, o.r, o.b, o.devW, o.devH, o.alphaQ);
        }
    };
    struct PieceEntry {
        std::unique_ptr<Gdiplus::Bitmap> bmp;  // 없으면 "그리지 않는다"는 결과를 캐시한 것
        Gdiplus::RectF contentDev{};           // 캐시 원점 기준 장치 픽셀
        bool drawn = false;
    };
    std::map<PieceKey, PieceEntry> pieceCache_;
    size_t pieceCacheBytes_ = 0;

    // 캐시를 거쳐 조각을 그린다. 캐시가 꺼져 있거나 쓸 수 없으면 예전처럼 바로 그린다.
    bool drawPieceCached(Gdiplus::Graphics& g, const UiPiece& p, const Gdiplus::RectF& dst, float alpha,
                         float scale, Gdiplus::RectF* content);

public:
    // 측정 표시에 쓴다(캐시가 실제로 쌓이는지 사람이 볼 수 있게).
    size_t uiCacheCount() const { return pieceCache_.size(); }
    size_t uiCacheBytes() const { return pieceCacheBytes_; }

    // v1.9: 이번 프레임에 실제로 그린 상태창·메뉴 창의 네모.
    // 끌어서 옮길 때 화면 밖으로 나가지 않게 App 이 이 크기로 자른다.
    // 그리는 쪽만 잘라 두면 저장되는 좌표는 화면 밖 값이라, 가장자리에서 손이 떨어졌다 붙으면
    // 창이 튀어 보였다(v1.8 까지의 증상).
    Gdiplus::RectF lastCardRect() const { return lastCardRect_; }
    Gdiplus::RectF lastMenuRect() const { return lastMenuRect_; }

private:
    Gdiplus::RectF lastCardRect_{0.f, 0.f, 0.f, 0.f};
    Gdiplus::RectF lastMenuRect_{0.f, 0.f, 0.f, 0.f};
    std::unique_ptr<Gdiplus::FontFamily> family_;
    std::map<int, std::unique_ptr<Gdiplus::Font>> fonts_;
    float width_ = 0.f;
    float height_ = 0.f;
    // 이번 프레임의 화면 배율. drawOverlay 시작에서 ctx.scale 로 정한다(UI 조각 캐시가 쓴다).
    float scale_ = 1.f;
    // 한 프레임 동안 쓸 스킨. drawOverlay 시작에서 정한다.
    const Sprites* sprites_ = nullptr;
    std::string skin_ = "default";

};

}  // namespace dt::win
