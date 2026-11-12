#include "Platform.h"

#include <shellapi.h>
#include <shlobj.h>

namespace dt::win {

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::filesystem::path exeDirectory() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    std::filesystem::path p(std::wstring(buf, n));
    return p.parent_path();
}

std::filesystem::path saveFilePath() {
    PWSTR appData = nullptr;
    std::filesystem::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) && appData) {
        base = std::filesystem::path(appData);
    } else {
        base = exeDirectory();
    }
    if (appData) CoTaskMemFree(appData);
    return base / L"DragonTower" / L"save.txt";
}

RECT primaryWorkArea() {
    RECT r{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0)) {
        r.left = 0;
        r.top = 0;
        r.right = GetSystemMetrics(SM_CXSCREEN);
        r.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    return r;
}

float dpiScale() {
    HDC dc = GetDC(nullptr);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    float s = static_cast<float>(dpi) / 96.f;
    return std::clamp(s, 1.f, 3.f);
}

bool isOtherAppFullscreen(HWND ignore1, HWND ignore2) {
    // 1) 셸이 알려 주는 상태(전용 전체화면 게임, 프레젠테이션 모드)
    QUERY_USER_NOTIFICATION_STATE state;
    if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
        if (state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE) return true;
    }

    // 2) 전면 창이 주 모니터 전체를 덮는가(창 모드 전체화면, 브라우저 F11 등)
    HWND fg = GetForegroundWindow();
    if (!fg || fg == ignore1 || fg == ignore2) return false;
    if (fg == GetDesktopWindow() || fg == GetShellWindow()) return false;
    if (!IsWindowVisible(fg) || IsIconic(fg)) return false;

    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 64);
    if (lstrcmpW(cls, L"WorkerW") == 0 || lstrcmpW(cls, L"Progman") == 0 || lstrcmpW(cls, L"Shell_TrayWnd") == 0)
        return false;

    HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return false;
    // 게임은 주 모니터에만 표시하므로 주 모니터의 전체화면만 본다.
    if (!(mi.dwFlags & MONITORINFOF_PRIMARY)) return false;

    RECT wr{};
    if (!GetWindowRect(fg, &wr)) return false;
    const RECT& m = mi.rcMonitor;
    bool covers = wr.left <= m.left && wr.top <= m.top && wr.right >= m.right && wr.bottom >= m.bottom;
    if (!covers) return false;

    // 작업 표시줄 자동 숨김에서 최대화된 일반 창은 제외한다(제목 표시줄이 있고 화면보다 크게 잡힘).
    LONG style = GetWindowLongW(fg, GWL_STYLE);
    bool exact = wr.left == m.left && wr.top == m.top && wr.right == m.right && wr.bottom == m.bottom;
    bool hasCaption = (style & WS_CAPTION) == WS_CAPTION;
    return exact || !hasCaption;
}

}  // namespace dt::win
