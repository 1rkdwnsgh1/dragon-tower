// Dragon Tower 진입점
#include "App.h"
#include "WinCommon.h"

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    // 같은 게임이 두 번 실행되지 않게 한다.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"DragonTower.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Dragon Tower가 이미 실행 중입니다.", L"Dragon Tower", MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 0;
    }

    SetProcessDPIAware();

    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"GDI+를 시작할 수 없습니다.", L"Dragon Tower", MB_OK | MB_ICONERROR);
        return 1;
    }

    int code = 1;
    {
        dt::win::App app;
        if (app.init(inst)) {
            code = app.run();
        } else {
            MessageBoxW(nullptr, L"게임 창을 만들 수 없습니다.", L"Dragon Tower", MB_OK | MB_ICONERROR);
        }
        app.shutdown();
    }

    Gdiplus::GdiplusShutdown(gdiToken);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return code;
}
