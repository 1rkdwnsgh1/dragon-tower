#include "FrameCache.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Config.h"

namespace dt::win {

using namespace Gdiplus;

namespace {

// 비트맵의 한 사각형을 PARGB 로 그대로 복사한다. 다시 샘플링하지 않으므로 빠르고,
// 긴 시트에서 한 칸만 그릴 때 GDI+ 가 시트 전체 비용을 내던 문제가 여기서 끝난다.
std::unique_ptr<Bitmap> copyRect(Bitmap& src, const Rect& r) {
    auto out = std::make_unique<Bitmap>(r.Width, r.Height, PixelFormat32bppPARGB);
    if (out->GetLastStatus() != Ok) return nullptr;
    Rect area = r;
    BitmapData from{};
    if (src.LockBits(&area, ImageLockModeRead, PixelFormat32bppPARGB, &from) != Ok) return nullptr;
    Rect all(0, 0, r.Width, r.Height);
    BitmapData to{};
    if (out->LockBits(&all, ImageLockModeWrite, PixelFormat32bppPARGB, &to) != Ok) {
        src.UnlockBits(&from);
        return nullptr;
    }
    const size_t rowBytes = static_cast<size_t>(r.Width) * 4;
    for (int y = 0; y < r.Height; ++y) {
        std::memcpy(static_cast<BYTE*>(to.Scan0) + static_cast<ptrdiff_t>(y) * to.Stride,
                    static_cast<const BYTE*>(from.Scan0) + static_cast<ptrdiff_t>(y) * from.Stride, rowBytes);
    }
    out->UnlockBits(&to);
    src.UnlockBits(&from);
    return out;
}

// 칸을 장치 픽셀 크기로 키운다. 그리는 쪽과 같은 보간 방식으로, 예전 drawSheetFrame 과 같은
// 감싸기(칸 경계에서 옆 칸이 번지지 않게)와 번쩍임을 넣는다. 투명도는 넣지 않는다(붙일 때 곱한다).
std::unique_ptr<Bitmap> scaleCell(Bitmap& cell, int devW, int devH, bool flip, float flash, InterpolationMode interp) {
    auto out = std::make_unique<Bitmap>(devW, devH, PixelFormat32bppPARGB);
    if (out->GetLastStatus() != Ok) return nullptr;
    Graphics g(out.get());
    g.SetCompositingMode(CompositingModeSourceCopy);
    g.Clear(Color(0, 0, 0, 0));
    g.SetCompositingMode(CompositingModeSourceOver);
    g.SetInterpolationMode(interp);
    if (flip) {
        g.TranslateTransform(static_cast<REAL>(devW), 0.f);
        g.ScaleTransform(-1.f, 1.f);
    }
    ImageAttributes ia;
    ia.SetWrapMode(WrapModeTileFlipXY);
    if (flash > 0.f) {
        ColorMatrix cm = {{{1.f, 0.f, 0.f, 0.f, 0.f},
                           {0.f, 1.f, 0.f, 0.f, 0.f},
                           {0.f, 0.f, 1.f, 0.f, 0.f},
                           {0.f, 0.f, 0.f, 1.f, 0.f},
                           {flash, flash, flash, 0.f, 1.f}}};
        ia.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    }
    const REAL cw = static_cast<REAL>(cell.GetWidth()), ch = static_cast<REAL>(cell.GetHeight());
    if (g.DrawImage(&cell, RectF(0.f, 0.f, static_cast<REAL>(devW), static_cast<REAL>(devH)), 0.f, 0.f, cw, ch,
                    UnitPixel, &ia) != Ok)
        return nullptr;
    return out;
}

}  // namespace

FrameCache::~FrameCache() { shutdown(); }

FrameCache::CellKey FrameCache::cellKey(const Source& src) {
    return CellKey{src.bmp, src.x, src.y, src.w, src.h};
}

FrameCache::FrameKey FrameCache::frameKey(const Source& src, int devW, int devH, bool flip, float flash,
                                          InterpolationMode interp) {
    FrameKey k;
    k.cell = cellKey(src);
    k.devW = devW;
    k.devH = devH;
    k.flip = flip;
    k.flashQ = static_cast<int>(std::lround(std::clamp(flash, 0.f, 1.f) * 32.f));
    k.interp = static_cast<int>(interp);
    return k;
}

void FrameCache::beginFrame(unsigned generation) {
    if (!hasGeneration_ || generation != generation_) {
        clearAll();
        generation_ = generation;
        hasGeneration_ = true;
    }
    ++frame_;

    // 다 만든 그림을 가져온다. 뒤 스레드가 잡고 있으면 다음 프레임에 가져간다(그리는 스레드를 세우지 않는다).
    std::vector<Done> got;
    {
        std::unique_lock<std::mutex> lock(m_, std::try_to_lock);
        if (lock.owns_lock()) got.swap(done_);
    }
    for (Done& d : got) {
        pending_.erase(d.key);
        if (d.epoch != epoch_ || !d.bmp) continue;
        const size_t b = static_cast<size_t>(d.key.devW) * d.key.devH * 4;
        auto it = frames_.find(d.key);
        if (it != frames_.end()) bytes_ -= it->second.bytes;
        frames_[d.key] = Entry{std::move(d.bmp), frame_, b};
        bytes_ += b;
    }

    for (auto it = seen_.begin(); it != seen_.end();) {
        if (it->second + 2 < frame_) it = seen_.erase(it);
        else ++it;
    }
    evict();
}

Bitmap* FrameCache::find(const Source& src, int devW, int devH, bool flip, float flash, InterpolationMode interp) {
    if (!src.bmp || devW <= 0 || devH <= 0) return nullptr;
    const FrameKey key = frameKey(src, devW, devH, flip, flash, interp);
    auto it = frames_.find(key);
    if (it != frames_.end()) {
        it->second.lastUsed = frame_;
        return it->second.bmp.get();
    }
    if (pending_.count(key)) return nullptr;
    auto s = seen_.find(key);
    if (s != seen_.end() && s->second + 1 == frame_) submit(key, src);
    seen_[key] = frame_;
    return nullptr;
}

void FrameCache::prefetch(const Source& src, int devW, int devH, bool flip, float flash, InterpolationMode interp) {
    if (!src.bmp || devW <= 0 || devH <= 0) return;
    const FrameKey key = frameKey(src, devW, devH, flip, flash, interp);
    auto it = frames_.find(key);
    if (it != frames_.end()) {
        it->second.lastUsed = frame_;  // 곧 쓸 것이니 버리지 않는다
        return;
    }
    if (pending_.count(key)) return;
    submit(key, src);
}

Bitmap* FrameCache::cell(const Source& src) {
    if (!src.bmp || src.w <= 0 || src.h <= 0 || src.x < 0 || src.y < 0) return nullptr;
    const CellKey key = cellKey(src);
    auto it = cells_.find(key);
    if (it != cells_.end()) {
        it->second.lastUsed = frame_;
        return it->second.bmp.get();
    }
    if (static_cast<UINT>(src.x + src.w) > src.bmp->GetWidth() ||
        static_cast<UINT>(src.y + src.h) > src.bmp->GetHeight())
        return nullptr;
    auto copy = copyRect(*src.bmp, Rect(src.x, src.y, src.w, src.h));
    if (!copy) return nullptr;
    const size_t b = static_cast<size_t>(src.w) * src.h * 4;
    Bitmap* raw = copy.get();
    cells_[key] = Entry{std::move(copy), frame_, b};
    bytes_ += b;
    return raw;
}

void FrameCache::submit(const FrameKey& key, const Source& src) {
    Bitmap* c = cell(src);
    if (!c) return;
    // 뒤 스레드 전용 복사본. 잘라 둔 칸은 그리는 스레드가 대신 그리기에 계속 쓴다.
    auto copy = copyRect(*c, Rect(0, 0, static_cast<INT>(c->GetWidth()), static_cast<INT>(c->GetHeight())));
    if (!copy) return;
    startWorkers();
    {
        std::lock_guard<std::mutex> lock(m_);
        // 밀린 일이 많으면 가장 오래된 것을 버린다 — 이미 지나간 프레임의 그림일 가능성이 크다.
        while (queue_.size() >= cfg::kFrameCacheMaxQueue) {
            pending_.erase(queue_.front().key);
            queue_.pop_front();
        }
        queue_.push_back(Job{key, std::move(copy), epoch_});
    }
    pending_[key] = true;
    cv_.notify_one();
}

void FrameCache::startWorkers() {
    if (!workers_.empty()) return;
    {
        std::lock_guard<std::mutex> lock(m_);
        stop_ = false;
    }
    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned n = std::clamp(hw > 1 ? hw - 1 : 1u, 1u, cfg::kFrameCacheMaxWorkers);
    for (unsigned i = 0; i < n; ++i) workers_.emplace_back([this] { workerLoop(); });
}

void FrameCache::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        auto bmp = scaleCell(*job.cell, job.key.devW, job.key.devH, job.key.flip, job.key.flashQ / 32.f,
                             static_cast<InterpolationMode>(job.key.interp));
        job.cell.reset();
        std::lock_guard<std::mutex> lock(m_);
        if (stop_ || job.epoch != epoch_) continue;  // 그사이 비웠다 — 결과를 버린다
        done_.push_back(Done{job.key, std::move(bmp), job.epoch});
    }
}

void FrameCache::clearAll() {
    std::vector<Done> stale;
    std::deque<Job> dropped;
    {
        std::lock_guard<std::mutex> lock(m_);
        ++epoch_;
        dropped.swap(queue_);
        stale.swap(done_);
    }
    frames_.clear();
    cells_.clear();
    seen_.clear();
    pending_.clear();
    bytes_ = 0;
}

void FrameCache::evict() {
    if (bytes_ <= cfg::kFrameCacheMaxBytes) return;
    // 이번 프레임에 쓴 것은 남기고, 오래 안 쓴 것부터 예산의 3/4 까지 버린다.
    struct Victim {
        uint64_t lastUsed;
        bool isCell;
        const void* key;
    };
    std::vector<Victim> victims;
    for (const auto& [k, e] : frames_)
        if (e.lastUsed < frame_) victims.push_back({e.lastUsed, false, &k});
    for (const auto& [k, e] : cells_)
        if (e.lastUsed < frame_) victims.push_back({e.lastUsed, true, &k});
    std::sort(victims.begin(), victims.end(),
              [](const Victim& a, const Victim& b) { return a.lastUsed < b.lastUsed; });
    const size_t target = cfg::kFrameCacheMaxBytes / 4 * 3;
    for (const Victim& v : victims) {
        if (bytes_ <= target) break;
        if (v.isCell) {
            auto it = cells_.find(*static_cast<const CellKey*>(v.key));
            bytes_ -= it->second.bytes;
            cells_.erase(it);
        } else {
            auto it = frames_.find(*static_cast<const FrameKey*>(v.key));
            bytes_ -= it->second.bytes;
            frames_.erase(it);
        }
    }
}

void FrameCache::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_);
        stop_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    for (std::thread& t : workers_)
        if (t.joinable()) t.join();
    workers_.clear();
    {
        std::lock_guard<std::mutex> lock(m_);
        done_.clear();
    }
    frames_.clear();
    cells_.clear();
    seen_.clear();
    pending_.clear();
    bytes_ = 0;
    hasGeneration_ = false;
}

}  // namespace dt::win
