// 플랫폼과 무관한 2D 수학 도구. Windows 헤더를 포함하지 않는다.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace dt {

constexpr float kPi = 3.14159265358979f;

struct Vec2 {
    float x = 0.f;
    float y = 0.f;

    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }

    float length() const { return std::sqrt(x * x + y * y); }
    Vec2 normalized() const {
        float len = length();
        return len > 1e-5f ? Vec2{x / len, y / len} : Vec2{0.f, 0.f};
    }
};

inline float distance(const Vec2& a, const Vec2& b) { return (a - b).length(); }

// 점 p와 선분 ab 사이의 최단 거리
inline float distancePointSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    Vec2 ab = b - a;
    float len2 = ab.x * ab.x + ab.y * ab.y;
    if (len2 < 1e-6f) return distance(p, a);
    float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2;
    t = std::clamp(t, 0.f, 1.f);
    return distance(p, a + ab * t);
}

struct RectF {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;

    RectF() = default;
    RectF(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

    float right() const { return x + w; }
    float bottom() const { return y + h; }
    Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }

    // margin만큼 안쪽으로 점을 가둔다
    Vec2 clampPoint(const Vec2& p, float margin) const {
        return {std::clamp(p.x, x + margin, std::max(x + margin, right() - margin)),
                std::clamp(p.y, y + margin, std::max(y + margin, bottom() - margin))};
    }
};

// 테스트에서 재현할 수 있도록 시드를 지정할 수 있는 난수기
class Rng {
public:
    explicit Rng(uint32_t seed = 12345u) : eng_(seed) {}
    void seed(uint32_t s) { eng_.seed(s); }
    float uniform(float lo = 0.f, float hi = 1.f) {
        std::uniform_real_distribution<float> d(lo, hi);
        return d(eng_);
    }
    int range(int lo, int hi) {  // [lo, hi]
        std::uniform_int_distribution<int> d(lo, hi);
        return d(eng_);
    }
    bool chance(float p) { return uniform() < p; }

private:
    std::mt19937 eng_;
};

}  // namespace dt
