#include "Sprites.h"

namespace dt::win {

namespace {
// assets/README.md의 파일 목록과 같아야 한다(기존 정지 이미지).
const char* kSpriteKeys[] = {
    "dragon_fire", "dragon_earth", "dragon_wind", "dragon_water",
    "enemy_imp",   "boss_guardian", "button_settings",
};

std::unique_ptr<Gdiplus::Bitmap> loadPng(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return nullptr;
    std::unique_ptr<Gdiplus::Bitmap> bmp(Gdiplus::Bitmap::FromFile(p.wstring().c_str(), FALSE));
    if (bmp && bmp->GetLastStatus() == Gdiplus::Ok && bmp->GetWidth() > 0 && bmp->GetHeight() > 0) return bmp;
    return nullptr;
}
}  // namespace

void Sprites::load(const std::filesystem::path& assetDir) {
    images_.clear();
    sheets_.clear();
    warnings_.clear();

    for (const char* key : kSpriteKeys) {
        if (auto bmp = loadPng(assetDir / (std::string(key) + ".png"))) images_[key] = std::move(bmp);
    }
    // 스킬 아이콘은 종이 늘어날 수 있으므로 등록된 종을 돌며 읽는다.
    for (int i = 0; i < speciesCount(); ++i) {
        const SpeciesDef& def = speciesDef(static_cast<Species>(i));
        for (int n = 1; n <= 3; ++n) {
            std::string key = "skill_" + def.key + "_" + std::to_string(n);
            if (auto bmp = loadPng(assetDir / (key + ".png"))) images_[key] = std::move(bmp);
        }
        // 종 폴더: 정지 이미지(dragon.png)와 상태별 시트
        std::filesystem::path dir = assetDir / "dragons" / def.key;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        if (auto bmp = loadPng(dir / "dragon.png")) images_["dragon_" + def.key] = std::move(bmp);
        loadSheetsFor(dir, def);
    }
}

void Sprites::loadSheetsFor(const std::filesystem::path& dir, const SpeciesDef& def) {
    for (int i = 0; i < kAnimStateCount; ++i) {
        AnimState st = static_cast<AnimState>(i);
        std::string name = animStateName(st);
        auto bmp = loadPng(dir / (name + ".png"));
        if (!bmp) continue;

        int w = static_cast<int>(bmp->GetWidth());
        int h = static_cast<int>(bmp->GetHeight());
        int frameW = def.anims[i].frameW;
        if (frameW <= 0) frameW = h;  // frame_w가 없으면 정사각형 칸으로 본다
        if (frameW > w) frameW = w;
        if (w % frameW != 0) {
            warnings_.push_back("dragons/" + def.key + "/" + name + ".png: 너비 " + std::to_string(w) +
                                "가 칸 너비 " + std::to_string(frameW) + "로 나누어떨어지지 않습니다");
            continue;
        }
        SheetData s;
        s.frames = w / frameW;
        s.frameW = frameW;
        s.frameH = h;
        s.bmp = std::move(bmp);
        sheets_[def.key + "/" + name] = std::move(s);
    }
}

Gdiplus::Bitmap* Sprites::get(const std::string& key) const {
    auto it = images_.find(key);
    return it == images_.end() ? nullptr : it->second.get();
}

Sheet Sprites::sheet(const std::string& speciesKey, AnimState state) const {
    // 대체 규칙(docs/character-pipeline.md 4장): move/attack/hit/death가 없으면 idle로 그린다.
    AnimState order[2] = {state, AnimState::Idle};
    for (AnimState s : order) {
        auto it = sheets_.find(speciesKey + "/" + animStateName(s));
        if (it == sheets_.end()) continue;
        Sheet out;
        out.bmp = it->second.bmp.get();
        out.frames = it->second.frames;
        out.frameW = it->second.frameW;
        out.frameH = it->second.frameH;
        return out;
    }
    return Sheet{};
}

bool Sprites::hasAnySheet(const std::string& speciesKey) const {
    return sheets_.find(speciesKey + "/idle") != sheets_.end();
}

}  // namespace dt::win
