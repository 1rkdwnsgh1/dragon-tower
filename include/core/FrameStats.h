// 프레임 한 장이 단계별로 얼마나 걸렸는지 모으는 **순수 계산**.
// 플랫폼에 기대지 않으므로 Linux 에서 테스트한다 — 시계를 읽는 쪽은 src/win/FrameProfiler.*.
// 큐 11번(그린 부분만 갱신 1단계: 측정). **재기만 한다. 그리는 방식을 바꾸지 않는다.**
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace dt {

// 한 프레임을 이 순서로 나눠 본다.
//   Clear   — 면 지우기와 그리기 상태 설정
//   World   — 드래곤·적·효과(스프라이트)
//   Ui      — 상태창·알림·메뉴·모달
//   Debug   — 측정 표시 자체. 켜지 않으면 늘 0 이다(표를 정직하게 두려고 따로 뺐다)
//   Present — UpdateLayeredWindow
enum class FrameStage { Clear = 0, World, Ui, Debug, Present };
constexpr std::size_t kFrameStageCount = 5;
constexpr std::size_t frameStageIndex(FrameStage s) { return static_cast<std::size_t>(s); }

// 창(window) 단위로 평균을 낸다. 창이 찰 때마다 보이는 값이 한 번씩 갱신되므로
// 숫자가 매 프레임 흔들려서 읽을 수 없게 되는 일이 없다.
class FrameStats {
public:
    struct Row {
        double lastMs = 0.0;  // 가장 최근에 닫힌 프레임
        double avgMs = 0.0;   // 지난 창의 평균
        double maxMs = 0.0;   // 지난 창의 최댓값
    };

    explicit FrameStats(int window = 30) { setWindow(window); }

    void setWindow(int window) { window_ = window < 1 ? 1 : window; }
    int window() const { return window_; }

    // 한 단계가 걸린 시간을 더한다. 한 프레임에 여러 번 불러도 더해진다.
    // 음수·NaN 은 0 으로 본다 — 시계가 튀어도 표가 망가지지 않게.
    void addStage(FrameStage s, double ms) {
        if (!(ms > 0.0)) ms = 0.0;
        frame_[frameStageIndex(s)] += ms;
    }

    void endFrame() {
        for (std::size_t i = 0; i < kFrameStageCount; ++i) {
            last_[i] = frame_[i];
            sum_[i] += frame_[i];
            if (frame_[i] > max_[i]) max_[i] = frame_[i];
            frame_[i] = 0.0;
        }
        ++frames_;
        ++totalFrames_;
        if (frames_ >= window_) publish();
    }

    Row row(FrameStage s) const {
        std::size_t i = frameStageIndex(s);
        Row r;
        r.lastMs = last_[i];
        r.avgMs = shownAvg_[i];
        r.maxMs = shownMax_[i];
        return r;
    }

    // 보이는 평균의 합. 아직 창이 한 번도 차지 않았으면 0 이다.
    double avgTotalMs() const {
        double t = 0.0;
        for (double v : shownAvg_) t += v;
        return t;
    }
    double fps() const {
        double t = avgTotalMs();
        return t > 0.0 ? 1000.0 / t : 0.0;
    }
    long long totalFrames() const { return totalFrames_; }
    bool hasAverage() const { return published_; }

    void reset() {
        int w = window_;
        *this = FrameStats(w);
    }

private:
    void publish() {
        double n = static_cast<double>(frames_);
        for (std::size_t i = 0; i < kFrameStageCount; ++i) {
            shownAvg_[i] = sum_[i] / n;
            shownMax_[i] = max_[i];
            sum_[i] = 0.0;
            max_[i] = 0.0;
        }
        frames_ = 0;
        published_ = true;
    }

    int window_ = 30;
    int frames_ = 0;
    long long totalFrames_ = 0;
    bool published_ = false;
    std::array<double, kFrameStageCount> frame_{};
    std::array<double, kFrameStageCount> last_{};
    std::array<double, kFrameStageCount> sum_{};
    std::array<double, kFrameStageCount> max_{};
    std::array<double, kFrameStageCount> shownAvg_{};
    std::array<double, kFrameStageCount> shownMax_{};
};

}  // namespace dt
