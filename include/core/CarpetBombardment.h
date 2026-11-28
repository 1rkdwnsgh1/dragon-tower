#pragma once

#include "core/MathUtil.h"

namespace dt::carpet {

// Playback values from the approved V1.1 metadata.
struct Atlas {
    const char* key;
    int width, height, columns, rows, frames, fps, anchorX, anchorY;
    float scale;
    bool loop;
    float duration() const { return static_cast<float>(frames) / fps; }
};
inline constexpr Atlas atlases[] = {
    {"carpet_projectile", 576, 256, 4, 2, 8, 12, 541, 126, 0.13f, true},
    {"carpet_muzzle_ignition", 384, 352, 3, 2, 6, 16, 116, 125, 0.153f, false},
    {"carpet_residual_smoke", 512, 448, 4, 3, 12, 12, 252, 367, 0.276f, false},
    {"carpet_ground_shockwave", 512, 128, 3, 2, 6, 24, 256, 62, 0.25f, false},
    {"carpet_impact_flash", 384, 232, 4, 1, 4, 24, 192, 143, 0.19f, false},
    {"carpet_impact_explosion", 576, 520, 4, 3, 12, 16, 276, 392, 0.267f, false},
};
enum Layer { projectile, ignition, smoke, shockwave, flash, explosion };
inline constexpr float impactDelay[] = {0.f, 0.f, .258f, 0.f, 0.f, .037f};
struct Shot { float launch, flight, impactScale; };
inline constexpr Shot shots[] = {
    {0.25f, 0.47f, 0.96f},
    {0.39f, 0.49f, 1.04f},
    {0.83f, 0.47f, 0.99f},
    {0.99f, 0.48f, 0.94f},
    {1.42f, 0.49f, 1.07f},
    {1.57f, 0.50f, 1.02f},
};
inline constexpr int shotCount = sizeof(shots) / sizeof(shots[0]);
inline constexpr float lastDrop = shots[shotCount - 1].launch;
inline constexpr float recoveryDuration = 3.f;
inline constexpr float recoverySpeed = 1.2f;
inline constexpr float runSpeed = 1.6f;
inline constexpr float flightEnd = lastDrop + recoveryDuration;
inline constexpr float clearance = 16.f;

inline float lift(float time, float scale) {
    const float rise = std::clamp(time / .24f, 0.f, 1.f);
    const float fall = std::clamp((flightEnd - time) / .50f, 0.f, 1.f);
    const float phase = rise * fall;
    return 148.f * std::sqrt(std::clamp(scale, .5f, 4.f)) * phase * phase * (3.f - 2.f * phase);
}

inline int flyFrame(float time, float fps) {
    return static_cast<int>(time * fps + 1e-5f) % 24;
}

// Abdomen landmarks in the unchanged right-facing 256px flight cells.
inline Vec2 port(int frame) {
    static const Vec2 points[] = {
        {166,170},{166,170},{165,173},{165,173},{165,173},{166,172},
        {166,172},{166,172},{168,170},{168,170},{170,167},{165,134},
        {165,134},{168,143},{168,143},{169,152},{169,152},{169,152},
        {169,152},{170,153},{170,153},{170,153},{169,155},{169,155}
    };
    return points[frame];
}

}  // namespace dt::carpet
