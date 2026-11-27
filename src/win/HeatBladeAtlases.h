// Generated from the approved V6.1 metadata by tools/install_steel_heat_blade.py.
#pragma once

namespace dt::win::heatBlade {
struct Atlas {
    const char* path;
    int width, height, frames, fps, anchorX, anchorY;
    float scale;
    int z, startTick, stopTick;
    bool loop, mirror;
};
inline constexpr Atlas atlases[] = {
    {"embers_smoke/start_rear.png", 228, 116, 12, 60, 114, 58, 1.00000f, -9, 0, 12, false, true},
    {"embers_smoke/loop_rear.png", 228, 116, 192, 60, 114, 58, 1.00000f, -9, 12, 204, true, true},
    {"embers_smoke/end_rear.png", 228, 116, 36, 60, 114, 58, 1.00000f, -9, 204, 240, false, true},
    {"flame_main_outer/start_rear.png", 228, 116, 12, 60, 114, 58, 1.00000f, -8, 0, 12, false, true},
    {"flame_main_outer/loop_rear.png", 228, 116, 32, 60, 114, 58, 1.00000f, -8, 12, 204, true, true},
    {"flame_main_outer/end_rear.png", 228, 116, 36, 60, 114, 58, 1.00000f, -8, 204, 240, false, true},
    {"flame_sub_stream/start_rear.png", 228, 116, 12, 60, 114, 58, 1.00000f, -7, 0, 12, false, true},
    {"flame_sub_stream/loop_rear.png", 228, 116, 24, 60, 114, 58, 1.00000f, -7, 12, 204, true, true},
    {"flame_sub_stream/end_rear.png", 228, 116, 36, 60, 114, 58, 1.00000f, -7, 204, 240, false, true},
    {"flame_speed_trail/start_rear.png", 228, 116, 12, 60, 114, 58, 1.00000f, -6, 0, 12, false, true},
    {"flame_speed_trail/loop_rear.png", 228, 116, 192, 60, 114, 58, 1.00000f, -6, 12, 204, true, true},
    {"flame_speed_trail/end_rear.png", 228, 116, 36, 60, 114, 58, 1.00000f, -6, 204, 240, false, true},
    {"orbit_blades_back/start_blade_rear.png", 528, 256, 12, 60, 264, 128, 0.50000f, -2, 0, 12, false, true},
    {"orbit_blades_back/loop_blade_rear.png", 528, 256, 32, 60, 264, 128, 0.50000f, -2, 12, 204, true, true},
    {"orbit_blades_back/end_blade_rear.png", 528, 256, 36, 60, 264, 128, 0.50000f, -2, 204, 240, false, true},
    {"flame_main_outer/start_front.png", 228, 116, 12, 60, 114, 58, 1.00000f, 10, 0, 12, false, true},
    {"flame_main_outer/loop_front.png", 228, 116, 32, 60, 114, 58, 1.00000f, 10, 12, 204, true, true},
    {"flame_main_outer/end_front.png", 228, 116, 36, 60, 114, 58, 1.00000f, 10, 204, 240, false, true},
    {"flame_sub_stream/start_front.png", 228, 116, 12, 60, 114, 58, 1.00000f, 14, 0, 12, false, true},
    {"flame_sub_stream/loop_front.png", 228, 116, 24, 60, 114, 58, 1.00000f, 14, 12, 204, true, true},
    {"flame_sub_stream/end_front.png", 228, 116, 36, 60, 114, 58, 1.00000f, 14, 204, 240, false, true},
    {"flame_speed_trail/start_front.png", 228, 116, 12, 60, 114, 58, 1.00000f, 18, 0, 12, false, true},
    {"flame_speed_trail/loop_front.png", 228, 116, 192, 60, 114, 58, 1.00000f, 18, 12, 204, true, true},
    {"flame_speed_trail/end_front.png", 228, 116, 36, 60, 114, 58, 1.00000f, 18, 204, 240, false, true},
    {"orbit_blades_front/start_blade_front.png", 528, 256, 12, 60, 264, 128, 0.50000f, 30, 0, 12, false, true},
    {"orbit_blades_front/loop_blade_front.png", 528, 256, 32, 60, 264, 128, 0.50000f, 30, 12, 204, true, true},
    {"orbit_blades_front/end_blade_front.png", 528, 256, 36, 60, 264, 128, 0.50000f, 30, 204, 240, false, true},
    {"embers_smoke/start_front.png", 228, 116, 12, 60, 114, 58, 1.00000f, 34, 0, 12, false, true},
    {"embers_smoke/loop_front.png", 228, 116, 192, 60, 114, 58, 1.00000f, 34, 12, 204, true, true},
    {"embers_smoke/end_front.png", 228, 116, 36, 60, 114, 58, 1.00000f, 34, 204, 240, false, true},
    {"outer_flame/start_wrap_front.png", 528, 256, 12, 60, 264, 128, 0.50000f, 40, 0, 12, false, true},
    {"outer_flame/loop_wrap_front.png", 528, 256, 32, 60, 264, 128, 0.50000f, 40, 12, 204, true, true},
    {"outer_flame/end_wrap_front.png", 528, 256, 36, 60, 264, 128, 0.50000f, 40, 204, 240, false, true},
    {"projectile_b4/flight_left_12f.png", 320, 128, 12, 60, 44, 66, 0.40000f, 0, 0, 0, true, false},
    {"projectile_b4/flight_right_12f.png", 320, 128, 12, 60, 275, 66, 0.40000f, 0, 0, 0, true, false},
    {"projectile_b4/launch_8f.png", 128, 128, 8, 20, 64, 64, 0.40000f, 0, 0, 0, false, false},
    {"hit_fx/pierce_8f.png", 128, 128, 8, 20, 64, 64, 0.40000f, 0, 0, 0, false, false},
    {"disappear_fx/ember_break_8f.png", 128, 128, 8, 20, 64, 64, 0.40000f, 0, 0, 0, false, false},
};
inline constexpr int orbitLayers = 33;
inline constexpr int flightLeft = 33, flightRight = 34, launch = 35, contact = 36, disappear = 37;
}  // namespace dt::win::heatBlade
