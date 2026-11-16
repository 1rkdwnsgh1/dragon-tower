// FrameProfiler 의 시계 부분. QueryPerformanceCounter 로 잰다.
#include "FrameProfiler.h"

#include "WinCommon.h"

namespace dt::win {
namespace {

double qpcFrequency() {
    LARGE_INTEGER f{};
    if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0) return 1.0;
    return static_cast<double>(f.QuadPart);
}

}  // namespace

double FrameProfiler::nowMs() {
    static const double freq = qpcFrequency();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) * 1000.0 / freq;
}

void FrameProfiler::setEnabled(bool on) {
    if (enabled_ == on) return;
    enabled_ = on;
    inFrame_ = false;
    stats_.reset();  // 켤 때마다 깨끗한 숫자부터 모은다
}

void FrameProfiler::beginFrame() {
    if (!enabled_) return;
    inFrame_ = true;
    markMs_ = nowMs();
}

void FrameProfiler::mark(FrameStage s) {
    if (!enabled_ || !inFrame_) return;
    double t = nowMs();
    stats_.addStage(s, t - markMs_);
    markMs_ = t;
}

void FrameProfiler::endFrame() {
    if (!enabled_ || !inFrame_) return;
    inFrame_ = false;
    stats_.endFrame();
}

FrameProfiler& frameProfiler() {
    static FrameProfiler p;
    return p;
}

}  // namespace dt::win
