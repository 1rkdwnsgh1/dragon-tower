// 그림 한 칸을 화면(장치 픽셀) 크기로 미리 키워 두는 캐시. 스프라이트·효과·장판·투사체·숨결이 함께 쓴다.
//
// 왜 필요한가(2026-11-19 측정, docs/implementation-notes.md "그림 캐시"):
// GDI+ 는 확대·투명도(ColorMatrix)가 걸린 DrawImage 가 픽셀마다 비싸다. 저주의 안개 한 칸을
// 크기 4.0(1120px)으로 그리면 119ms 였고, 같은 그림을 미리 키워 두고 정수 좌표에 찍으면 1.5ms 다.
// 게다가 가로로 긴 시트(안개 12800px)는 한 칸만 그려도 시트 전체 크기만큼 비용을 낸다(약 17ms).
//
// 하는 일
// 1. 시트에서 칸을 잘라 따로 둔다(cell). 1:1 메모리 복사라 빠르고, 위의 "긴 시트" 비용이 사라진다.
// 2. 칸을 장치 픽셀 크기로 키운 그림(frame)을 **따로 도는 스레드**가 만든다. 큰 그림은 한 장에
//    100ms 가까이 걸려서, 그리는 스레드에서 만들면 그 프레임이 끊긴다.
// 3. 다 만들 때까지 그리는 쪽은 잘라 둔 칸을 그린다(작으면 양선형, 크면 최근접 — 둘 다 빠르다).
//
// 키에 투명도는 넣지 않는다. 효과는 나타나고 사라지며 투명도가 매 프레임 바뀌어 키가 재사용되지
// 않기 때문이다. 투명도는 붙일 때 곱한다(그래도 1120px 에서 8.7ms 로 예전보다 열 배 넘게 빠르다).
//
// 스레드: GDI+ 개체는 두 스레드가 같이 쓰면 안 된다. 뒤 스레드에는 잘라 둔 칸의 **복사본**만 넘기고,
// 만든 그림은 beginFrame 에서 그리는 스레드가 가져간다. 원본 시트는 그리는 스레드만 만진다.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

#include "WinCommon.h"

namespace dt::win {

class FrameCache {
public:
    // 시트 안의 한 칸(또는 그림 전체).
    struct Source {
        Gdiplus::Bitmap* bmp = nullptr;
        int x = 0, y = 0, w = 0, h = 0;
    };

    FrameCache() = default;
    FrameCache(const FrameCache&) = delete;
    FrameCache& operator=(const FrameCache&) = delete;
    ~FrameCache();

    // 매 프레임 그리기 전에 부른다. 뒤에서 다 만든 그림을 가져오고, 예산을 넘으면 오래 안 쓴 것부터 버린다.
    // generation 이 바뀌면(그림을 다시 읽었다) 통째로 버린다 — 같은 주소에 다른 그림이 올 수 있다.
    void beginFrame(unsigned generation);

    // 장치 픽셀 devW x devH 로 키운 그림. 없으면 nullptr — 두 프레임 연속으로 같은 것을 찾으면
    // 그때 뒤 스레드에 만들기를 맡긴다(크기가 매 프레임 바뀌는 효과에 헛일을 시키지 않으려고).
    // interp 는 그리는 쪽의 보간 방식 — 같은 방식으로 키워야 예전과 같은 그림이 된다
    // (게임 화면은 Bilinear, 미리보기 캡처는 HighQualityBilinear).
    Gdiplus::Bitmap* find(const Source& src, int devW, int devH, bool flip, float flash,
                          Gdiplus::InterpolationMode interp);
    // 곧 쓸 그림을 미리 맡긴다(애니메이션의 다음 칸, 장착한 숨결).
    void prefetch(const Source& src, int devW, int devH, bool flip, float flash, Gdiplus::InterpolationMode interp);
    // 잘라 둔 칸(키운 그림이 아직 없을 때 대신 그린다). 자를 수 없으면 nullptr.
    Gdiplus::Bitmap* cell(const Source& src);

    // 뒤 스레드를 멈추고 전부 버린다. GDI+ 를 끄기 전에 부른다.
    void shutdown();

    size_t count() const { return frames_.size() + cells_.size(); }
    size_t bytes() const { return bytes_; }
    size_t pending() const { return pending_.size(); }

private:
    struct CellKey {
        const Gdiplus::Bitmap* bmp = nullptr;
        int x = 0, y = 0, w = 0, h = 0;
        bool operator<(const CellKey& o) const {
            return std::tie(bmp, x, y, w, h) < std::tie(o.bmp, o.x, o.y, o.w, o.h);
        }
    };
    struct FrameKey {
        CellKey cell;
        int devW = 0, devH = 0;
        bool flip = false;
        int flashQ = 0;  // 번쩍임을 1/32 단위로
        int interp = 0;  // Gdiplus::InterpolationMode
        bool operator<(const FrameKey& o) const {
            return std::tie(cell, devW, devH, flip, flashQ, interp) <
                   std::tie(o.cell, o.devW, o.devH, o.flip, o.flashQ, o.interp);
        }
    };
    struct Entry {
        std::unique_ptr<Gdiplus::Bitmap> bmp;
        uint64_t lastUsed = 0;
        size_t bytes = 0;
    };
    struct Job {
        FrameKey key;
        std::unique_ptr<Gdiplus::Bitmap> cell;  // 뒤 스레드 전용 복사본
        uint64_t epoch = 0;
    };
    struct Done {
        FrameKey key;
        std::unique_ptr<Gdiplus::Bitmap> bmp;
        uint64_t epoch = 0;
    };

    static CellKey cellKey(const Source& src);
    static FrameKey frameKey(const Source& src, int devW, int devH, bool flip, float flash,
                             Gdiplus::InterpolationMode interp);
    void submit(const FrameKey& key, const Source& src);
    void startWorkers();
    void workerLoop();
    void clearAll();
    void evict();

    // 그리는 스레드 전용
    std::map<CellKey, Entry> cells_;
    std::map<FrameKey, Entry> frames_;
    std::map<FrameKey, uint64_t> seen_;  // 아직 없는 그림을 마지막으로 찾은 프레임
    std::map<FrameKey, bool> pending_;   // 뒤 스레드에 맡겨 둔 것
    size_t bytes_ = 0;
    uint64_t frame_ = 0;
    unsigned generation_ = 0;
    bool hasGeneration_ = false;

    // 그리는 스레드와 뒤 스레드가 같이 쓴다(m_ 으로 지킨다)
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    std::vector<Done> done_;
    uint64_t epoch_ = 0;  // clearAll 마다 올린다 — 그 전에 맡긴 일의 결과는 버린다
    bool stop_ = false;
    std::vector<std::thread> workers_;
};

}  // namespace dt::win
