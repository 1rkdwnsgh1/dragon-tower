#include "Sprites.h"

namespace dt::win {

namespace {
// assets/README.md의 파일 목록과 같아야 한다.
const char* kSpriteKeys[] = {
    "dragon_fire", "dragon_earth", "dragon_wind", "dragon_water",
    "enemy_imp",   "boss_guardian", "button_settings",
    "skill_fire_1", "skill_fire_2", "skill_fire_3",
    "skill_earth_1", "skill_earth_2", "skill_earth_3",
    "skill_wind_1", "skill_wind_2", "skill_wind_3",
    "skill_water_1", "skill_water_2", "skill_water_3",
};
}  // namespace

void Sprites::load(const std::filesystem::path& assetDir) {
    images_.clear();
    for (const char* key : kSpriteKeys) {
        std::filesystem::path p = assetDir / (std::string(key) + ".png");
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) continue;
        std::unique_ptr<Gdiplus::Bitmap> bmp(Gdiplus::Bitmap::FromFile(p.wstring().c_str(), FALSE));
        if (bmp && bmp->GetLastStatus() == Gdiplus::Ok && bmp->GetWidth() > 0) {
            images_[key] = std::move(bmp);
        }
    }
}

Gdiplus::Bitmap* Sprites::get(const std::string& key) const {
    auto it = images_.find(key);
    return it == images_.end() ? nullptr : it->second.get();
}

}  // namespace dt::win
