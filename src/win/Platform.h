// Windows 환경 도구: 문자열 변환, 경로, 작업 영역, 전체화면 감지
#pragma once

#include <filesystem>
#include <string>

#include "WinCommon.h"

namespace dt::win {

std::wstring widen(const std::string& utf8);
std::filesystem::path exeDirectory();
std::filesystem::path saveFilePath();   // %APPDATA%\DragonTower\save.txt
RECT primaryWorkArea();                 // 주 모니터에서 작업 표시줄을 뺀 영역
float dpiScale();                       // 96 DPI 기준 배율

// 다른 앱이 주 모니터를 전체화면으로 쓰고 있는가.
// 게임 자신의 창(ignore1, ignore2)은 제외한다.
bool isOtherAppFullscreen(HWND ignore1, HWND ignore2);

}  // namespace dt::win
