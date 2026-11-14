#include "App.h"

#include <shellapi.h>

#include <chrono>
#include <cmath>
#include <ctime>
#include <string>

#include "Platform.h"
#include "core/Config.h"
#include "core/DragonData.h"
#include "core/EnemyData.h"

using namespace Gdiplus;

namespace dt::win {

namespace {
constexpr UINT kTrayMsg = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr int kHotkeyId = 1;
constexpr int kMenuToggleInput = 100;
constexpr int kMenuShowHide = 101;
constexpr int kMenuResetPanel = 102;
constexpr int kMenuExit = 103;
}  // namespace


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
    EnemyScanResult escan = loadEnemyFolders(assetDir / "enemies");
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
    if (escan.newEnemies > 0)
        game_.notify("적 폴더에서 " + std::to_string(escan.newEnemies) + "종을 추가로 읽었습니다.", 0xFF8CFFA0, 5.f);
    for (const std::string& w : escan.warnings) scan.warnings.push_back(w);
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
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    createTray();
    // Ctrl+Shift+D 로 키보드 조작을 켜고 끈다. 이미 쓰는 조합이면 조용히 넘어간다.
    if (!RegisterHotKey(overlay_, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'D')) {
        game_.notify("Ctrl+Shift+D 단축키를 등록하지 못했습니다(다른 앱이 쓰는 중).", 0xFFFFD166, 6.f);
    }
    refreshTrayTip();
    if (!game_.hintShown()) {
        game_.notify("창과 버튼은 언제나 마우스로 씁니다. Ctrl+Shift+D는 키보드 조작을 켜고 끕니다.", 0xFFAAD4FF, 12.f);
        game_.markHintShown();
        saveNow();
    }
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
    UnregisterHotKey(overlay_, kHotkeyId);
    destroyTray();
    overlaySurf_.destroy();
    buttonSurf_.destroy();
    renderer_.shutdown();
}

// ---------------------------------------------------------------------------
// 입력 전환
// ---------------------------------------------------------------------------

void App::setInputEnabled(bool on) {
    // v1.0: 이 스위치는 "키보드 조작"만 켜고 끈다. 마우스는 우리가 그린 것 위에서 항상 받는다.
    // 클릭 통과(WS_EX_TRANSPARENT)는 updateClickThrough 가 매 프레임 맡는다. 여기서 같이 건드리면
    // 커서가 UI 위에 있는데도 통과로 되돌려 놓는 일이 생긴다.
    inputEnabled_ = on;
    cursorOnUi_ = false;  // 다음 갱신에서 다시 판단하게 한다
    LONG_PTR ex = GetWindowLongPtrW(overlay_, GWL_EXSTYLE);
    ex |= static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    if (on) ex &= ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
    else ex |= static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
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
    // v1.4: 키 1~4는 **장착 칸**이다(스킬 목록 번호가 아니다).
    // edge()는 상태를 남기므로 모든 키를 매 프레임 한 번씩 꼭 읽어야 한다. 조건부로 건너뛰면
    // 눌림이 쌓여 다음에 엉뚱하게 발동한다.
    for (int i = 0; i < kSkillSlots; ++i) {
        bool num = edge('1' + i);
        bool pad = edge(VK_NUMPAD1 + i);
        in.skill[i] = num || pad;
    }

    if (edge(VK_ESCAPE)) {
        Screen sc = game_.screen();
        if (sc == Screen::AutoWarning || sc == Screen::TowerWarning || sc == Screen::Codex) game_.closeModal();
        else if (sc == Screen::Stable) game_.closeStable();  // 띄운 개체가 없으면 닫히지 않는다
        else if (sc == Screen::Skills) game_.closeSkills();
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

    // 끌기가 어떤 이유로든 끊겼으면 잡고 있는 상태도 푼다(드래곤이 굳어 버리지 않게).
    if (drag_ != Drag::Dragon && game_.dragonHeld()) game_.setDragonHeld(false);

    PlayerInput in = pollKeys();
    game_.update(dt, in, hidden_, inputEnabled_);
    if (game_.consumeSaveRequest()) saveNow();

    pumpAlerts();

    // 클릭 통과 범위는 매 프레임 갱신한다(요소가 움직이므로).
    updateClickThrough();

    if (hidden_) return;

    // v0.6에서는 조용할 때 항상 프레임을 줄였는데, 입력을 끄면 드래곤 움직임이 끊겨 보였다.
    // v0.8부터는 절전 설정을 켠 경우에만 줄이고, 드래곤이 걷거나 나는 동안에는 줄이지 않는다.
    if (!game_.powerSave()) {
        idleAccum_ = 0.f;
        render();
        return;
    }
    const DragonActor& dr = game_.world().dragon();
    bool dragonMoving = dr.alive && (dr.anim == AnimState::Move || dr.anim == AnimState::Fly ||
                                     dr.flying || dr.landing);
    bool busy = game_.hunting() || game_.inTowerFlow() || inputEnabled_ ||
                !game_.world().enemies().empty() || game_.world().hasActiveFx() ||
                game_.screen() != Screen::Desktop || drag_ != Drag::None || dragonMoving ||
                cursorOnUi_;
    if (busy) {
        idleAccum_ = 0.f;
        render();
        return;
    }
    idleAccum_ += dt;
    if (idleAccum_ >= cfg::kIdleFrameInterval) {
        idleAccum_ = 0.f;
        render();
    }
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
        renderer_.drawOverlay(g, game_, sprites_, ctx, buttons_, &regions_);
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
        case UiAction::TogglePanel: game_.setPanelCollapsed(!game_.panelCollapsed()); break;
        case UiAction::ToggleConfine: game_.setConfineToBand(!game_.confineToBand()); break;
        case UiAction::MenuScale:
            game_.setMenuScale(game_.menuScale() + b.arg * cfg::kMenuScaleStep);
            break;
        case UiAction::CharScale:
            game_.setCharScale(game_.charScale() + b.arg * cfg::kCharScaleStep);
            break;
        case UiAction::TogglePowerSave: game_.setPowerSave(!game_.powerSave()); break;
        case UiAction::ToggleMenu: game_.setMenuOpen(!game_.menuOpen()); break;
        case UiAction::OpenStable: game_.openStable(); break;
        case UiAction::CloseStable: game_.closeStable(); break;
        case UiAction::OpenSkills: game_.openSkills(); break;
        case UiAction::CloseSkills: game_.closeSkills(); break;
        case UiAction::SelectSlot: game_.selectSlot(b.arg); break;
        case UiAction::EquipSkill: game_.equipSkill(game_.selectedSlot(), b.arg); break;
        case UiAction::ClearSlot: game_.clearSlot(b.arg); break;
        case UiAction::SkillPage: game_.setSkillPage(b.arg); break;
        case UiAction::ShowDragon: game_.switchDragon(b.arg); break;
        case UiAction::AddDragon: game_.beginAddDragon(); break;
        case UiAction::SetKeepRange: game_.setKeepRange(b.arg); break;
        case UiAction::DragPanel:   // 끌기는 WM_LBUTTONDOWN에서 시작한다
        case UiAction::DragMenu:
        case UiAction::DragDragon:
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

// ---------------------------------------------------------------------------
// v0.6: 클릭 통과 · 트레이 아이콘 · 전역 단축키 · 끌기
// ---------------------------------------------------------------------------

void App::updateClickThrough() {
    // 우리가 그린 것 위에 커서가 있을 때만 입력을 받는다. 나머지는 바탕화면과 다른 창으로
    // 그대로 넘어간다(기획서의 "클릭 통과 범위" 결정).
    // v1.0: 키보드 조작과 상관없이, 우리가 그린 것 위에서는 언제나 받는다.
    bool want = false;
    if (!hidden_) {
        // 선택 화면·탑·모달은 배경 클릭을 막아야 하지만, 키보드 조작이 꺼져 있을 때까지
        // 화면 전체를 막으면 바탕화면을 쓸 수 없게 된다. 그때는 창 위에서만 받는다.
        if (regions_.fullScreen && inputEnabled_) {
            want = true;
        } else {
            POINT p{};
            GetCursorPos(&p);
            float lx = (p.x - work_.left) / scale_;
            float ly = (p.y - work_.top) / scale_;
            for (const Gdiplus::RectF& r : regions_.hit) {
                if (r.Contains(Gdiplus::PointF(lx, ly))) { want = true; break; }
            }
            if (drag_ != Drag::None) want = true;  // 끄는 중에는 놓칠 수 없다
        }
    }
    if (want == cursorOnUi_) return;
    cursorOnUi_ = want;
    LONG_PTR ex = GetWindowLongPtrW(overlay_, GWL_EXSTYLE);
    if (want) ex &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    else ex |= static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    SetWindowLongPtrW(overlay_, GWL_EXSTYLE, ex);
}

void App::createTray() {
    if (trayAdded_) return;
    tray_ = NOTIFYICONDATAW{};
    tray_.cbSize = sizeof(tray_);
    tray_.hWnd = overlay_;
    tray_.uID = kTrayId;
    tray_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray_.uCallbackMessage = kTrayMsg;
    tray_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(tray_.szTip, L"Dragon Tower");
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &tray_) != FALSE;
}

void App::destroyTray() {
    if (!trayAdded_) return;
    Shell_NotifyIconW(NIM_DELETE, &tray_);
    trayAdded_ = false;
}

void App::refreshTrayTip() {
    if (!trayAdded_) return;
    std::wstring tip = L"Dragon Tower — ";
    tip += inputEnabled_ ? L"키보드 조작 켜짐" : L"키보드 조작 꺼짐";
    if (hidden_) tip += L" (숨김)";
    wcsncpy_s(tray_.szTip, tip.c_str(), _TRUNCATE);
    tray_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &tray_);
}

void App::showBalloon(const std::string& title, const std::string& body, bool warning) {
    if (!trayAdded_) return;
    NOTIFYICONDATAW d{};
    d.cbSize = sizeof(d);
    d.hWnd = overlay_;
    d.uID = kTrayId;
    d.uFlags = NIF_INFO;
    d.dwInfoFlags = warning ? NIIF_WARNING : NIIF_INFO;
    wcsncpy_s(d.szInfoTitle, widen(title).c_str(), _TRUNCATE);
    wcsncpy_s(d.szInfo, widen(body).c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &d);
}

void App::pumpAlerts() {
    // 사망·탑 결과처럼 화면을 보고 있지 않아도 알아야 하는 일은 트레이 풍선으로 알린다.
    // 소리는 쓰지 않는다(기획서의 "소리 금지"). 한 프레임에 하나씩 내보낸다.
    if (!game_.hasAlert()) return;
    Game::Alert a = game_.takeAlert();
    if (a.title.empty()) return;
    showBalloon(a.title, a.body, a.warning);
}

void App::showTrayMenu() {
    HMENU m = CreatePopupMenu();
    if (!m) return;
    AppendMenuW(m, MF_STRING, kMenuToggleInput,
                inputEnabled_ ? L"키보드 조작 끄기 (Ctrl+Shift+D)" : L"키보드 조작 켜기 (Ctrl+Shift+D)");
    AppendMenuW(m, MF_STRING, kMenuShowHide, hidden_ ? L"드래곤 보이기" : L"드래곤 숨기기");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kMenuResetPanel, L"상태창 위치 되돌리기");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kMenuExit, L"종료");
    POINT p{};
    GetCursorPos(&p);
    SetForegroundWindow(overlay_);  // 메뉴가 바로 닫히지 않게 한다
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, overlay_, nullptr);
    PostMessageW(overlay_, WM_NULL, 0, 0);
    DestroyMenu(m);
}

bool App::startDrag(float lx, float ly) {
    for (const UiButton& b : buttons_) {
        if (!b.enabled || !b.rect.Contains(Gdiplus::PointF(lx, ly))) continue;
        if (b.action == UiAction::DragPanel || b.action == UiAction::DragMenu) {
            drag_ = b.action == UiAction::DragPanel ? Drag::Panel : Drag::Menu;
            // 창 왼쪽 위에서 커서까지의 거리를 기억한다
            dragDX_ = lx - b.rect.X;
            dragDY_ = ly - b.rect.Y;
            return true;
        }
        if (b.action == UiAction::DragDragon) break;
    }
    // v1.0: 드래곤은 언제나 끌 수 있다(마우스는 항상 받는다). 사냥 중에도 되고,
    // 전투 띠 밖으로는 나가지 않게 placeDragon 이 맞춘다. 탑에서는 화면이 따로 있어 끌지 않는다.
    if (game_.hasDragon() && !game_.inTowerFlow()) {
        const DragonActor& d = game_.world().dragon();
        float rr = cfg::kDragonRadius * 1.6f;
        if (std::fabs(lx - d.pos.x) <= rr && std::fabs(ly - d.pos.y) <= rr) {
            drag_ = Drag::Dragon;
            dragDX_ = lx - d.pos.x;
            dragDY_ = ly - d.pos.y;
            game_.setDragonHeld(true);  // 끄는 동안 스스로 움직이지 않게 한다
            return true;
        }
    }
    return false;
}

LRESULT App::handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    const bool isButton = hwnd == button_;
    // 탐색기가 다시 시작하면 트레이 아이콘이 사라진다. 다시 넣어 준다.
    if (!isButton && taskbarCreatedMsg_ != 0 && msg == taskbarCreatedMsg_) {
        trayAdded_ = false;
        createTray();
        refreshTrayTip();
        return 0;
    }
    switch (msg) {
        case WM_MOUSEACTIVATE:
            // 설정 버튼은 포커스를 빼앗지 않는다. 오버레이는 키보드 조작이 켜졌을 때만 활성화된다.
            // (마우스만 쓸 때 창을 활성화하면 쓰던 프로그램의 포커스를 뺏는다.)
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
                if (drag_ == Drag::Panel) {
                    game_.setPanelPos({mouseX_ - dragDX_, mouseY_ - dragDY_});
                } else if (drag_ == Drag::Menu) {
                    game_.setMenuPos({mouseX_ - dragDX_, mouseY_ - dragDY_});
                } else if (drag_ == Drag::Dragon) {
                    game_.placeDragon({mouseX_ - dragDX_, mouseY_ - dragDY_});
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (isButton) buttonHover_ = false;
            return 0;

        case WM_LBUTTONDOWN:
            if (isButton) {
                setInputEnabled(!inputEnabled_);
            } else {
                // v1.0: 우리가 그린 것 위에서는 키보드 조작이 꺼져 있어도 언제나 받는다.
                float x = static_cast<float>(static_cast<short>(LOWORD(lp)));
                float y = static_cast<float>(static_cast<short>(HIWORD(lp)));
                float lx = x / scale_, ly = y / scale_;
                SetCapture(hwnd);
                if (!startDrag(lx, ly)) onClick(lx, ly);
            }
            return 0;

        case WM_LBUTTONUP:
            if (!isButton) {
                if (drag_ != Drag::None) {
                    if (drag_ == Drag::Dragon) game_.setDragonHeld(false);
                    drag_ = Drag::None;
                    saveNow();  // 옮긴 위치를 바로 남긴다
                }
                if (GetCapture() == hwnd) ReleaseCapture();
            }
            return 0;

        case WM_RBUTTONDOWN:
            if (isButton) setInputEnabled(!inputEnabled_);
            else showTrayMenu();  // 오버레이에서 오른쪽 클릭하면 같은 메뉴
            return 0;

        case WM_HOTKEY:
            if (wp == kHotkeyId) {
                setInputEnabled(!inputEnabled_);
                refreshTrayTip();
            }
            return 0;

        case kTrayMsg:
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) showTrayMenu();
            else if (LOWORD(lp) == WM_LBUTTONDBLCLK) { setInputEnabled(!inputEnabled_); refreshTrayTip(); }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case kMenuToggleInput: setInputEnabled(!inputEnabled_); refreshTrayTip(); return 0;
                case kMenuShowHide:
                    hidden_ = !hidden_;
                    ShowWindow(overlay_, hidden_ ? SW_HIDE : SW_SHOWNOACTIVATE);
                    ShowWindow(button_, hidden_ ? SW_HIDE : SW_SHOWNOACTIVATE);
                    refreshTrayTip();
                    return 0;
                case kMenuResetPanel:
                    game_.setPanelPos({-1.f, -1.f});
                    game_.setPanelCollapsed(false);
                    saveNow();
                    return 0;
                case kMenuExit: requestExit(); return 0;
                default: break;
            }
            return 0;

        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, isButton ? IDC_HAND : IDC_ARROW));
            return TRUE;

        case WM_DISPLAYCHANGE:
            if (!isButton) layout();
            return 0;

        case WM_DPICHANGED:
            // 화면 배율이 바뀌면 다시 재서 그린다(다시 켜지 않아도 되게).
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
