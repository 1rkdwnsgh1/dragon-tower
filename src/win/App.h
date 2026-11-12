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

    Game game_;
    Renderer renderer_;
    Sprites sprites_;
    std::vector<UiButton> buttons_;
    std::filesystem::path savePath_;
};

}  // namespace dt::win
