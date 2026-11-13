// 바탕화면 오버레이 창, 설정 버튼 창, 메인 루프
#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "Renderer.h"
#include "Sprites.h"
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
    void updateClickThrough();   // 커서가 우리 요소 위에 있을 때만 입력을 받는다
    void createTray();
    void destroyTray();
    void showTrayMenu();
    void refreshTrayTip();
    bool startDrag(float lx, float ly);  // 상태창·드래곤 끌기 시작

    HINSTANCE inst_ = nullptr;
    HWND overlay_ = nullptr;
    HWND button_ = nullptr;
    Surface overlaySurf_;
    Surface buttonSurf_;
    RECT work_{};
    float scale_ = 1.f;
    int buttonPx_ = 64;
    int buttonX_ = 0;
    int buttonY_ = 0;

    bool running_ = false;
    bool inputEnabled_ = false;
    bool hidden_ = false;
    bool buttonHover_ = false;
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
    enum class Drag { None, Panel, Dragon };
    Drag drag_ = Drag::None;
    float dragDX_ = 0.f, dragDY_ = 0.f;
    // v0.6 트레이 아이콘
    NOTIFYICONDATAW tray_{};
    bool trayAdded_ = false;
    // v0.6 조용할 때 프레임을 줄인다
    float idleAccum_ = 0.f;

    Game game_;
    Renderer renderer_;
    Sprites sprites_;
    std::vector<UiButton> buttons_;
    std::filesystem::path savePath_;
};

}  // namespace dt::win
