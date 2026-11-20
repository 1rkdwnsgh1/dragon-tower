#include "App.h"

#include <shellapi.h>

#include <chrono>
#include <cmath>
#include <ctime>
#include <string>

#include "FrameProfiler.h"
#include "Platform.h"
#include "core/Config.h"
#include "core/DragonData.h"
#include "core/EnemyData.h"
#include "core/TowerData.h"

using namespace Gdiplus;

namespace dt::win {

namespace {
constexpr UINT kTrayMsg = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr int kHotkeyId = 1;
constexpr int kHotkeyHideId = 2;  // v1.9: Ctrl+Alt+D 로 전부 숨기기
constexpr int kMenuToggleInput = 100;
constexpr int kMenuShowHide = 101;
constexpr int kMenuResetPanel = 102;
constexpr int kMenuExit = 103;
constexpr int kMenuFrameStats = 104;  // 큐 11번: 프레임 측정 표시 켜고 끄기
}  // namespace


namespace {
const wchar_t* kWindowClass = L"DragonTowerWindow";

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
    // v1.6: 탑 층 구성도 데이터에서 온다. 파일이 없으면 내장 3층을 그대로 쓴다.
    TowerScanResult tscan = loadTowerFloors(assetDir / "tower");
    if (tscan.found) game_.setTowerFloors(tscan.floors);
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
    // v1.4: 설정 버튼 창을 없앴다. 키보드는 게임이 알아서 켜고(싸우는 중이고 자동이 아닐 때),
    // 메뉴는 상태창에서 연다. 레이어드 창 하나와 매 프레임 UpdateLayeredWindow 한 번이 사라졌다.
    if (!overlay_) return false;

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
    if (tscan.found)
        game_.notify("탑 층 구성을 데이터에서 " + std::to_string(game_.towerFloorCount()) + "층 읽었습니다.",
                     0xFF8CFFA0, 5.f);
    for (const std::string& w : escan.warnings) scan.warnings.push_back(w);
    for (const std::string& w : tscan.warnings) scan.warnings.push_back(w);
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

    // 종이 하나도 없으면 게임을 할 수 없다. 이때 **반드시** 왜 그런지 알려야 한다.
    //
    // v1.5에서 내장 4종을 지운 뒤로 종은 전부 assets/dragons/ 에서 온다. 그래서 assets 폴더가
    // exe 옆에 없으면 드래곤이 0마리인 채로 켜진다. 그런데 loadDragonFolders 는 폴더가 아예
    // 없으면 경고도 없이 조용히 돌아오고(warnings 가 빈다), newSpecies 도 0이라 위의 알림이
    // 하나도 뜨지 않았다 — 화면이 그냥 비어 보여서 "드래곤이 사라졌다"로 읽힌다.
    // 실제로 exe 만 들어 있는 압축을 받아서 그렇게 됐다.
    if (speciesCount() == 0) {
        std::error_code dec;
        const bool dirMissing = !std::filesystem::is_directory(assetDir / "dragons", dec);
        if (dirMissing) {
            game_.notify("assets 폴더를 찾지 못했습니다. dragon_tower.exe 와 같은 폴더에 "
                         "assets 폴더가 있어야 합니다.", 0xFFFF6B6B, 600.f);
            game_.notify("배포본(DragonTower-windows)을 압축 푼 폴더 그대로 실행해 주세요. "
                         "exe 만 따로 옮기면 드래곤을 읽을 수 없습니다.", 0xFFFFD166, 600.f);
        } else {
            game_.notify("assets/dragons 에서 드래곤을 하나도 읽지 못했습니다. "
                         "폴더마다 dragon.txt 가 있어야 합니다.", 0xFFFF6B6B, 600.f);
        }
    }

    ShowWindow(overlay_, SW_SHOWNOACTIVATE);
    // 처음 선택 화면이면 바로 고를 수 있도록 입력을 켠다.
    setInputEnabled(game_.needsInput());
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    createTray();
    // Ctrl+Shift+D 로 키보드 조작을 켜고 끈다. 이미 쓰는 조합이면 조용히 넘어간다.
    // v1.4: 키보드 받기는 게임이 알아서 하므로, 이 단축키는 **메뉴 열고 닫기**로 쓴다.
    if (!RegisterHotKey(overlay_, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'D')) {
        game_.notify("Ctrl+Shift+D 단축키를 등록하지 못했습니다(다른 앱이 쓰는 중).", 0xFFFFD166, 6.f);
    }
    // v1.9: Ctrl+Alt+D 는 드래곤과 UI 를 통째로 숨긴다. 숨어 있어도 자동 사냥은 계속 돈다.
    // 등록에 실패해도 트레이 메뉴로 같은 일을 할 수 있으므로 알리기만 한다.
    if (!RegisterHotKey(overlay_, kHotkeyHideId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'D')) {
        game_.notify("Ctrl+Alt+D 단축키를 등록하지 못했습니다(다른 앱이 쓰는 중). 트레이 메뉴를 쓰세요.",
                     0xFFFFD166, 6.f);
    }
    refreshTrayTip();
    if (!game_.hintShown()) {
        game_.notify("창과 버튼은 마우스로 씁니다. 싸우기 시작하면 키보드를 받습니다. Ctrl+Shift+D는 메뉴, Ctrl+Alt+D는 숨기기입니다.", 0xFFAAD4FF, 12.f);
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

    game_.setDesktopBounds(RectF(0.f, 0.f, w / scale_, h / scale_));
}

void App::shutdown() {
    UnregisterHotKey(overlay_, kHotkeyId);
    UnregisterHotKey(overlay_, kHotkeyHideId);
    if (taskbar_) {
        DestroyWindow(taskbar_);
        taskbar_ = nullptr;
    }
    taskbarSurf_.destroy();
    destroyTray();
    overlaySurf_.destroy();
    renderer_.shutdown();
}

// ---------------------------------------------------------------------------
// 입력 전환
// ---------------------------------------------------------------------------

void App::setInputEnabled(bool on) {
    // v1.4: 사람이 켜고 끄는 스위치가 아니다. 게임이 키보드를 필요로 할 때만(싸우는 중이고
    // 자동이 아닐 때) 켜진다. syncKeyboard() 가 매 프레임 이 함수를 부른다.
    // 클릭 통과(WS_EX_TRANSPARENT)는 updateClickThrough 가 맡는다. 여기서 같이 건드리면
    // 커서가 UI 위에 있는데도 통과로 되돌려 놓는 일이 생긴다.
    if (inputEnabled_ == on) return;
    inputEnabled_ = on;
    cursorOnUi_ = false;  // 다음 갱신에서 다시 판단하게 한다
    LONG_PTR ex = GetWindowLongPtrW(overlay_, GWL_EXSTYLE);
    ex |= static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    if (on) ex &= ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
    else ex |= static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
    SetWindowLongPtrW(overlay_, GWL_EXSTYLE, ex);
    SetWindowPos(overlay_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
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
    // 2026-11-20: 키 5는 궁극기 전용 칸이다(장착 칸 1~4 와 따로).
    {
        const bool num = edge('5');
        const bool pad = edge(VK_NUMPAD5);
        in.ultimate = num || pad;
    }

    if (edge(VK_ESCAPE)) {
        Screen sc = game_.screen();
        if (sc == Screen::AutoWarning || sc == Screen::TowerWarning || sc == Screen::TowerSelect || sc == Screen::Codex) game_.closeModal();
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

// v1.9: 작업표시줄 꾸미기.
// 오버레이 본체는 **작업 영역**(작업표시줄을 뺀 영역)만 덮으므로, 작업표시줄을 꾸미려면
// 그 위에 겹치는 창이 따로 필요하다. 클릭은 항상 통과시키므로 시작 단추·트레이 아이콘은
// 그대로 눌린다. 다만 **그림에 가려 보이지 않을 수는 있다** — 그래서 환경 설정에서 끌 수 있고,
// 스킨에 taskbar.png 가 없으면 아무 일도 하지 않는다.
//
// 0.25초마다 불린다. 바뀐 게 없으면 맨 위로만 올리고 바로 돌아간다(그리기는 건너뛴다).
void App::updateTaskbarSkin() {
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    std::string skin = game_.uiSkin();
    UiPiece piece{};
    bool want = game_.taskbarSkin() && tray != nullptr;
    if (want) {
        piece = sprites_.ui(skin, "taskbar");
        want = piece.valid() && piece.bmp->GetWidth() > 0 && piece.bmp->GetHeight() > 0;
    }
    if (!want) {
        if (taskbar_ && taskbarDrawn_) {
            ShowWindow(taskbar_, SW_HIDE);
            taskbarDrawn_ = false;
            taskbarSkinKey_.clear();
        }
        return;
    }

    RECT tb{};
    if (!GetWindowRect(tray, &tb)) return;
    int w = tb.right - tb.left;
    int h = tb.bottom - tb.top;
    if (w <= 0 || h <= 0) return;
    // 세로로 선 작업표시줄(왼쪽·오른쪽 고정)에는 가로로 긴 띠가 맞지 않는다. 그릴 자리가 없으니 끈다.
    if (h >= w) {
        if (taskbar_ && taskbarDrawn_) {
            ShowWindow(taskbar_, SW_HIDE);
            taskbarDrawn_ = false;
            taskbarSkinKey_.clear();
        }
        return;
    }

    // 2026-11-17 (#77): 작업표시줄 **위에 덮지 않고 바로 바깥쪽**에 띠를 그린다.
    // 덮어 그리면 시작 버튼·앱 아이콘·트레이·시계가 가려진다. 작업표시줄은 다른 프로세스
    // (explorer)의 창이고 Windows 11 은 그 안이 WinUI XAML 이라, 그 밑으로 파고드는 길이 없다
    // (SetParent 는 성공해도 한 픽셀도 그려지지 않는다). 자리를 옮기는 것이 유일하게 확실한 길이다.
    // 작업표시줄이 화면 위쪽에 붙어 있으면 띠도 아래쪽으로 간다.
    HMONITOR mon = MonitorFromRect(&tb, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    bool dockedTop = GetMonitorInfoW(mon, &mi) && tb.top <= mi.rcMonitor.top;
    RECT r = dockedTop ? RECT{tb.left, tb.bottom, tb.right, tb.bottom + h}
                       : RECT{tb.left, tb.top - h, tb.right, tb.top};

    if (!taskbar_) {
        // 본체와 같은 창 클래스를 쓰되 lpParam 을 넘기지 않는다 —
        // 그래야 StaticProc 이 App 을 찾지 못하고 DefWindowProc 으로 흘려보낸다.
        DWORD ex = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
        taskbar_ = CreateWindowExW(ex, kWindowClass, L"Dragon Tower Taskbar", WS_POPUP, r.left, r.top, w, h,
                                   nullptr, nullptr, inst_, nullptr);
        if (!taskbar_) return;
    }

    bool same = taskbarDrawn_ && taskbarSkinKey_ == skin && r.left == taskbarRect_.left &&
                r.top == taskbarRect_.top && r.right == taskbarRect_.right && r.bottom == taskbarRect_.bottom;
    if (same) {
        // 드래곤 바로 아래에 둔다 — 띠가 드래곤·몬스터를 덮으면 안 된다.
        if (!hidden_) SetWindowPos(taskbar_, overlay_, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }

    if (taskbarSurf_.w != w || taskbarSurf_.h != h) {
        if (!taskbarSurf_.create(w, h)) return;
    }
    {
        Graphics g(taskbarSurf_.gbmp.get());
        g.Clear(Color(0, 0, 0, 0));
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        // 비율을 지킨다(#77). 가로를 띠 너비에 맞추고, 세로가 넘치면 **가운데를 잘라** 쓴다.
        // 예전에는 Rect(0,0,w,h) 로 늘려 그려서 가로로 길쭉하게 일그러졌다.
        float iw = static_cast<float>(piece.bmp->GetWidth());
        float ih = static_cast<float>(piece.bmp->GetHeight());
        float sx = static_cast<float>(w) / iw;          // 가로를 꽉 채우는 배율
        float drawH = ih * sx;
        if (drawH > static_cast<float>(h)) {
            float srcH = static_cast<float>(h) / sx;    // 띠에 들어갈 만큼만 원본에서 떼어 온다
            float srcY = (ih - srcH) * 0.5f;
            g.DrawImage(piece.bmp, Rect(0, 0, w, h), 0.f, srcY, iw, srcH, UnitPixel);
        } else {
            // 띠보다 납작하면 작업표시줄에 붙는 쪽으로 맞춘다(아래 고정이면 띠의 아래, 위 고정이면 위).
            int top = dockedTop ? 0 : h - static_cast<int>(drawH + 0.5f);
            g.DrawImage(piece.bmp, Rect(0, top, w, static_cast<int>(drawH + 0.5f)), 0.f, 0.f, iw, ih, UnitPixel);
        }
    }
    taskbarRect_ = r;
    taskbarSkinKey_ = skin;
    taskbarSurf_.present(taskbar_, r.left, r.top);
    SetWindowPos(taskbar_, overlay_, r.left, r.top, w, h, SWP_NOACTIVATE);
    if (!hidden_) ShowWindow(taskbar_, SW_SHOWNOACTIVATE);
    taskbarDrawn_ = true;
}

void App::updateFullscreenState() {
    // v1.9: 숨는 이유가 둘이다 — 다른 앱이 전체화면이거나, 사람이 직접 숨겼거나.
    // 둘 중 하나라도 참이면 숨는다. 전에는 전체화면 여부만 보고 hidden_ 을 덮어써서,
    // 사람이 트레이로 숨겨도 0.25초 뒤 감시가 다시 켜 버렸다.
    bool want = isOtherAppFullscreen(overlay_, nullptr) || userHidden_;
    if (want == hidden_) return;
    hidden_ = want;
    if (hidden_) {
        // 숨김은 일시정지가 아니다. 자동 사냥은 계속 진행된다.
        ShowWindow(overlay_, SW_HIDE);
        if (taskbar_) ShowWindow(taskbar_, SW_HIDE);
    } else {
        ShowWindow(overlay_, SW_SHOWNOACTIVATE);
        SetWindowPos(overlay_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (taskbar_ && taskbarDrawn_) ShowWindow(taskbar_, SW_SHOWNOACTIVATE);
    }
}

// v1.9: Ctrl+Alt+D 와 트레이 메뉴가 함께 쓴다. 숨겨도 게임은 계속 돈다.
void App::setUserHidden(bool on) {
    if (userHidden_ == on) return;
    userHidden_ = on;
    updateFullscreenState();   // 숨김 여부를 한 곳에서만 정한다
    refreshTrayTip();
    if (!on) game_.notify("다시 보입니다. Ctrl+Alt+D 로 숨길 수 있습니다.", 0xFFAAD4FF, 4.f);
}

void App::tick(float dt) {
    time_ += dt;
    fullscreenTimer_ += dt;
    if (fullscreenTimer_ >= 0.25f) {
        fullscreenTimer_ = 0.f;
        updateFullscreenState();
        updateTaskbarSkin();  // v1.9: 작업표시줄이 옮겨지거나 드래곤을 바꾸면 다시 그린다
    }
    topmostTimer_ += dt;
    if (topmostTimer_ >= 2.f && !hidden_) {
        topmostTimer_ = 0.f;
        SetWindowPos(overlay_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }

    // 끌기가 어떤 이유로든 끊겼으면 잡고 있는 상태도 푼다(드래곤이 굳어 버리지 않게).
    if (drag_ != Drag::Dragon && game_.dragonHeld()) game_.setDragonHeld(false);

    PlayerInput in = pollKeys();
    game_.update(dt, in, hidden_, inputEnabled_);
    if (game_.consumeSaveRequest()) saveNow();

    pumpAlerts();

    // 클릭 통과 범위는 매 프레임 갱신한다(요소가 움직이므로).
    syncKeyboard();
    updateClickThrough();

    if (hidden_) return;

    // v0.6에서는 조용할 때 항상 프레임을 줄였는데, 입력을 끄면 드래곤 움직임이 끊겨 보였다.
    // v0.8부터는 절전 설정을 켠 경우에만 줄이고, 드래곤이 걷거나 나는 동안에는 줄이지 않는다.
    // 큐 11번: 측정 중에는 프레임을 줄이지 않는다. 띄워 놓고 재는 동안 프레임이 드문드문
    // 들어오면 평균·fps 를 읽을 수 없다. (측정을 끄면 예전과 똑같이 동작한다.)
    if (!game_.powerSave() || frameProfiler().enabled()) {
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
        // 큐 11번: 프레임 측정. 꺼져 있으면 아래 호출들은 분기 하나로 끝난다.
        FrameProfiler& prof = frameProfiler();
        prof.beginFrame();
        Graphics g(overlaySurf_.gbmp.get());
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.Clear(Color(0, 0, 0, 0));
        g.SetCompositingMode(CompositingModeSourceOver);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        // v1.4: 고품질 바이큐빅은 스프라이트·UI 조각을 매 프레임 다시 샘플링하면서 비싸다.
        // 노트북에서 프레임이 떨어지던 가장 큰 원인이라 양선형으로 낮췄다. 눈으로는 거의 같다.
        g.SetInterpolationMode(InterpolationModeBilinear);
        g.ScaleTransform(scale_, scale_);
        prof.mark(FrameStage::Clear);
        RenderContext ctx;
        ctx.inputEnabled = inputEnabled_;
        ctx.time = time_;
        ctx.mouseX = mouseX_;
        ctx.mouseY = mouseY_;
        ctx.scale = scale_;
        renderer_.drawOverlay(g, game_, sprites_, ctx, buttons_, &regions_);
        g.Flush(FlushIntentionSync);
        overlaySurf_.present(overlay_, work_.left, work_.top);
        prof.mark(FrameStage::Present);  // Flush + UpdateLayeredWindow
        prof.endFrame();
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
        case UiAction::OpenTower: game_.openTowerSelection(); break;
        case UiAction::UseUltimate: game_.useUltimate(); break;
        case UiAction::ConfirmTowerFloor: game_.openTowerWarning(); break;
        case UiAction::SelectTowerFloor: game_.selectTowerFloor(b.arg); break;
        case UiAction::DragTowerScroll: break;
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
        case UiAction::CloseMenu: game_.setMenuOpen(false); break;
        case UiAction::SetMotion: game_.setForcedMotion(static_cast<AnimState>(b.arg)); break;
        case UiAction::ClearMotion:
            game_.clearForcedMotion();
            game_.closeMotionMenu();
            break;
        case UiAction::SetMenuTab: game_.setMenuTab(b.arg); break;
        case UiAction::DragMenuSize:   // 끌기는 WM_LBUTTONDOWN 에서 시작한다
        case UiAction::DragCharSlider: break;
        case UiAction::TogglePowerSave: game_.setPowerSave(!game_.powerSave()); break;
        case UiAction::ToggleTaskbarSkin:
            game_.setTaskbarSkin(!game_.taskbarSkin());
            updateTaskbarSkin();  // 켜고 끈 것이 바로 보이게 한다
            break;
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

// v1.4: 게임 상태에 맞춰 키보드 받기를 켜고 끈다.
// 켜질 때만 창을 활성화하므로, 자동 사냥이나 그냥 구경할 때는 포커스를 가져가지 않는다.
void App::syncKeyboard() {
    bool want = game_.wantsKeyboard() && !hidden_;
    if (want == inputEnabled_) return;
    setInputEnabled(want);
    if (want) {
        SetForegroundWindow(overlay_);
        SetFocus(overlay_);
    }
}

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
    tip += inputEnabled_ ? L"키보드 받는 중" : L"대기";
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
                game_.menuOpen() ? L"메뉴 닫기 (Ctrl+Shift+D)" : L"메뉴 열기 (Ctrl+Shift+D)");
    AppendMenuW(m, MF_STRING, kMenuShowHide, userHidden_ ? L"드래곤 보이기 (Ctrl+Alt+D)" : L"드래곤 숨기기 (Ctrl+Alt+D)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kMenuResetPanel, L"상태창 위치 되돌리기");
    // 큐 11번: 프레임 측정. 기본 꺼짐이고 켜 둔 동안만 왼쪽 위에 표가 뜬다.
    AppendMenuW(m, MF_STRING, kMenuFrameStats,
                frameProfiler().enabled() ? L"프레임 측정 끄기" : L"프레임 측정 켜기 (디버그)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kMenuExit, L"종료");
    POINT p{};
    GetCursorPos(&p);
    SetForegroundWindow(overlay_);  // 메뉴가 바로 닫히지 않게 한다
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, overlay_, nullptr);
    PostMessageW(overlay_, WM_NULL, 0, 0);
    DestroyMenu(m);
}

// v1.9: 창을 끌 때 화면 밖으로 나가지 않게 왼쪽 위 좌표를 자른다.
// 그리는 쪽에서만 잘라 두면(v1.8 까지) 저장되는 좌표는 화면 밖 값이라, 마우스를 가장자리 너머로
// 끌었다가 되돌릴 때 창이 손에서 떨어져 튀어 보였다. 저장되는 값 자체를 잘라야 한다.
Vec2 App::clampToDesktop(Vec2 p, const Gdiplus::RectF& drawn) const {
    const RectF& d = game_.desktopBounds();
    float w = drawn.Width > 1.f ? drawn.Width : 1.f;
    float h = drawn.Height > 1.f ? drawn.Height : 1.f;
    p.x = std::clamp(p.x, d.x, std::max(d.x, d.x + d.w - w));
    p.y = std::clamp(p.y, d.y, std::max(d.y, d.y + d.h - h));
    return p;
}

void App::dragTowerScroll(float y) {
    const float fraction = std::clamp((y - dragDY_) / std::max(1.f, dragDX_), 0.f, 1.f);
    const int range = std::max(0, game_.towerFloorCount() - Game::visibleTowerFloors);
    game_.scrollTowerFloors(1 + static_cast<int>(std::round((1.f - fraction) * range)));
}

bool App::startDrag(float lx, float ly) {
    if (game_.screen() == Screen::TowerSelect) {
        for (const auto& b : buttons_) {
            if (b.action != UiAction::DragTowerScroll || !b.rect.Contains(PointF(lx, ly))) continue;
            const float thumb = static_cast<float>(b.arg) / 100.f;
            const int range = std::max(1, game_.towerFloorCount() - Game::visibleTowerFloors);
            drag_ = Drag::TowerScroll;
            dragDX_ = b.rect.Height - thumb;
            const float top = b.rect.Y + dragDX_ * (range - (game_.firstVisibleTowerFloor() - 1)) / range;
            const float grab = ly >= top && ly <= top + thumb ? ly - top : thumb * 0.5f;
            dragDY_ = b.rect.Y + grab;
            dragTowerScroll(ly);
            return true;
        }
        return false;
    }
    for (const UiButton& b : buttons_) {
        if (!b.enabled || !b.rect.Contains(Gdiplus::PointF(lx, ly))) continue;
        if (b.action == UiAction::DragMenuSize) {
            drag_ = Drag::MenuSize;
            dragDX_ = lx;                      // 시작한 가로 위치
            dragStartScale_ = game_.menuScale();
            return true;
        }
        if (b.action == UiAction::DragCharSlider) {
            // arg 에 슬라이더 왼쪽 끝(정수 픽셀)을, rect 너비에 길이를 담아 보낸다
            drag_ = Drag::CharSlider;
            dragDX_ = b.rect.X;
            dragDY_ = b.rect.Width;
            game_.setCharScale(cfg::kCharScaleMin + (lx - b.rect.X) / std::max(1.f, b.rect.Width) *
                                                        (cfg::kCharScaleMax - cfg::kCharScaleMin));
            return true;
        }
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
    // 탐색기가 다시 시작하면 트레이 아이콘이 사라진다. 다시 넣어 준다.
    if (taskbarCreatedMsg_ != 0 && msg == taskbarCreatedMsg_) {
        trayAdded_ = false;
        createTray();
        refreshTrayTip();
        return 0;
    }
    switch (msg) {
        case WM_MOUSEACTIVATE:
            // 키보드를 받는 중(싸우는 중이고 자동이 아님)일 때만 창을 활성화한다.
            // 마우스만 쓸 때 활성화하면 쓰던 프로그램의 포커스를 뺏는다.
            if (!inputEnabled_) return MA_NOACTIVATE;
            return MA_ACTIVATE;

        case WM_MOUSEWHEEL: {
            if (game_.screen() != Screen::TowerSelect) break;
            const float x = (static_cast<short>(LOWORD(lp)) - work_.left) / scale_;
            const float y = (static_cast<short>(HIWORD(lp)) - work_.top) / scale_;
            if (renderer_.towerListRect().Contains(PointF(x, y))) {
                const int delta = GET_WHEEL_DELTA_WPARAM(wp);
                const int steps = std::max(1, std::abs(delta) / WHEEL_DELTA);
                if (delta) game_.scrollTowerFloors(game_.firstVisibleTowerFloor() + (delta > 0 ? steps : -steps));
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            float x = static_cast<float>(static_cast<short>(LOWORD(lp)));
            float y = static_cast<float>(static_cast<short>(HIWORD(lp)));
            mouseX_ = x / scale_;
            mouseY_ = y / scale_;
            if (drag_ == Drag::TowerScroll) {
                dragTowerScroll(mouseY_);
            } else if (drag_ == Drag::Panel) {
                game_.setPanelPos(clampToDesktop({mouseX_ - dragDX_, mouseY_ - dragDY_},
                                                 renderer_.lastCardRect()));
            } else if (drag_ == Drag::Menu) {
                game_.setMenuPos(clampToDesktop({mouseX_ - dragDX_, mouseY_ - dragDY_},
                                                renderer_.lastMenuRect()));
            } else if (drag_ == Drag::MenuSize) {
                // v1.4: 오른쪽 아래 모서리를 끌어 메뉴 크기를 바꾼다
                game_.setMenuScale(dragStartScale_ + (mouseX_ - dragDX_) / cfg::kMenuScaleDragPx);
            } else if (drag_ == Drag::Dragon) {
                game_.placeDragon({mouseX_ - dragDX_, mouseY_ - dragDY_});
            } else if (drag_ == Drag::CharSlider) {
                game_.setCharScale(cfg::kCharScaleMin +
                                   (mouseX_ - dragDX_) / std::max(1.f, dragDY_) *
                                       (cfg::kCharScaleMax - cfg::kCharScaleMin));
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            // v1.0: 우리가 그린 것 위에서는 언제나 받는다.
            float x = static_cast<float>(static_cast<short>(LOWORD(lp)));
            float y = static_cast<float>(static_cast<short>(HIWORD(lp)));
            float lx = x / scale_, ly = y / scale_;
            SetCapture(hwnd);
            // 모션 목록 밖을 누르면 목록만 닫는다.
            bool onMotionMenu = false;
            for (const UiButton& b : buttons_) {
                if ((b.action == UiAction::SetMotion || b.action == UiAction::ClearMotion) &&
                    b.rect.Contains(Gdiplus::PointF(lx, ly))) {
                    onMotionMenu = true;
                    break;
                }
            }
            if (!onMotionMenu && game_.motionMenuOpen()) game_.closeMotionMenu();
            bool dragging = startDrag(lx, ly);
            if (!dragging) {
                onClick(lx, ly);
            }
            return 0;
        }

        case WM_LBUTTONUP:
            if (drag_ != Drag::None) {
                if (drag_ == Drag::Dragon) game_.setDragonHeld(false);
                drag_ = Drag::None;
                saveNow();  // 옮긴 위치·크기를 바로 남긴다
            }
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;

        case WM_RBUTTONDOWN: {
            // v1.5: 드래곤 위에서 오른쪽 클릭하면 모션 목록을 연다. 그 밖은 트레이와 같은 메뉴.
            float lx = static_cast<float>(static_cast<short>(LOWORD(lp))) / scale_;
            float ly = static_cast<float>(static_cast<short>(HIWORD(lp))) / scale_;
            if (game_.motionMenuOpen()) {
                game_.closeMotionMenu();
                return 0;
            }
            if (game_.hasDragon() && game_.screen() == Screen::Desktop) {
                const DragonActor& d = game_.world().dragon();
                float reach = cfg::kDragonRadius * 1.6f * game_.charScale();
                if (std::fabs(lx - d.pos.x) <= reach && std::fabs(ly - d.pos.y) <= reach) {
                    game_.openMotionMenu({lx + 12.f, ly - 20.f});
                    return 0;
                }
            }
            showTrayMenu();
            return 0;
        }

        case WM_HOTKEY:
            // v1.4: 키보드 받기는 게임이 알아서 하므로, 단축키는 메뉴를 열고 닫는다.
            if (wp == kHotkeyId) game_.setMenuOpen(!game_.menuOpen());
            else if (wp == kHotkeyHideId) setUserHidden(!userHidden_);
            return 0;

        case kTrayMsg:
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) showTrayMenu();
            else if (LOWORD(lp) == WM_LBUTTONDBLCLK) { setInputEnabled(!inputEnabled_); refreshTrayTip(); }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case kMenuToggleInput: game_.setMenuOpen(!game_.menuOpen()); refreshTrayTip(); return 0;
                case kMenuShowHide:
                    // v1.9: 숨김 여부는 setUserHidden 한 곳에서만 정한다.
                    // 전에는 여기서 hidden_ 을 직접 뒤집어, 전체화면 감시가 0.25초 뒤 되돌려 버렸다.
                    setUserHidden(!userHidden_);
                    refreshTrayTip();
                    return 0;
                case kMenuResetPanel:
                    game_.setPanelPos({-1.f, -1.f});
                    game_.setPanelCollapsed(false);
                    saveNow();
                    return 0;
                case kMenuFrameStats:
                    // 저장하지 않는다 — 디버그 표시이므로 다시 켜면 꺼진 상태로 시작한다.
                    frameProfiler().setEnabled(!frameProfiler().enabled());
                    return 0;
                case kMenuExit: requestExit(); return 0;
                default: break;
            }
            return 0;

        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;

        case WM_DISPLAYCHANGE:
            layout();
            return 0;

        case WM_DPICHANGED:
            // 화면 배율이 바뀌면 다시 재서 그린다(다시 켜지 않아도 되게).
            layout();
            return 0;

        case WM_SETTINGCHANGE:
            if (wp == SPI_SETWORKAREA) layout();
            return 0;

        case WM_QUERYENDSESSION:
            saveNow();
            return TRUE;

        case WM_ENDSESSION:
            if (wp) saveNow();
            return 0;

        case WM_CLOSE:
            requestExit();
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace dt::win
