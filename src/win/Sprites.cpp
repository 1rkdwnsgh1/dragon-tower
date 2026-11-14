#include "Sprites.h"

#include <algorithm>
#include <array>
#include <fstream>

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
        // v1.5 스킬 아이콘: 종 폴더의 icon_1.png ~ icon_N.png. 없으면 글자로 그린다.
        for (size_t n = 0; n < def.skills.size(); ++n) {
            std::string file = "icon_" + std::to_string(n + 1) + ".png";
            if (auto bmp = loadPng(dir / file))
                images_["skillicon_" + def.key + "_" + std::to_string(n + 1)] = std::move(bmp);
        }
        loadSheetsFor(dir, def.anims, def.key, "dragons/" + def.key);
        loadSkillSheets(dir, def, "dragons/" + def.key);  // v1.4 스킬 전용 모션
    }

    // UI 스킨 폴더 (v0.9)
    loadUiSkins(assetDir / "ui");

    // 적 폴더 (v0.4)
    for (int i = 0; i < enemyCount(); ++i) {
        const EnemyDef& def = enemyDef(static_cast<EnemyKind>(i));
        // v1.6: art_from= 을 적은 변종은 다른 적 폴더의 시트를 그대로 읽는다(그림 복사 없음).
        // 읽어 넣는 자리는 자기 키("enemy:<키>")이고, [anim.*] 설정은 자기 enemy.txt 의 것을 쓴다.
        const std::string artKey = def.artFrom.empty() ? def.key : def.artFrom;
        std::filesystem::path dir = assetDir / "enemies" / artKey;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        loadSheetsFor(dir, def.anims, "enemy:" + def.key, "enemies/" + artKey);
    }
}

bool Sprites::loadSheetFile(const std::filesystem::path& file, const AnimDef& anim, const std::string& key,
                            const std::string& label) {
    auto bmp = loadPng(file);
    if (!bmp) return false;

    int w = static_cast<int>(bmp->GetWidth());
    int h = static_cast<int>(bmp->GetHeight());
    int frameW = anim.frameW;
    if (frameW <= 0) frameW = h;  // frame_w가 없으면 정사각형 칸으로 본다
    if (frameW > w) frameW = w;
    if (frameW <= 0 || w % frameW != 0) {
        warnings_.push_back(label + ": 너비 " + std::to_string(w) + "가 칸 너비 " + std::to_string(frameW) +
                            "로 나누어떨어지지 않습니다");
        return false;
    }
    SheetData s;
    s.frames = w / frameW;
    s.frameW = frameW;
    s.frameH = h;
    s.bmp = std::move(bmp);
    sheets_[key] = std::move(s);
    return true;
}

void Sprites::loadSheetsFor(const std::filesystem::path& dir, const AnimDef* anims, const std::string& prefix,
                            const std::string& label) {
    for (int i = 0; i < kAnimStateCount; ++i) {
        AnimState st = static_cast<AnimState>(i);
        std::string name = animStateName(st);
        loadSheetFile(dir / (name + ".png"), anims[i], prefix + "/" + name, label + "/" + name + ".png");
    }
}

// v1.4: 스킬마다 motion= 으로 적어 둔 이름의 시트를 읽는다. 같은 이름을 여러 스킬이
// 함께 써도 되고(한 번만 읽는다), 파일이 없으면 조용히 넘어가 공용 attack 으로 그려진다.
void Sprites::loadSkillSheets(const std::filesystem::path& dir, const SpeciesDef& def, const std::string& label) {
    for (const SkillDef& sk : def.skills) {
        if (sk.motion.empty()) continue;
        std::string key = def.key + "/skill_" + sk.motion;
        if (sheets_.count(key) != 0) continue;  // 다른 스킬이 이미 읽었다
        std::string file = "skill_" + sk.motion + ".png";
        loadSheetFile(dir / file, sk.motionAnim, key, label + "/" + file);
    }
}

Sheet Sprites::skillSheet(const std::string& speciesKey, const std::string& motion) const {
    Sheet out;
    if (motion.empty()) return out;
    auto it = sheets_.find(speciesKey + "/skill_" + motion);
    if (it == sheets_.end()) return out;
    out.bmp = it->second.bmp.get();
    out.frames = it->second.frames;
    out.frameW = it->second.frameW;
    out.frameH = it->second.frameH;
    return out;
}

// ui.txt 의 9조각 여백을 읽는다. 형식은 dragon.txt 와 같은 `[구역]` + `키=값` 이다.
// 파일이 없거나 줄이 이상해도 그냥 넘어간다. 여백이 없으면 늘리지 않고 그대로 그린다.
void Sprites::loadUiSkins(const std::filesystem::path& uiDir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(uiDir, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(uiDir, ec)) {
        if (!entry.is_directory()) continue;
        std::string skin = entry.path().filename().string();

        // 먼저 여백 표를 읽는다(조각 이름 -> 네 변)
        std::map<std::string, std::array<int, 4>> margins;
        std::map<std::string, UiFit> fits;
        std::ifstream f(entry.path() / "ui.txt");
        std::string line, section;
        while (std::getline(f, line)) {
            // 주석과 공백 제거
            size_t c = line.find_first_of(";#");
            if (c != std::string::npos) line = line.substr(0, c);
            size_t b = line.find_first_not_of(" \t\r\n");
            if (b == std::string::npos) continue;
            size_t e = line.find_last_not_of(" \t\r\n");
            line = line.substr(b, e - b + 1);
            if (line.empty()) continue;
            if (line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                margins[section];  // 구역만 있어도 자리를 만든다
                continue;
            }
            size_t eq = line.find('=');
            if (eq == std::string::npos || section.empty()) continue;
            std::string key = line.substr(0, eq);
            if (key == "fit") {
                std::string v = line.substr(eq + 1);
                if (v == "height") fits[section] = UiFit::Height;
                else if (v == "contain") fits[section] = UiFit::Contain;
                else if (v == "nine") fits[section] = UiFit::Nine;
                else warnings_.push_back("ui/" + skin + "/" + section + ": fit 값이 이상합니다 (" + v + ")");
                continue;
            }
            int val = 0;
            try {
                val = std::stoi(line.substr(eq + 1));
            } catch (...) {
                continue;
            }
            if (val < 0) val = 0;
            auto& m = margins[section];
            if (key == "left") m[0] = val;
            else if (key == "top") m[1] = val;
            else if (key == "right") m[2] = val;
            else if (key == "bottom") m[3] = val;
        }

        // PNG를 읽는다. ui.txt 에 없는 조각도 그대로 쓴다(늘리지 않음).
        for (const auto& png : std::filesystem::directory_iterator(entry.path(), ec)) {
            if (png.path().extension() != ".png") continue;
            std::string name = png.path().stem().string();
            auto bmp = loadPng(png.path());
            if (!bmp) {
                warnings_.push_back("ui/" + skin + "/" + name + ".png: 그림을 읽지 못했습니다");
                continue;
            }
            UiData u;
            auto it = margins.find(name);
            if (it != margins.end()) {
                int w = static_cast<int>(bmp->GetWidth());
                int h = static_cast<int>(bmp->GetHeight());
                // 여백이 그림보다 크면 9조각 계산이 뒤집힌다. 줄여서 쓰고 알린다.
                if (it->second[0] + it->second[2] >= w || it->second[1] + it->second[3] >= h) {
                    warnings_.push_back("ui/" + skin + "/" + name + ": 여백이 그림보다 큽니다. 줄여서 씁니다");
                    it->second[0] = std::min(it->second[0], (w - 1) / 2);
                    it->second[2] = std::min(it->second[2], (w - 1) / 2);
                    it->second[1] = std::min(it->second[1], (h - 1) / 2);
                    it->second[3] = std::min(it->second[3], (h - 1) / 2);
                }
                u.left = it->second[0];
                u.top = it->second[1];
                u.right = it->second[2];
                u.bottom = it->second[3];
            }
            auto fi = fits.find(name);
            if (fi != fits.end()) {
                u.fit = fi->second;
                u.hasCfg = true;
            }
            if (it != margins.end()) u.hasCfg = true;
            u.bmp = std::move(bmp);
            ui_[skin + "/" + name] = std::move(u);
        }
    }

    // 스킨에 ui.txt 가 없거나 그 조각이 안 적혀 있으면 default 의 설정을 물려받는다.
    // 그림만 바꾼 스킨(색만 다른 것)이 여백·맞춤 방식을 잃고 통째로 늘어나는 것을 막는다.
    for (auto& [key, data] : ui_) {
        if (data.hasCfg) continue;
        size_t slash = key.find('/');
        if (slash == std::string::npos) continue;
        if (key.compare(0, slash, "default") == 0) continue;
        auto base = ui_.find("default/" + key.substr(slash + 1));
        if (base == ui_.end() || !base->second.hasCfg) continue;
        data.left = base->second.left;
        data.top = base->second.top;
        data.right = base->second.right;
        data.bottom = base->second.bottom;
        data.fit = base->second.fit;
    }
}

UiPiece Sprites::ui(const std::string& skinKey, const std::string& name) const {
    // 고른 스킨에 없으면 default 에서 찾는다. 둘 다 없으면 빈 조각(도형으로 그린다).
    for (const std::string& skin : {skinKey, std::string("default")}) {
        auto it = ui_.find(skin + "/" + name);
        if (it == ui_.end()) continue;
        UiPiece p;
        p.bmp = it->second.bmp.get();
        p.left = it->second.left;
        p.top = it->second.top;
        p.right = it->second.right;
        p.bottom = it->second.bottom;
        p.fit = it->second.fit;
        return p;
    }
    return UiPiece{};
}

Gdiplus::Bitmap* Sprites::get(const std::string& key) const {
    auto it = images_.find(key);
    return it == images_.end() ? nullptr : it->second.get();
}

Sheet Sprites::sheet(const std::string& speciesKey, AnimState state) const {
    // 대체 규칙(docs/character-pipeline.md 4장): 없는 상태는 idle로 그린다.
    // fly 시트가 없으면 move를 먼저 써 본다(걷는 그림이 가만히 있는 것보다 낫다).
    AnimState order[3] = {state, state == AnimState::Fly ? AnimState::Move : AnimState::Idle, AnimState::Idle};
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
