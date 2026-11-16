// 바탕화면 오버레이 창, 설정 버튼 창, 메인 루프
#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "Renderer.h"
#include "Sprites.h"
#include <shellapi.h>  // NOTIFYICONDATAW (트레이 아이콘)

#include "WinCommon.h"
#include "core/Game.h"

namespace dt::win {

// UpdateLayeredWindow에 쓰는 32비트 알파 그림판
struct Surface {
    int w = 0;
    int h = 0;
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ old = nullptr;
    void* bits = nullptr;
    std::unique_ptr<Gdiplus::Bitmap> gbmp;

    bool create(int width, int height);
    void destroy();
    void present(HWND hwnd, int x, int y);
    ~Surface() { destroy(); }
};

class App {
public:
    bool init(HINSTANCE inst);
    int run();
    void shutdown();

private:
    static LRESULT CALLBACK StaticProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void layout();
    void setInputEnabled(bool on);
    void tick(float dt);
    void render();
    PlayerInput pollKeys();
    void onClick(float lx, float ly);
    void doAction(const UiButton& b);
    void saveNow();
    void requestExit();
    void updateFullscreenState();
    // v0.6
    void syncKeyboard();  // 게임 상태에 맞춰 키보드 받기를 켜고 끈다
    void updateClickThrough();   // 커서가 우리 요소 위에 있을 때만 입력을 받는다
    void createTray();
    void destroyTray();
    void showTrayMenu();
    void refreshTrayTip();
    void showBalloon(const std::string& title, const std::string& body, bool warning);
    void pumpAlerts();
    bool startDrag(float lx, float ly);  // 상태창·메뉴·드래곤 끌기 시작
    // v1.9: 끌어 옮긴 창이 화면 밖으로 나가지 않게 자른다(drawn 은 이번 프레임에 그린 네모)
    Vec2 clampToDesktop(Vec2 p, const Gdiplus::RectF& drawn) const;
    void updateTaskbarSkin();            // v1.9: 작업표시줄 꾸미기 창을 따라다니게 한다
    void setUserHidden(bool on);         // v1.9: Ctrl+Alt+D / 트레이로 전부 숨기기

    HINSTANCE inst_ = nullptr;
    HWND overlay_ = nullptr;
    Surface overlaySurf_;
    RECT work_{};
    float scale_ = 1.f;

    bool running_ = false;
    bool inputEnabled_ = false;
    bool hidden_ = false;
    // v1.9: 사람이 직접 숨긴 상태(Ctrl+Alt+D·트레이). 다른 앱 전체화면 때문에 숨는 것과 구분한다.
    // 하나로 쓰면 전체화면 감시(0.25초마다)가 사람이 숨긴 것을 바로 되살려 버린다(v1.8 까지의 버그).
    bool userHidden_ = false;
    // v1.9: 작업표시줄 꾸미기 창(별도 레이어 창, 클릭은 항상 통과시킨다)
    HWND taskbar_ = nullptr;
    Surface taskbarSurf_;
    RECT taskbarRect_{};
    std::string taskbarSkinKey_;   // 지금 그려 둔 스킨. 드래곤을 바꾸면 다시 그린다
    bool taskbarDrawn_ = false;
    float time_ = 0.f;
    float fullscreenTimer_ = 0.f;
    float topmostTimer_ = 0.f;
    bool saveFailedNotified_ = false;
    float mouseX_ = -1.f;
    float mouseY_ = -1.f;
    bool keyPrev_[256] = {};

    // v0.6 클릭 통과: 커서가 요소 위에 있는 동안만 입력을 받는다
    FrameRegions regions_;
    bool cursorOnUi_ = false;
    // v0.6 끌기
    enum class Drag { None, Panel, Menu, MenuSize, Dragon, CharSlider };
    float dragStartScale_ = 1.f;  // 메뉴 크기 끌기를 시작할 때의 배수
    Drag drag_ = Drag::None;
    float dragDX_ = 0.f, dragDY_ = 0.f;
    // v0.6 트레이 아이콘
    NOTIFYICONDATAW tray_{};
    bool trayAdded_ = false;
    UINT taskbarCreatedMsg_ = 0;  // 탐색기가 다시 시작하면 트레이 아이콘을 다시 넣는다
    // v0.6 조용할 때 프레임을 줄인다
    float idleAccum_ = 0.f;

    Game game_;
    Renderer renderer_;
    Sprites sprites_;
    std::vector<UiButton> buttons_;
    std::filesystem::path savePath_;
};

}  // namespace dt::win
