// Windows·GDI+ 헤더를 올바른 순서로 포함한다.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

#include <algorithm>

// GDI+ 헤더는 min/max 매크로를 기대하므로 std 버전을 넣어 준다.
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus

#include <objidl.h>
#include <gdiplus.h>
