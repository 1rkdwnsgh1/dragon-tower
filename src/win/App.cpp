#include "App.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <string>

#include "Platform.h"
#include "core/Config.h"
#include "core/DragonData.h"

using namespace Gdiplus;

namespace dt::win {

namespace {
const wchar_t* kWindowClass = L"DragonTowerWindow";
constexpr float kButtonLogical = 64.f;
constexpr float kButtonMargin = 16.f;

bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
}  // namespace

// ---------------------------------------------------------------------------
// Surface
// ---------------------------------------------------------------------------

bool Surface::create(int width, int height) {
    destroy();
    w = std::max(1, width);
    h = std::max(1, height);
    HDC screen = GetDC(nullptr);
    dc = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // 위에서 아래로
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!dc || !bmp || !bits) {
        destroy();
        return false;
    }
    old = SelectObject(dc, bmp);
    gbmp = std::make_unique<Bitmap>(w, h, w * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
    return gbmp->GetLastStatus() == Ok;
}

void Surface::destroy() {
    gbmp.reset();
    if (dc && old) SelectObject(dc, old);
    if (bmp) DeleteObject(bmp);
    if (dc) DeleteDC(dc);
    dc = nullptr;
    bmp = nullptr;
    old = nullptr;
    bits = nullptr;
}

void Surface::present(HWND hwnd, int x, int y) {
    if (!dc || !hwnd) return;
    POINT src{0, 0};
    POINT dst{x, y};
    SIZE size{w, h};
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    HDC screen = GetDC(nullptr);
    UpdateLayeredWindow(hwnd, screen, &dst, &size, dc, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

// ---------------------------------------------------------------------------
// 초기화
// ---------------------------------------------------------------------------

bool App::init(HINSTANCE inst) {
    inst_ = inst;
    if (!renderer_.init()) return false;

    // v0.2: 이미지를 읽기 전에 종 폴더를 먼저 읽는다(새 종의 이미지도 읽어야 하므로).
    std::filesystem::path assetDir = exeDirectory() / L"assets";
    DragonScanResult scan = loadDragonFolders(assetDir / "dragons");
    sprites_.load(assetDir);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &App::StaticProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return false;

    // 클릭 통과·비활성 상태로 시작한다. 입력을 켜면 스타일을 바꾼다.
    DWORD ex = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
    overlay_ = CreateWindowExW(ex, kWindowClass, L"Dragon Tower", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, inst, this);
    button_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kWindowClass,
                              L"Dragon Tower 설정", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, inst, this);
    if (!overlay_ || !button_) return false;

    layout();

    // 저장 불러오기
    savePath_ = saveFilePath();
    SaveData save;
    bool corrupted = false;
    std::error_code ec;
    if (std::filesystem::exists(savePath_, ec)) {
        if (!loadFromFile(savePath_, save)) {
            corrupted = true;
            std::filesystem::path backup = savePath_;
            backup += ".broken-" + std::to_string(static_cast<long long>(std::time(nullptr)));
            std::filesystem::rename(savePath_, backup, ec);
            save = SaveData{};
        }
    }
    game_.seed(static_cast<uint32_t>(GetTickCount()));
    game_.init(save, RectF(0.f, 0.f, overlaySurf_.w / scale_, overlaySurf_.h / scale_));
    if (corrupted) game_.notify("저장 파일을 읽을 수 없어 백업해 두고 새로 시작합니다.", 0xFFFFD166, 8.f);
    if (sprites_.loadedCount() == 0 && sprites_.sheetCount() == 0)
        game_.notify("assets 폴더에 이미지가 없어 임시 도형으로 표시합니다.", 0xFFAAB2C4, 5.f);

    // 데이터 파일·시트의 문제는 조용히 넘기지 않고 화면에 알린다(오타를 바로 찾을 수 있게).
    if (scan.newSpecies > 0)
        game_.notify("드래곤 폴더에서 " + std::to_string(scan.newSpecies) + "종을 추가로 읽었습니다.", 0xFF8CFFA0, 5.f);
    int warned = 0;
    for (const std::string& w : scan.warnings) {
        if (warned++ >= 3) break;
        game_.notify("드래곤 데이터: " + w, 0xFFFFD166, 10.f);
    }
    for (const std::string& w : sprites_.warnings()) {
        if (warned++ >= 5) break;
        game_.notify("이미지: " + w, 0xFFFFD166, 10.f);
    }
    int more = static_cast<int>(scan.warnings.size() + sprites_.warnings().size()) - warned;
    if (more > 0) game_.notify("경고 " + std::to_string(more) + "건이 더 있습니다.", 0xFFFFD166, 10.f);

    ShowWindow(overlay_, SW_SHOWNOACTIVATE);
    ShowWindow(button_, SW_SHOWNOACTIVATE);
    // 처음 선택 화면이면 바로 고를 수 있도록 입력을 켠다.
    setInputEnabled(game_.needsInput());
    render();
    return true;
}

void App::layout() {
    work_ = primaryWorkArea();
    scale_ = dpiScale();
    int w = work_.right - work_.left;
    int h = work_.bottom - work_.top;
    overlaySurf_.create(w, h);
    SetWindowPos(overlay_, HWND_TOPMOST, work_.left, work_.top, w, h, SWP_NOACTIVATE);

    buttonPx_ = static_cast<int>(std::lround(kButtonLogical * scale_));
    int margin = static_cast<int>(std::lround(kButtonMargin * scale_));
    buttonX_ = work_.right - margin - buttonPx_;
    buttonY_ = work_.bottom - margin - buttonPx_;
    buttonSurf_.create(buttonPx_, buttonPx_);
    SetWindowPos(button_, HWND_TOPMOST, buttonX_, buttonY_, buttonPx_, buttonPx_, SWP_NOACTIVATE);
    game_.setDesktopBounds(RectF(0.f, 0.f, w / scale_, h / scale_));
}

void App::shutdown() {
    overlaySurf_.destroy();
    buttonSurf_.destroy();
    renderer_.shutdown();
}

// ---------------------------------------------------------------------------
// 입력 전환
// ---------------------------------------------------------------------------

void App::setInputEnabled(bool on) {
    inputEnabled_ = on;
    LONG_PTR ex = GetWindowLongPtrW(overlay_, GWL_EXSTYLE);
    if (on) ex &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    else ex |= static_cast<LONG_PTR>(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    SetWindowLongPtrW(overlay_, GWL_EXSTYLE, ex);
    SetWindowPos(overlay_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    SetWindowPos(button_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (on) {
        SetForegroundWindow(overlay_);
        SetFocus(overlay_);
    }
    for (bool& k : keyPrev_) k = true;  // 켜는 순간 눌려 있던 키는 무시
}

PlayerInput App::pollKeys() {
    PlayerInput in;
    if (!inputEnabled_ || hidden_ || GetForegroundWindow() != overlay_) {
        for (bool& k : keyPrev_) k = true;
        return in;
    }
    auto edge = [&](int vk) {
        bool d = keyDown(vk);
        bool pressed = d && !keyPrev_[vk & 0xFF];
        keyPrev_[vk & 0xFF] = d;
        return pressed;
    };
    float right = (keyDown('D') || keyDown(VK_RIGHT)) ? 1.f : 0.f;
    float left = (keyDown('A') || keyDown(VK_LEFT)) ? 1.f : 0.f;
    float down = (keyDown('S') || keyDown(VK_DOWN)) ? 1.f : 0.f;
    float up = (keyDown('W') || keyDown(VK_UP)) ? 1.f : 0.f;
    in.moveX = right - left;
    in.moveY = down - up;
    in.dodge = edge(VK_SPACE);
    bool s1 = edge('1');
    bool s2 = edge('2');
    bool s3 = edge('3');
    bool n1 = edge(VK_NUMPAD1);
    bool n2 = edge(VK_NUMPAD2);
    bool n3 = edge(VK_NUMPAD3);
    in.skill[0] = s1 || n1;
    in.skill[1] = s2 || n2;
    in.skill[2] = s3 || n3;

    if (edge(VK_ESCAPE)) {
        Screen sc = game_.screen();
        if (sc == Screen::AutoWarning || sc == Screen::TowerWarning || sc == Screen::Codex) game_.closeModal();
        else if (sc == Screen::Desktop) setInputEnabled(false);
    }
    return in;
}

// ---------------------------------------------------------------------------
// 루프
// ---------------------------------------------------------------------------

int App::run() {
    running_ = true;
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    const double frame = 1.0 / 30.0;
    MSG msg;
    while (running_) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running_ = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running_) break;

        auto now = clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        dt = std::min(dt, 0.1f);
        tick(dt);

        double spent = std::chrono::duration<double>(clock::now() - now).count();
        double wait = frame - spent;
        if (wait > 0.002) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(wait * 1000.0), QS_ALLINPUT);
        }
    }
    saveNow();
    return 0;
}

void App::updateFullscreenState() {
    bool fs = isOtherAppFullscreen(overlay_, button_);
    if (fs == hidden_) return;
    hidden_ = fs;
    if (hidden_) {
        // 숨김은 일시정지가 아니다. 자동 사냥은 계속 진행된다.
        ShowWindow(overlay_, SW_HIDE);
        ShowWindow(button_, SW_HIDE);
    } else {
        ShowWindow(overlay_, SW_SHOWNOACTIVATE);
        ShowWindow(button_, SW_SHOWNOACTIVATE);
        SetWindowPos(overlay_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(button_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void App::tick(float dt) {
    time_ += dt;
    fullscreenTimer_ += dt;
    if (fullscreenTimer_ >= 0.25f) {
        fullscreenTimer_ = 0.f;
        updateFullscreenState();
    }
    topmostTimer_ += dt;
    if (topmostTimer_ >= 2.f && !hidden_) {
        topmostTimer_ = 0.f;
        SetWindowPos(overlay_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(button_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    PlayerInput in = pollKeys();
    game_.update(dt, in, hidden_, inputEnabled_);
    if (game_.consumeSaveRequest()) saveNow();
    if (!hidden_) render();
}

void App::render() {
    if (overlaySurf_.gbmp) {
        Graphics g(overlaySurf_.gbmp.get());
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.Clear(Color(0, 0, 0, 0));
        g.SetCompositingMode(CompositingModeSourceOver);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.ScaleTransform(scale_, scale_);
        RenderContext ctx;
        ctx.inputEnabled = inputEnabled_;
        ctx.time = time_;
        ctx.mouseX = mouseX_;
        ctx.mouseY = mouseY_;
        renderer_.drawOverlay(g, game_, sprites_, ctx, buttons_);
        g.Flush(FlushIntentionSync);
        overlaySurf_.present(overlay_, work_.left, work_.top);
    }
    if (buttonSurf_.gbmp) {
        Graphics g(buttonSurf_.gbmp.get());
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.Clear(Color(0, 0, 0, 0));
        g.SetCompositingMode(CompositingModeSourceOver);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.ScaleTransform(scale_, scale_);
        bool attention = !inputEnabled_ && (game_.needsInput() || (game_.combatPaused() && !hidden_));
        renderer_.drawSettingsButton(g, kButtonLogical, sprites_, inputEnabled_, attention, buttonHover_, time_);
        g.Flush(FlushIntentionSync);
        buttonSurf_.present(button_, buttonX_, buttonY_);
    }
}

// ---------------------------------------------------------------------------
// 클릭 처리
// ---------------------------------------------------------------------------

void App::onClick(float lx, float ly) {
    for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it) {
        if (it->enabled && it->rect.Contains(PointF(lx, ly))) {
            doAction(*it);
            return;
        }
    }
}

void App::doAction(const UiButton& b) {
    switch (b.action) {
        case UiAction::ChooseSpecies: game_.chooseSpecies(static_cast<Species>(b.arg)); break;
        case UiAction::StartHunt: game_.startHunt(); break;
        case UiAction::StopHunt: game_.stopHunt(); break;
        case UiAction::SetMode: game_.setHuntMode(static_cast<CombatMode>(b.arg)); break;
        case UiAction::OpenTower: game_.openTowerWarning(); break;
        case UiAction::EnterTower: game_.enterTower(static_cast<CombatMode>(b.arg)); break;
        case UiAction::CloseModal: game_.closeModal(); break;
        case UiAction::ConfirmAuto: game_.confirmAutoWarning(); break;
        case UiAction::NextFloor: game_.nextFloor(); break;
        case UiAction::LeaveTower: game_.leaveTower(); break;
        case UiAction::OpenCodex: game_.openCodex(); break;
        case UiAction::AckDeath: game_.acknowledgeDeath(); break;
        case UiAction::ExitGame: requestExit(); return;
        case UiAction::None: break;
    }
    if (game_.consumeSaveRequest()) saveNow();
}

void App::saveNow() {
    SaveData snap = game_.snapshotForSave();
    if (!saveToFile(snap, savePath_) && !saveFailedNotified_) {
        saveFailedNotified_ = true;
        game_.notify("저장에 실패했습니다. 저장 폴더 권한을 확인해 주세요.", 0xFFFF7070, 8.f);
    }
}

void App::requestExit() {
    saveNow();
    running_ = false;
    if (button_) DestroyWindow(button_);
    button_ = nullptr;
    if (overlay_) DestroyWindow(overlay_);
    overlay_ = nullptr;
    PostQuitMessage(0);
}

// ---------------------------------------------------------------------------
// 창 메시지
// ---------------------------------------------------------------------------

LRESULT CALLBACK App::StaticProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handle(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    const bool isButton = hwnd == button_;
    switch (msg) {
        case WM_MOUSEACTIVATE:
            // 설정 버튼은 포커스를 빼앗지 않는다. 오버레이는 입력이 켜졌을 때만 활성화된다.
            if (isButton || !inputEnabled_) return MA_NOACTIVATE;
            return MA_ACTIVATE;

        case WM_MOUSEMOVE: {
            float x = static_cast<float>(static_cast<short>(LOWORD(lp)));
            float y = static_cast<float>(static_cast<short>(HIWORD(lp)));
            if (isButton) {
                if (!buttonHover_) {
                    TRACKMOUSEEVENT tme{};
                    tme.cbSize = sizeof(tme);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = hwnd;
                    TrackMouseEvent(&tme);
                }
                buttonHover_ = true;
            } else {
                mouseX_ = x / scale_;
                mouseY_ = y / scale_;
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (isButton) buttonHover_ = false;
            return 0;

        case WM_LBUTTONDOWN:
            if (isButton) {
                setInputEnabled(!inputEnabled_);
            } else if (inputEnabled_) {
                float x = static_cast<float>(static_cast<short>(LOWORD(lp)));
                float y = static_cast<float>(static_cast<short>(HIWORD(lp)));
                onClick(x / scale_, y / scale_);
            }
            return 0;

        case WM_RBUTTONDOWN:
            if (isButton) setInputEnabled(!inputEnabled_);
            return 0;

        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, isButton ? IDC_HAND : IDC_ARROW));
            return TRUE;

        case WM_DISPLAYCHANGE:
            if (!isButton) layout();
            return 0;

        case WM_SETTINGCHANGE:
            if (!isButton && wp == SPI_SETWORKAREA) layout();
            return 0;

        case WM_QUERYENDSESSION:
            saveNow();
            return TRUE;

        case WM_ENDSESSION:
            if (wp) saveNow();
            return 0;

        case WM_CLOSE:
            if (!isButton) requestExit();
            return 0;

        case WM_DESTROY:
            if (!isButton) PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace dt::win
