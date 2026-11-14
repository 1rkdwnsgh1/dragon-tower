// 프레임 시간을 단계별로 재는 **디버그 도구**. 기본 꺼짐 — 트레이 메뉴에서 켠다.
// 큐 11번. 고치는 것이 아니라 **재는 것**이다. 끄면 mark() 가 분기 하나로 끝나고
// 아무것도 그리지 않는다 — 기본 상태에서 달라지는 것이 없어야 한다.
// 순수 계산은 include/core/FrameStats.h 에 있다(그쪽은 Linux 에서 테스트된다).
#pragma once

#include "core/FrameStats.h"

namespace dt::win {

// 평균을 낼 창 길이(프레임). **임시값** — 60fps 에서 0.5초마다 숫자가 갱신된다.
constexpr int kFrameStatsWindow = 30;

class FrameProfiler {
public:
    bool enabled() const { return enabled_; }
    void setEnabled(bool on);

    void beginFrame();        // 프레임 시작. 시계를 여기서 맞춘다
    void mark(FrameStage s);  // 지난 mark 이후 걸린 시간을 s 단계에 넣는다
    void endFrame();          // 프레임을 닫는다(평균에 들어간다)

    const FrameStats& stats() const { return stats_; }

private:
    static double nowMs();

    bool enabled_ = false;
    bool inFrame_ = false;
    double markMs_ = 0.0;
    FrameStats stats_{kFrameStatsWindow};
};

// 전역 하나. 트레이 메뉴(App.cpp)와 그리기(Renderer*.cpp)가 같은 것을 본다.
FrameProfiler& frameProfiler();

}  // namespace dt::win
